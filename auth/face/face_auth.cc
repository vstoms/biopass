#include "face_auth.h"

#include <openpnp-capture.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

#include "auth_config.h"
#include "face_as.h"
#include "face_detection.h"
#include "face_recognition.h"
#include "image_utils.h"

namespace biopass {

namespace {
void capture_log_callback(uint32_t level, const char* message) {
  if (!message)
    return;
  if (std::strstr(message, "tjDecompressHeader2 failed: No error") != nullptr) {
    return;
  }

  std::string msg(message);
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

std::optional<CapDeviceID> resolve_camera_device_index(CapContext ctx) {
  const uint32_t count = Cap_getDeviceCount(ctx);
  if (count == 0) {
    return std::nullopt;
  }

  return static_cast<CapDeviceID>(0);
}
}  // namespace

std::string get_timestamp_string() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return std::to_string(ms);
}

void save_failed_face(const std::string& username, const ImageRGB& face,
                      const std::string& reason) {
  biopass::setup_config(username);
  std::string failedFacePath =
      biopass::debug_path(username) + "/" + reason + "." + get_timestamp_string() + ".jpg";
  if (!image_save(failedFacePath, face)) {
    spdlog::error("FaceAuth: Could not save failed face to {}", failedFacePath);
  }
}

bool FaceAuth::is_available() const {
  configure_capture_logging_once();
  CapContext ctx = Cap_createContext();
  if (!ctx)
    return false;

  uint32_t count = Cap_getDeviceCount(ctx);
  if (count == 0) {
    Cap_releaseContext(ctx);
    return false;
  }

  const auto device_index = resolve_camera_device_index(ctx);
  if (!device_index.has_value()) {
    Cap_releaseContext(ctx);
    return false;
  }

  CapStream stream = Cap_openStream(ctx, *device_index, 0);
  bool available = stream >= 0 && Cap_isOpenStream(ctx, stream);
  if (available)
    Cap_closeStream(ctx, stream);
  Cap_releaseContext(ctx);
  return available;
}

static ImageRGB capture_frame_from_camera() {
  configure_capture_logging_once();
  CapContext ctx = Cap_createContext();
  if (!ctx)
    return {};

  uint32_t count = Cap_getDeviceCount(ctx);
  if (count == 0) {
    Cap_releaseContext(ctx);
    return {};
  }

  const auto device_index = resolve_camera_device_index(ctx);
  if (!device_index.has_value()) {
    Cap_releaseContext(ctx);
    return {};
  }

  CapFormatInfo fmt;
  if (Cap_getFormatInfo(ctx, *device_index, 0, &fmt) != CAPRESULT_OK) {
    Cap_releaseContext(ctx);
    return {};
  }

  CapStream stream = Cap_openStream(ctx, *device_index, 0);
  if (stream < 0 || !Cap_isOpenStream(ctx, stream)) {
    Cap_releaseContext(ctx);
    return {};
  }

  uint32_t buf_size = fmt.width * fmt.height * 3;
  std::vector<uint8_t> buf(buf_size);

  // Warmup frames
  for (int i = 0, got = 0; got < 5 && i < 2000; i++) {
    if (Cap_hasNewFrame(ctx, stream)) {
      Cap_captureFrame(ctx, stream, buf.data(), buf_size);
      got++;
    } else {
      usleep(10000);
    }
  }

  // Capture actual frame
  ImageRGB result;
  for (int i = 0; i < 1000; i++) {
    if (Cap_hasNewFrame(ctx, stream)) {
      if (Cap_captureFrame(ctx, stream, buf.data(), buf_size) == CAPRESULT_OK) {
        result = ImageRGB((int)fmt.width, (int)fmt.height, buf.data());
        break;
      }
    }
    usleep(10000);
  }

  Cap_closeStream(ctx, stream);
  Cap_releaseContext(ctx);
  return result;
}

AuthResult FaceAuth::authenticate(const std::string& username, const AuthConfig& config,
                                  std::atomic<bool>* cancel_signal) {
  if (!this->is_available()) {
    spdlog::error("FaceAuth: Could not open camera");
    return AuthResult::Unavailable;
  }

  std::vector<std::string> enrolledFaces = biopass::list_faces(username);
  if (enrolledFaces.empty()) {
    spdlog::error("FaceAuth: No face enrolled for user {}, skipping", username);
    return AuthResult::Unavailable;
  }

  std::string recogModelPath = face_config_.recognition.model;
  std::string detectModelPath = face_config_.detection.model;
  if (!std::ifstream(recogModelPath).good() || !std::ifstream(detectModelPath).good()) {
    spdlog::error("FaceAuth: Model files not found for user {}, skipping", username);
    return AuthResult::Unavailable;
  }
  std::unique_ptr<FaceRecognition> faceReg;
  std::unique_ptr<FaceDetection> faceDetector;
  try {
    faceDetector = std::make_unique<FaceDetection>(detectModelPath);
  } catch (const std::exception& e) {
    std::string msg = e.what();
    size_t first_line = msg.find('\n');
    if (first_line != std::string::npos)
      msg = msg.substr(0, first_line);
    spdlog::error("FaceAuth: Failed to load detection model: {}, skipping", msg);
    return AuthResult::Unavailable;
  }
  try {
    faceReg = std::make_unique<FaceRecognition>(recogModelPath);
  } catch (const std::exception& e) {
    std::string msg = e.what();
    size_t first_line = msg.find('\n');
    if (first_line != std::string::npos)
      msg = msg.substr(0, first_line);
    spdlog::error("FaceAuth: Failed to load recognition model: {}, skipping", msg);
    return AuthResult::Unavailable;
  }

  if (cancel_signal && cancel_signal->load()) {
    return AuthResult::Failure;
  }

  ImageRGB loginFace = capture_frame_from_camera();
  if (loginFace.empty()) {
    spdlog::error("FaceAuth: Could not read frame");
    return AuthResult::Retry;
  }

  std::vector<Detection> detectedImages = faceDetector->inference(loginFace);
  if (detectedImages.empty()) {
    spdlog::error("FaceAuth: No face detected");
    return AuthResult::Retry;
  }

  ImageRGB face = detectedImages[0].image;

  if (config.anti_spoof) {
    std::string asModelPath = face_config_.anti_spoofing.model;
    try {
      FaceAntiSpoofing faceAs(asModelPath);
      SpoofResult spoofCheck = faceAs.inference(face);
      if (spoofCheck.spoof) {
        spdlog::warn("FaceAuth: Spoof detected, score: {}", spoofCheck.score);
        if (config.debug) {
          save_failed_face(username, face, "spoof");
        }
        return AuthResult::Retry;
      }
    } catch (const std::exception& e) {
      spdlog::error("FaceAuth: Anti-spoofing model failed: {}, skipping check", e.what());
    }
  }

  // Match against all enrolled faces — succeed if any match
  for (const auto& facePath : enrolledFaces) {
    ImageRGB preparedFace = image_load(facePath);
    if (preparedFace.empty())
      continue;
    MatchResult match = faceReg->match(preparedFace, face);
    if (match.similar) {
      return AuthResult::Success;
    }
  }

  // No match found
  if (config.debug) {
    save_failed_face(username, face, "not_similar");
  }

  return AuthResult::Retry;
}

}  // namespace biopass
