#pragma once
// TEMPORARY latency instrumentation (input -> render -> panel). Compiled in only
// with -DCROSSINK_LATENCY_TRACE=1 (env x4-pro-latency). Mark ids are shared with
// the SDK via FreeInkLatencyTrace.h; this file owns the event buffer and the dump.
//
// The SDK header is only reachable on hardware envs (FreeInkDisplay lib), so it
// is included only when tracing is on; the simulator sees pure no-ops.
#if defined(CROSSINK_LATENCY_TRACE) && CROSSINK_LATENCY_TRACE
#include <FreeInkLatencyTrace.h>
#define LATENCY_MARK(id) FREEINK_LAT_MARK(::freeink::lat::id)
#include <Arduino.h>
#include <BoardConfig.h>
// Timestamped one-line event, printed immediately (loop task only). Goes to the
// raw USB CDC port: `Serial` is Logging.h's deprecated proxy, which has no body
// on hardware, and LatencyTrace.cpp's dump uses the raw port too.
#define LATENCY_LOG(fmt, ...)                                                                   \
  BoardConfig::serialTransport().printf("LAT: @%lu " fmt "\n", static_cast<unsigned long>(millis()), \
                                        ##__VA_ARGS__)
namespace LatencyTrace {
// Prints the marks recorded since the last page turn (or render start) over
// Serial and clears them. Called by the render task once a frame is complete.
void dump();
}  // namespace LatencyTrace
#else
#define LATENCY_MARK(id) \
  do {                   \
  } while (0)
#define LATENCY_LOG(fmt, ...) \
  do {                        \
  } while (0)
namespace LatencyTrace {
inline void dump() {}
}  // namespace LatencyTrace
#endif
