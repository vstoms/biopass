#pragma once

#include <security/_pam_types.h>

#include <string>

namespace biopass {
enum AuthResult { Success, Failure, Retry, Unavailable };

struct AuthConfig {
  bool debug = false;
  bool anti_spoof = false;
};

struct IAuthMethod {
  virtual ~IAuthMethod() = default;
  virtual std::string name() const = 0;
  virtual bool is_available() const = 0;
  virtual int get_retries() const = 0;
  virtual int get_retry_delay_ms() const = 0;
  virtual AuthResult authenticate(const std::string& username, const AuthConfig& config,
                                  std::atomic<bool>* cancel_signal = nullptr) = 0;
};

struct RetryStrategy {
  RetryStrategy(int max_retries) : max_retries_(max_retries) {}

  bool should_retry(AuthResult result, int attempts) const {
    if (result != AuthResult::Retry) {
      return false;
    }
    return attempts < max_retries_;
  }

 private:
  int max_retries_;
};
}  // namespace biopass
