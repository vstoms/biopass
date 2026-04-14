#include <spdlog/spdlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>

#include <CLI/CLI.hpp>
#include <memory>

#include "auth_config.h"
#include "auth_manager.h"
#include "face_auth.h"
#include "fingerprint_auth.h"
#include "image_utils.h"
#include "voice_auth.h"

#include "detection/face_detection.h"

int handle_crop_face(const std::string& inputPath, const std::string& outputPath,
                     const std::string& modelPath) {
  ImageRGB image = image_load(inputPath);
  if (image.empty()) {
    spdlog::error("Could not read input image: {}", inputPath);
    return 1;
  }

  std::unique_ptr<FaceDetection> faceDetector;
  try {
    faceDetector = std::make_unique<FaceDetection>(modelPath);
  } catch (const std::exception& e) {
    spdlog::error("Failed to load detection model: {}", e.what());
    return 1;
  }

  std::vector<Detection> detectedFaces = faceDetector->inference(image);
  if (detectedFaces.empty()) {
    spdlog::error("No face detected in the image");
    return 2;  // Special exit code for "no face detected"
  }

  ImageRGB faceCrop = detectedFaces[0].image;
  if (!image_save(outputPath, faceCrop)) {
    spdlog::error("Could not save cropped image to: {}", outputPath);
    return 1;
  }

  spdlog::info("Successfully cropped face and saved to: {}", outputPath);
  return 0;
}

int handle_authenticate(const std::string& username) {
  const char* pUsername = username.c_str();

  // Load configuration from file
  if (!biopass::config_exists(pUsername)) {
    // User has not configured biopass — skip this module transparently
    return 2;  // PAM_IGNORE
  }
  biopass::BiopassConfig config = biopass::load_config(pUsername);

  if (config.debug) {
    spdlog::set_level(spdlog::level::debug);
  } else {
    spdlog::set_level(spdlog::level::info);
  }

  // Create and configure AuthManager
  biopass::AuthManager manager;
  manager.set_mode(config.mode);
  manager.set_config(config.auth);

  // Add requested authentication methods
  int methods_count = 0;
  for (const auto& method_name : config.methods) {
    if (method_name == "face") {
      manager.add_method(std::make_unique<biopass::FaceAuth>(config.methods_config.face));
      methods_count++;
    } else if (method_name == "voice") {
      manager.add_method(std::make_unique<biopass::VoiceAuth>(config.methods_config.voice));
      methods_count++;
    } else if (method_name == "fingerprint") {
      manager.add_method(
          std::make_unique<biopass::FingerprintAuth>(config.methods_config.fingerprint));
      methods_count++;
    }
  }

  // If no methods are enabled, ignore this module and let PAM jump to the next one
  if (methods_count == 0) {
    return 2;  // PAM_IGNORE
  }

  // Authenticate
  int retval = manager.authenticate(pUsername);

  if (retval == 0 /* PAM_SUCCESS is usually 0 */) {
    return 0;  // PAM_SUCCESS
  } else {
    return 1;  // PAM_AUTH_ERR
  }
}

int handle_migrate_config(const std::string& username) {
  if (getpwnam(username.c_str()) == nullptr) {
    spdlog::error("User '{}' not found", username);
    return 1;
  }

  std::string error;
  if (!biopass::migrate_config_schema(username, &error)) {
    spdlog::error("Failed to migrate config schema: {}", error);
    return 1;
  }

  spdlog::info("Biopass: Config migration completed for user '{}'", username);
  return 0;
}

int main(int argc, char** argv) {
  CLI::App app{"Biopass Helper Tool"};
  app.require_subcommand(1, 1);

  auto crop_cmd = app.add_subcommand("crop-face", "Crop a face from an image");
  std::string inputPath, outputPath, modelPath;
  crop_cmd->add_option("--input,-i", inputPath, "Input image path")->required();
  crop_cmd->add_option("--output,-o", outputPath, "Output image path")->required();
  crop_cmd->add_option("--model,-m", modelPath, "Detection model path")->required();

  std::string username;
  auto auth_cmd = app.add_subcommand("auth", "Authenticate a user with Biopass");
  auth_cmd->add_option("--username,-u", username, "Username for authentication")->required();

  std::string migrate_username;
  auto migrate_cmd =
      app.add_subcommand("migrate", "Migration tool for applying schema changes (run once after updates)");
  migrate_cmd->add_option("--username,-u", migrate_username, "Username to migrate")->required();
  migrate_cmd->group("");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e);
  }

  if (app.got_subcommand(crop_cmd)) {
    return handle_crop_face(inputPath, outputPath, modelPath);
  }

  if (app.got_subcommand(auth_cmd)) {
    if (username.empty()) {
      spdlog::info("{}", app.help());
      return 2;  // PAM_IGNORE logic / error
    }
    return handle_authenticate(username);
  }

  if (app.got_subcommand(migrate_cmd)) {
    if (migrate_username.empty()) {
      spdlog::info("{}", app.help());
      return 1;
    }
    return handle_migrate_config(migrate_username);
  }

  spdlog::error("No valid subcommand provided");
  return 1;
}
