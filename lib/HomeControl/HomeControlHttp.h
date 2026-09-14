#pragma once

// Thin HTTPS transport for Home Control. On firmware it wraps
// freeink::SecureHttpClient (wolfSSL) and keeps the socket alive between
// calls; in the simulator it answers from canned fixtures so the UI flow can be
// exercised without a bridge or a tado account.

#include <cstddef>
#include <cstdint>

#if defined(CROSSINK_APP_CAP_HOME_CONTROL) && CROSSINK_APP_CAP_HOME_CONTROL && !defined(SIMULATOR)
#include <SecureHttpClient.h>
#endif

namespace homecontrol {

constexpr int kStatusLowMemory = -100;    // heap gate refused the TLS handshake
constexpr int kStatusTransport = -1;      // connect/handshake/header failure
constexpr int kStatusBodyTooLarge = -101;  // response did not fit the caller's buffer
constexpr uint32_t kDefaultTimeoutMs = 15000;

struct HttpRequest {
  const char* method = "GET";
  const char* url = nullptr;  // full https://host/path
  const char* contentType = nullptr;
  const char* body = nullptr;
  size_t bodyLen = 0;
  const char* headerName = nullptr;  // one optional extra header (auth)
  const char* headerValue = nullptr;
  uint32_t timeoutMs = kDefaultTimeoutMs;
};

struct HttpResponse {
  int status = 0;
  uint8_t* buf = nullptr;  // caller-owned, typically PSRAM
  size_t cap = 0;
  size_t len = 0;
  bool truncated = false;

  // NUL-terminates the body in place. Requires len < cap, which request() guarantees.
  const char* text();
};

// True when free internal heap is too low to risk a TLS handshake.
bool insufficientHeapForTls();

class HttpSession {
 public:
  HttpSession() = default;
  ~HttpSession();
  HttpSession(const HttpSession&) = delete;
  HttpSession& operator=(const HttpSession&) = delete;

  // Runs one request and copies the body into out.buf. Returns the HTTP
  // status, or one of the negative kStatus* codes.
  int request(const HttpRequest& req, HttpResponse& out);

 private:
#if defined(CROSSINK_APP_CAP_HOME_CONTROL) && CROSSINK_APP_CAP_HOME_CONTROL && !defined(SIMULATOR)
  freeink::SecureHttpClient http;
#endif
};

}  // namespace homecontrol
