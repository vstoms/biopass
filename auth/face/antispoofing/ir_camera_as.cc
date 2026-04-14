#include "ir_camera_as.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

#include "camera_capture.h"
#include "debug_image_io.h"
#include "face_detection.h"

namespace biopass {

bool run_ir_camera_anti_spoof(const std::string& device_path,
                              const std::string& detection_model_path, float detection_threshold,
                              const std::string& username, bool debug) {
  if (device_path.empty()) {
    return false;
  }

  if (!std::ifstream(detection_model_path).good()) {
    spdlog::error("FaceAuth: Detection model file not found: {}", detection_model_path);
    return false;
  }

  CameraCaptureOptions capture_options;
  capture_options.warmup_frames = 5;
  capture_options.warmup_timeout_ms = 1000;
  capture_options.capture_timeout_ms = 3000;
  capture_options.poll_interval_ms = 10;

  ImageRGB frame = capture_by_camera(device_path, capture_options, CameraCaptureFormat::V4L2Grey);
  if (frame.empty()) {
    spdlog::error("FaceAuth: IR camera failed to capture frame from {}", device_path);
    return false;
  }

  if (debug)
    save_failed_face(username, frame, "ir_capture_raw");

  try {
    FaceDetection detector(detection_model_path, 640, {"face"}, detection_threshold);
    if (!detector.inference(frame).empty()) {
      spdlog::error("FaceAuth: IR camera check failed, no face detected in captured frame");
      if (debug) {
        save_failed_face(username, frame, "ir_no_face");
      }
      return false;
    }

    return true;
  } catch (const std::exception& e) {
    spdlog::error("FaceAuth: IR anti-spoofing check failed: {}", e.what());
    return false;
  }
}
}  // namespace biopass
