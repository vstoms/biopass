#pragma once

#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "auth_method.h"

namespace biopass {

enum class ExecutionMode {
  Sequential,  // Try methods in order, fallback on failure
  Parallel     // Run all methods concurrently, succeed on first success
};

/**
 * Manages multiple authentication methods and their execution.
 */
class AuthManager {
 public:
  AuthManager() = default;
  ~AuthManager() = default;

  // Non-copyable, movable
  AuthManager(const AuthManager &) = delete;
  AuthManager &operator=(const AuthManager &) = delete;
  AuthManager(AuthManager &&) = default;
  AuthManager &operator=(AuthManager &&) = default;

  void add_method(std::unique_ptr<IAuthMethod> method);
  void set_mode(ExecutionMode mode);
  void set_config(const AuthConfig &config);
  int authenticate(const std::string &username);

 private:
  int run_sequential(const std::string &username);
  int run_parallel(const std::string &username);

  std::vector<std::unique_ptr<IAuthMethod>> methods_;
  ExecutionMode mode_ = ExecutionMode::Parallel;
  AuthConfig config_;
};

}  // namespace biopass
