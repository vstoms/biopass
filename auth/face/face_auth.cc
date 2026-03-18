#include "face_auth.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <unistd.h>

#include <openpnp-capture.h>

#include "auth_config.h"
#include "face_as.h"
#include "face_detection.h"
#include "face_recognition.h"
#include "image_utils.h"

namespace biopass {

std::string get_timestamp_string() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return std::to_string(ms);
}

void save_failed_face(const std::string &username, const ImageRGB &face,
                      const std::string &reason) {
  biopass::setup_config(username);
  std::string failedFacePath =
      biopass::debug_path(username) + "/" + reason + "." + get_timestamp_string() + ".bmp";
  if (!image_save_bmp(failedFacePath, face)) {
    std::cerr << "FaceAuth: Could not save failed face to " << failedFacePath << std::endl;
  }
}

bool FaceAuth::is_available() const {
  CapContext ctx = Cap_createContext();
  if (!ctx) return false;
  uint32_t count = Cap_getDeviceCount(ctx);
  Cap_releaseContext(ctx);
  return count > 0;
}

static ImageRGB capture_frame_from_camera() {
  CapContext ctx = Cap_createContext();
  if (!ctx) return {};

  uint32_t count = Cap_getDeviceCount(ctx);
  if (count == 0) {
    Cap_releaseContext(ctx);
    return {};
  }

  CapFormatInfo fmt;
  Cap_getFormatInfo(ctx, 0, 0, &fmt);
  CapStream stream = Cap_openStream(ctx, 0, 0);
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

AuthResult FaceAuth::authenticate(const std::string &username, const AuthConfig &config,
                                  std::atomic<bool> *cancel_signal) {
  if (!this->is_available()) {
    std::cerr << "FaceAuth: Could not open camera" << std::endl;
    return AuthResult::Unavailable;
  }

  std::vector<std::string> enrolledFaces = biopass::list_user_faces(username);
  if (enrolledFaces.empty()) {
    std::cerr << "FaceAuth: No face enrolled for user " << username << ", skipping" << std::endl;
    return AuthResult::Unavailable;
  }

  std::string recogModelPath = face_config_.recognition.model;
  std::string detectModelPath = face_config_.detection.model;
  if (!std::ifstream(recogModelPath).good() || !std::ifstream(detectModelPath).good()) {
    std::cerr << "FaceAuth: Model files not found for user " << username << ", skipping"
              << std::endl;
    return AuthResult::Unavailable;
  }
  std::unique_ptr<FaceRecognition> faceReg;
  std::unique_ptr<FaceDetection> faceDetector;
  try {
    faceDetector = std::make_unique<FaceDetection>(detectModelPath);
  } catch (const std::exception &e) {
    std::string msg = e.what();
    size_t first_line = msg.find('\n');
    if (first_line != std::string::npos)
      msg = msg.substr(0, first_line);
    std::cerr << "FaceAuth: Failed to load detection model: " << msg << ", skipping" << std::endl;
    return AuthResult::Unavailable;
  }
  try {
    faceReg = std::make_unique<FaceRecognition>(recogModelPath);
  } catch (const std::exception &e) {
    std::string msg = e.what();
    size_t first_line = msg.find('\n');
    if (first_line != std::string::npos)
      msg = msg.substr(0, first_line);
    std::cerr << "FaceAuth: Failed to load recognition model: " << msg << ", skipping" << std::endl;
    return AuthResult::Unavailable;
  }

  if (cancel_signal && cancel_signal->load()) {
    return AuthResult::Failure;
  }

  ImageRGB loginFace = capture_frame_from_camera();
  if (loginFace.empty()) {
    std::cerr << "FaceAuth: Could not read frame" << std::endl;
    return AuthResult::Retry;
  }

  std::vector<Detection> detectedImages = faceDetector->inference(loginFace);
  if (detectedImages.empty()) {
    std::cerr << "FaceAuth: No face detected" << std::endl;
    return AuthResult::Retry;
  }

  ImageRGB face = detectedImages[0].image;

  if (config.anti_spoof) {
    std::string asModelPath = face_config_.anti_spoofing.model;
    try {
      FaceAntiSpoofing faceAs(asModelPath);
      SpoofResult spoofCheck = faceAs.inference(face);
      if (spoofCheck.spoof) {
        std::cerr << "FaceAuth: Spoof detected, score: " << spoofCheck.score << std::endl;
        if (config.debug) {
          save_failed_face(username, face, "spoof");
        }
        return AuthResult::Retry;
      }
    } catch (const std::exception &e) {
      std::cerr << "FaceAuth: Anti-spoofing model failed: " << e.what() << ", skipping check"
                << std::endl;
    }
  }

  // Match against all enrolled faces — succeed if any match
  for (const auto &facePath : enrolledFaces) {
    ImageRGB preparedFace = image_load_bmp(facePath);
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
