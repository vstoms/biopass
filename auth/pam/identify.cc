#include "identify.h"

#include <openpnp-capture.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include <memory>
#include <mutex>

using namespace std;

namespace uuid {
static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_int_distribution<> dis(0, 15);
static std::uniform_int_distribution<> dis2(8, 11);

string v4() {
  stringstream ss;
  ss << std::hex;

  for (int i = 0; i < 8; i++) ss << dis(gen);

  ss << "-";
  for (int i = 0; i < 4; i++) ss << dis(gen);

  ss << "-4";
  for (int i = 0; i < 3; i++) ss << dis(gen);

  ss << "-";
  ss << dis2(gen);
  for (int i = 0; i < 3; i++) ss << dis(gen);

  ss << "-";
  for (int i = 0; i < 12; i++) ss << dis(gen);

  return ss.str();
}
}  // namespace uuid

namespace {
void capture_log_callback(uint32_t level, const char *message) {
  if (!message)
    return;
  if (strstr(message, "tjDecompressHeader2 failed: No error") != nullptr) {
    return;
  }

  string msg(message);
  while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
    msg.pop_back();
  }

  if (level <= 3) {
    spdlog::error("openpnp-capture: {}", msg);
  } else if (level == 4) {
    spdlog::warn("openpnp-capture: {}", msg);
  } else if (level >= 7) {
    spdlog::debug("openpnp-capture: {}", msg);
  } else {
    spdlog::info("openpnp-capture: {}", msg);
  }
}

void configure_capture_logging_once() {
  static std::once_flag once;
  std::call_once(once, []() { Cap_installCustomLogFunction(capture_log_callback); });
}

bool save_failed_face(const string &username, const ImageRGB &face, const string &reason) {
  string failedFacePath = biopass::debug_path(username) + "/" + reason + "." + uuid::v4() + ".jpg";
  if (!image_save(failedFacePath, face)) {
    spdlog::error("identify: Could not save failed face to {}", failedFacePath);
    return false;
  }
  return true;
}

bool process_anti_spoofing(FaceAntiSpoofing &faceAs, ImageRGB &face) {
  SpoofResult spoofCheck = faceAs.inference(face);
  if (spoofCheck.spoof) {
    spdlog::warn("identify: Spoof detected, score: {}", spoofCheck.score);
    return false;
  }
  return true;
}
void sleep_for(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); }

ImageRGB capture_frame(CapContext ctx, CapStream stream, uint32_t width, uint32_t height) {
  uint32_t buf_size = width * height * 3;
  std::vector<uint8_t> buf(buf_size);

  for (int i = 0; i < 1000; i++) {
    if (Cap_hasNewFrame(ctx, stream)) {
      if (Cap_captureFrame(ctx, stream, buf.data(), buf_size) == CAPRESULT_OK) {
        return ImageRGB((int)width, (int)height, buf.data());
      }
    }
    usleep(10000);
  }
  return {};
}
}  // namespace

int scan_face(const string &username, const biopass::FaceMethodConfig &face_config, int8_t retries,
              const int gap, bool anti_spoofing) {
  configure_capture_logging_once();
  CapContext ctx = Cap_createContext();
  if (!ctx) {
    spdlog::error("identify: Could not create capture context");
    return PAM_AUTH_ERR;
  }

  uint32_t device_count = Cap_getDeviceCount(ctx);
  if (device_count == 0) {
    spdlog::error("identify: No camera found");
    Cap_releaseContext(ctx);
    return PAM_AUTH_ERR;
  }

  CapFormatInfo fmt;
  Cap_getFormatInfo(ctx, 0, 0, &fmt);
  CapStream stream = Cap_openStream(ctx, 0, 0);
  if (stream < 0 || !Cap_isOpenStream(ctx, stream)) {
    spdlog::error("identify: Could not open camera stream");
    Cap_releaseContext(ctx);
    return PAM_AUTH_ERR;
  }

  // Warmup frames
  for (int i = 0, got = 0; got < 5 && i < 2000; i++) {
    uint32_t buf_size = fmt.width * fmt.height * 3;
    std::vector<uint8_t> tmp(buf_size);
    if (Cap_hasNewFrame(ctx, stream)) {
      Cap_captureFrame(ctx, stream, tmp.data(), buf_size);
      got++;
    } else {
      usleep(10000);
    }
  }

  std::vector<std::string> enrolledFaces = biopass::list_user_faces(username);
  if (enrolledFaces.empty()) {
    spdlog::error("identify: No face enrolled for user {}", username);
    Cap_closeStream(ctx, stream);
    Cap_releaseContext(ctx);
    return PAM_AUTH_ERR;
  }

  std::unique_ptr<FaceRecognition> faceReg;
  std::unique_ptr<FaceDetection> faceDetector;
  std::unique_ptr<FaceAntiSpoofing> faceAs;
  try {
    faceDetector = std::make_unique<FaceDetection>(face_config.detection.model);
  } catch (const std::exception &e) {
    std::string msg = e.what();
    size_t first_line = msg.find('\n');
    if (first_line != std::string::npos)
      msg = msg.substr(0, first_line);
    spdlog::error("identify: Failed to load detection model: {}", msg);
    Cap_closeStream(ctx, stream);
    Cap_releaseContext(ctx);
    return PAM_AUTH_ERR;
  }
  try {
    faceReg = std::make_unique<FaceRecognition>(face_config.recognition.model);
  } catch (const std::exception &e) {
    std::string msg = e.what();
    size_t first_line = msg.find('\n');
    if (first_line != std::string::npos)
      msg = msg.substr(0, first_line);
    spdlog::error("identify: Failed to load recognition model: {}", msg);
    Cap_closeStream(ctx, stream);
    Cap_releaseContext(ctx);
    return PAM_AUTH_ERR;
  }
  if (anti_spoofing) {
    try {
      faceAs = std::make_unique<FaceAntiSpoofing>(face_config.anti_spoofing.model);
    } catch (const std::exception &e) {
      std::string msg = e.what();
      size_t first_line = msg.find('\n');
      if (first_line != std::string::npos)
        msg = msg.substr(0, first_line);
      spdlog::error("identify: Failed to load anti-spoofing model: {}", msg);
      Cap_closeStream(ctx, stream);
      Cap_releaseContext(ctx);
      return PAM_AUTH_ERR;
    }
  }

  bool success = false;
  while (retries--) {
    ImageRGB loginFace = capture_frame(ctx, stream, fmt.width, fmt.height);
    if (loginFace.empty()) {
      spdlog::error("identify: Could not read frame");
      break;
    }

    std::vector<Detection> detectedImages = faceDetector->inference(loginFace);
    if (detectedImages.empty()) {
      spdlog::error("identify: No face detected");
      sleep_for(gap);
      continue;
    }

    ImageRGB face = detectedImages[0].image;
    if (anti_spoofing && !process_anti_spoofing(*faceAs, face)) {
      save_failed_face(username, face, "spoof");
      sleep_for(gap);
      continue;
    }

    // Match against all enrolled faces — succeed if any match
    bool matched = false;
    for (const auto &facePath : enrolledFaces) {
      ImageRGB preparedFace = image_load(facePath);
      if (preparedFace.empty())
        continue;
      MatchResult match = faceReg->match(preparedFace, face);
      if (match.similar) {
        matched = true;
        break;
      }
    }

    if (matched) {
      success = true;
      break;
    }

    save_failed_face(username, face, "not similar");
    sleep_for(gap);
  }

  Cap_closeStream(ctx, stream);
  Cap_releaseContext(ctx);
  return success ? PAM_SUCCESS : PAM_AUTH_ERR;
}
