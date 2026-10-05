#pragma once

#if CROSSINK_APP_CAP_DASHBOARD

#include "TrmnlProtocol.h"

// Transport for the TRMNL BYOS device API. Wi-Fi must already be connected.
// The caller (TrmnlSource) owns the settings: it passes the server URL and
// key in and persists a provisioned key itself.
namespace trmnl {

// Negative results; non-negative values are HTTP status codes.
constexpr int kStatusTransport = -1;
constexpr int kStatusBodyTooLarge = -2;
constexpr int kStatusLowMemory = -3;
constexpr int kStatusBadUrl = -4;

// TLS needs contiguous internal RAM; http:// always passes.
bool heapOkForUrl(const char* url);

// GET {baseUrl}/api/setup. Returns the HTTP status; out.ok when a key parsed.
int requestSetup(const char* baseUrl, SetupResult& out);

// GET {baseUrl}/api/display with Access-Token. Returns the HTTP status;
// out.ok when the body parsed.
int requestDisplay(const char* baseUrl, const char* apiKey, DisplayResult& out);

}  // namespace trmnl

#endif  // CROSSINK_APP_CAP_DASHBOARD
