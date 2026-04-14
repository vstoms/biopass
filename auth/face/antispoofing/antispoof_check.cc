#include "antispoof_check.h"

#include <fstream>
#include <future>
#include <vector>

#include <spdlog/spdlog.h>

#include "debug_image_io.h"
#include "face_as.h"
#include "ir_camera_as.h"

namespace biopass {

namespace {

struct AntiSpoofTask {
  std::string name;
  std::future<bool> future;
};

AntiSpoofTask make_task(const std::string& name, std::future<bool> future) {
  AntiSpoofTask task;
  task.name = name;
  task.future = std::move(future);
  return task;
}

bool run_ai_anti_spoof_check(const FaceMethodConfig& face_config, const std::string& username,
                             const ImageRGB& face, const AuthConfig& config) {
  const std::string model_path = face_config.anti_spoofing.model.path;
  if (model_path.empty() || !std::ifstream(model_path).good()) {
    spdlog::error("FaceAuth: Anti-spoofing model file not found: {}", model_path);
    return false;
  }

  try {
    FaceAntiSpoofing face_as(model_path, 128, face_config.anti_spoofing.model.threshold);
    const SpoofResult spoof_check = face_as.inference(face);
    if (spoof_check.spoof) {
      spdlog::warn("FaceAuth: AI anti-spoofing detected spoof, score: {}", spoof_check.score);
      if (config.debug) {
        save_failed_face(username, face, "spoof");
      }
      return false;
    }

    spdlog::debug("FaceAuth: AI anti-spoofing check passed");
    return true;
  } catch (const std::exception& e) {
    spdlog::error("FaceAuth: AI anti-spoofing check failed: {}", e.what());
    return false;
  }
}

}  // namespace

bool run_anti_spoof_checks(const FaceMethodConfig& face_config, const std::string& username,
                           const ImageRGB& face, const AuthConfig& config) {
  const bool ai_enabled = face_config.anti_spoofing.enable;
  const bool ir_enabled = !face_config.anti_spoofing.ir_camera.empty();

  if (!ai_enabled && !ir_enabled) {
    spdlog::debug("FaceAuth: Anti-spoofing methods are disabled, skipping checks");
    return true;
  }

  spdlog::debug("FaceAuth: Anti-spoofing started (ai_enabled={}, ir_enabled={}, ir_camera='{}')",
                ai_enabled, ir_enabled, face_config.anti_spoofing.ir_camera);

  std::vector<AntiSpoofTask> tasks;

  if (ai_enabled) {
    const auto face_config_copy = face_config;
    const auto username_copy = username;
    const auto face_copy = face;
    const auto config_copy = config;
    tasks.push_back(make_task(
        "AI", std::async(std::launch::async,
                         [face_config_copy, username_copy, face_copy, config_copy]() {
                           return run_ai_anti_spoof_check(face_config_copy, username_copy, face_copy,
                                                          config_copy);
                         })));
  }

  if (ir_enabled) {
    const auto ir_camera_path = face_config.anti_spoofing.ir_camera;
    const auto detection_model = face_config.detection.model;
    const auto detection_threshold = face_config.detection.threshold;
    const auto username_copy = username;
    const auto debug_enabled = config.debug;
    tasks.push_back(make_task(
        "IR", std::async(std::launch::async,
                         [ir_camera_path, detection_model, detection_threshold, username_copy,
                          debug_enabled]() {
                           return run_ir_camera_anti_spoof(ir_camera_path, detection_model,
                                                           detection_threshold, username_copy,
                                                           debug_enabled);
        })));
  }

  bool any_success = false;
  for (auto& task : tasks) {
    bool ok = false;
    try {
      ok = task.future.get();
    } catch (const std::exception& e) {
      spdlog::error("FaceAuth: {} anti-spoofing task failed: {}", task.name, e.what());
      ok = false;
    }
    if (ok) {
      spdlog::debug("FaceAuth: {} anti-spoofing method passed", task.name);
      any_success = true;
    } else {
      spdlog::debug("FaceAuth: {} anti-spoofing method failed", task.name);
    }
  }

  if (!any_success) {
    spdlog::error("FaceAuth: Anti-spoofing failed (all enabled methods failed)");
  }
  return any_success;
}

}  // namespace biopass
