#include "HomeControlHttp.h"

#if defined(CROSSINK_APP_CAP_HOME_CONTROL) && CROSSINK_APP_CAP_HOME_CONTROL

#include <Arduino.h>
#include <Logging.h>

#include <cstring>

#ifdef SIMULATOR
#include "HomeControlFixtures.h"
#endif

namespace homecontrol {

namespace {
// Same floors as KOReaderSyncClient: wolfSSL needs contiguous internal RAM for
// the handshake even when the response body lands in PSRAM.
constexpr uint32_t MIN_FREE_HEAP_FOR_TLS = 35000;
constexpr uint32_t MIN_MAX_ALLOC_HEAP_FOR_TLS = 20000;
}  // namespace

const char* HttpResponse::text() {
  if (buf == nullptr || cap == 0) return "";
  const size_t end = len < cap ? len : cap - 1;
  buf[end] = '\0';
  return reinterpret_cast<const char*>(buf);
}

bool insufficientHeapForTls() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_TLS || maxAllocHeap < MIN_MAX_ALLOC_HEAP_FOR_TLS) {
    LOG_ERR("HC", "Insufficient heap for TLS: %u free (need %u), %u max alloc (need %u)", freeHeap,
            MIN_FREE_HEAP_FOR_TLS, maxAllocHeap, MIN_MAX_ALLOC_HEAP_FOR_TLS);
    return true;
  }
  return false;
}

HttpSession::~HttpSession() {
#ifndef SIMULATOR
  http.end();
#endif
}

#ifdef SIMULATOR

int HttpSession::request(const HttpRequest& req, HttpResponse& out) {
  out.len = 0;
  out.truncated = false;
  out.status = fixtures::respond(req, out);
  LOG_DBG("HC", "[sim] %s %s -> %d (%u bytes)", req.method, req.url, out.status, static_cast<unsigned>(out.len));
  return out.status;
}

#else

int HttpSession::request(const HttpRequest& req, HttpResponse& out) {
  out.len = 0;
  out.truncated = false;
  if (out.buf == nullptr || out.cap < 2) {
    LOG_ERR("HC", "No response buffer for %s %s", req.method, req.url);
    return kStatusTransport;
  }
  if (insufficientHeapForTls()) {
    out.status = kStatusLowMemory;
    return out.status;
  }

  http.setInsecure();  // Hue bridges use a self-signed certificate; tado is pinned by hostname only
  http.setTimeout(req.timeoutMs);
  if (!http.begin(req.url)) {
    LOG_ERR("HC", "Bad URL: %s", req.url);
    out.status = kStatusTransport;
    return out.status;
  }
  if (req.contentType != nullptr) http.addHeader("Content-Type", req.contentType);
  if (req.headerName != nullptr && req.headerValue != nullptr) http.addHeader(req.headerName, req.headerValue);
  http.addHeader("Accept", "application/json");

  // One std::function per request, not per byte: it is the SDK's callback
  // type and lets the body stream straight into the caller's (PSRAM) buffer
  // instead of growing the client's internal std::string.
  const auto onData = [&out](const uint8_t* data, size_t len) {
    const size_t room = out.cap - 1 - out.len;  // keep one byte for the NUL in text()
    const size_t take = len < room ? len : room;
    if (take > 0) {
      std::memcpy(out.buf + out.len, data, take);
      out.len += take;
    }
    if (take < len) {
      out.truncated = true;
      return false;  // stop streaming; the body is unusable anyway
    }
    return true;
  };

  const auto* payload = reinterpret_cast<const uint8_t*>(req.body);
  const int status = http.sendRequest(req.method, payload, req.body != nullptr ? req.bodyLen : 0, onData);
  LOG_DBG("HC", "%s %s -> %d (%u bytes%s)", req.method, req.url, status, static_cast<unsigned>(out.len),
          out.truncated ? ", truncated" : "");

  if (status < 0) {
    LOG_ERR("HC", "Transport failure for %s %s", req.method, req.url);
    http.end();  // drop the socket so the next call reconnects cleanly
    out.status = kStatusTransport;
    return out.status;
  }
  if (out.truncated) {
    LOG_ERR("HC", "Response for %s exceeded %u bytes", req.url, static_cast<unsigned>(out.cap));
    http.end();
    out.status = kStatusBodyTooLarge;
    return out.status;
  }
  out.status = status;
  return status;
}

#endif  // SIMULATOR

}  // namespace homecontrol

#endif  // CROSSINK_APP_CAP_HOME_CONTROL
