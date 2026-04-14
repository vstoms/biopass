#pragma once

#include <optional>
#include <string>

#include "image_utils.h"

namespace biopass {

enum class CameraCaptureFormat {
  Default,
  V4L2Grey,
};

struct CameraCaptureOptions {
  int warmup_frames = 5;
  int warmup_timeout_ms = 20000;
  int capture_timeout_ms = 10000;
  int poll_interval_ms = 10;
};

bool is_camera_available(const std::optional<std::string>& device_path);
ImageRGB capture_by_camera(
    const std::optional<std::string>& device_path,
    const CameraCaptureOptions& options = CameraCaptureOptions{},
    CameraCaptureFormat format = CameraCaptureFormat::Default);

}  // namespace biopass
