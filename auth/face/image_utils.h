#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/**
 * Minimal RGB image container replacing cv::Mat.
 * Stores 3-channel (RGB) uint8 data in row-major order.
 */
struct ImageRGB {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> data;  // size = width * height * 3

  ImageRGB() = default;
  ImageRGB(int w, int h) : width(w), height(h), data(w * h * 3, 0) {}
  ImageRGB(int w, int h, const uint8_t *src) : width(w), height(h), data(src, src + w * h * 3) {}

  bool empty() const { return data.empty(); }
  uint8_t *ptr() { return data.data(); }
  const uint8_t *ptr() const { return data.data(); }

  uint8_t &at(int y, int x, int c) { return data[(y * width + x) * 3 + c]; }
  const uint8_t &at(int y, int x, int c) const { return data[(y * width + x) * 3 + c]; }

  ImageRGB crop(int x1, int y1, int x2, int y2) const {
    x1 = std::max(0, x1);
    y1 = std::max(0, y1);
    x2 = std::min(width, x2);
    y2 = std::min(height, y2);
    int cw = x2 - x1, ch = y2 - y1;
    if (cw <= 0 || ch <= 0) return {};
    ImageRGB out(cw, ch);
    for (int r = 0; r < ch; r++)
      std::memcpy(&out.data[r * cw * 3], &data[((y1 + r) * width + x1) * 3], cw * 3);
    return out;
  }

  ImageRGB clone() const {
    ImageRGB out;
    out.width = width;
    out.height = height;
    out.data = data;
    return out;
  }
};

/**
 * Bilinear resize.
 */
inline ImageRGB image_resize(const ImageRGB &src, int tw, int th) {
  if (src.empty()) return {};
  ImageRGB dst(tw, th);
  float sx = (float)src.width / tw;
  float sy = (float)src.height / th;
  for (int y = 0; y < th; y++) {
    float fy = (y + 0.5f) * sy - 0.5f;
    int y0 = (int)std::floor(fy);
    int y1 = y0 + 1;
    float wy = fy - y0;
    y0 = std::max(0, std::min(y0, src.height - 1));
    y1 = std::max(0, std::min(y1, src.height - 1));
    for (int x = 0; x < tw; x++) {
      float fx = (x + 0.5f) * sx - 0.5f;
      int x0 = (int)std::floor(fx);
      int x1_c = x0 + 1;
      float wx = fx - x0;
      x0 = std::max(0, std::min(x0, src.width - 1));
      x1_c = std::max(0, std::min(x1_c, src.width - 1));
      for (int c = 0; c < 3; c++) {
        float v = (1 - wy) * ((1 - wx) * src.at(y0, x0, c) + wx * src.at(y0, x1_c, c)) +
                  wy * ((1 - wx) * src.at(y1, x0, c) + wx * src.at(y1, x1_c, c));
        dst.at(y, x, c) = (uint8_t)std::min(255.0f, std::max(0.0f, v + 0.5f));
      }
    }
  }
  return dst;
}

/**
 * Letterbox resize: scale to fit target keeping aspect ratio, pad with pad_val.
 */
inline ImageRGB image_letterbox(const ImageRGB &src, int tw, int th, uint8_t pad_val = 114) {
  if (src.empty()) return {};
  float scale = std::min((float)tw / src.width, (float)th / src.height);
  int nw = (int)std::round(src.width * scale);
  int nh = (int)std::round(src.height * scale);
  ImageRGB resized = image_resize(src, nw, nh);

  ImageRGB out(tw, th);
  std::memset(out.data.data(), pad_val, out.data.size());

  int dx = (tw - nw) / 2;
  int dy = (th - nh) / 2;
  for (int r = 0; r < nh; r++)
    std::memcpy(&out.data[((dy + r) * tw + dx) * 3], &resized.data[r * nw * 3], nw * 3);
  return out;
}

/**
 * Resize with aspect-ratio preserving and zero-padding (for recognition).
 */
inline ImageRGB image_resize_pad(const ImageRGB &src, int tw, int th) {
  return image_letterbox(src, tw, th, 0);
}

/**
 * HWC RGB uint8 -> CHW float, normalized to [0,1].
 */
inline std::vector<float> image_to_chw(const ImageRGB &img) {
  int h = img.height, w = img.width;
  std::vector<float> out(3 * h * w);
  for (int c = 0; c < 3; c++)
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        out[c * h * w + y * w + x] = img.at(y, x, c) / 255.0f;
  return out;
}

/**
 * HWC RGB uint8 -> CHW float, with mean/std normalization.
 */
inline std::vector<float> image_to_chw_normalized(const ImageRGB &img, const float mean[3],
                                                  const float std[3]) {
  int h = img.height, w = img.width;
  std::vector<float> out(3 * h * w);
  for (int c = 0; c < 3; c++)
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        out[c * h * w + y * w + x] = (img.at(y, x, c) / 255.0f - mean[c]) / std[c];
  return out;
}

/**
 * Save image as BMP file (RGB input).
 */
#pragma pack(push, 1)
struct BMPFileHeader {
  uint16_t sig = 0x4D42;
  uint32_t file_size;
  uint16_t r1 = 0, r2 = 0;
  uint32_t data_offset = 54;
  uint32_t hdr_size = 40;
  int32_t bmp_width;
  int32_t bmp_height;
  uint16_t planes = 1;
  uint16_t bpp = 24;
  uint32_t compression = 0;
  uint32_t img_size;
  int32_t xppm = 2835, yppm = 2835;
  uint32_t clr_used = 0, clr_imp = 0;
};
#pragma pack(pop)

inline bool image_save_bmp(const std::string &path, const ImageRGB &img) {
  uint32_t row_stride = (img.width * 3 + 3) & ~3u;
  uint32_t img_size = row_stride * img.height;
  BMPFileHeader hdr;
  hdr.file_size = 54 + img_size;
  hdr.bmp_width = img.width;
  hdr.bmp_height = img.height;
  hdr.img_size = img_size;

  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fwrite(&hdr, 1, 54, f);

  uint32_t pad = row_stride - img.width * 3;
  uint8_t zeros[3] = {};
  for (int y = img.height - 1; y >= 0; y--) {
    for (int x = 0; x < img.width; x++) {
      uint8_t bgr[3] = {img.at(y, x, 2), img.at(y, x, 1), img.at(y, x, 0)};
      fwrite(bgr, 1, 3, f);
    }
    if (pad) fwrite(zeros, 1, pad, f);
  }
  fclose(f);
  return true;
}

/**
 * Load BMP file as RGB image.
 */
inline ImageRGB image_load_bmp(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return {};

  BMPFileHeader hdr;
  if (fread(&hdr, 1, 54, f) != 54 || hdr.sig != 0x4D42 || hdr.bpp != 24) {
    fclose(f);
    return {};
  }

  int w = hdr.bmp_width;
  int h = std::abs(hdr.bmp_height);
  bool top_down = (hdr.bmp_height < 0);
  uint32_t row_stride = (w * 3 + 3) & ~3u;

  fseek(f, hdr.data_offset, SEEK_SET);
  ImageRGB img(w, h);
  std::vector<uint8_t> row_buf(row_stride);

  for (int y = 0; y < h; y++) {
    int dst_y = top_down ? y : (h - 1 - y);
    if (fread(row_buf.data(), 1, row_stride, f) != row_stride) {
      fclose(f);
      return {};
    }
    for (int x = 0; x < w; x++) {
      img.at(dst_y, x, 0) = row_buf[x * 3 + 2];  // B -> R
      img.at(dst_y, x, 1) = row_buf[x * 3 + 1];  // G -> G
      img.at(dst_y, x, 2) = row_buf[x * 3 + 0];  // R -> B
    }
  }
  fclose(f);
  return img;
}

#endif  // IMAGE_UTILS_H
