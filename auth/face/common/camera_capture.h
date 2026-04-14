#pragma once

#include <optional>
#include <string>

#include "image_utils.h"

namespace biopass {

enum class CameraCaptureFormat {
  Default,
  V4L2Grey,
};

bool is_camera_available(const std::optional<std::string>& device_path);
ImageRGB capture_by_camera(
    const std::optional<std::string>& device_path,
    CameraCaptureFormat format = CameraCaptureFormat::Default);
ImageRGB capture_ir_by_camera(const std::string& device_path, int warmup_frames = 5,
                              int capture_timeout_ms = 3000, int poll_interval_ms = 10);

}  // namespace biopass
