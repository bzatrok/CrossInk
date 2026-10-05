#pragma once

#include <cstddef>

// Pure parsing for the TRMNL BYOS device API (/api/setup, /api/display).
// No Arduino, no HAL: the native tests in test/trmnl_protocol link this file
// directly. Fixed-size outputs keep the timer-wake path free of std::string.
namespace trmnl {

struct SetupResult {
  bool ok;
  char apiKey[64];
  char friendlyId[16];
};

struct DisplayResult {
  bool ok;
  int status;  // the body's "status" field; 0 when absent
  char imageUrl[256];
  char filename[96];
};

// ok only when the body is a JSON object with a non-empty api_key that fits.
bool parseSetup(const char* json, size_t len, SetupResult& out);

// ok when the body is a JSON object and every string field fits its buffer.
// A missing image_url leaves imageUrl empty. refresh_rate is deliberately
// ignored: the local interval setting wins.
bool parseDisplay(const char* json, size_t len, DisplayResult& out);

// True when the server says nothing changed: status 202, or the same
// non-empty filename as the image already shown.
bool isNoChange(const DisplayResult& r, const char* lastFilename);

// Absolute image URLs pass through. Relative ones ("/x" or "x") join to the
// scheme://host[:port] origin of base. False when base has no origin, the
// image URL is empty, or the result does not fit in cap.
bool resolveImageUrl(const char* base, const char* imageUrl, char* out, size_t cap);

}  // namespace trmnl
