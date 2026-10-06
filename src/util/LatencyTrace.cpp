#include "LatencyTrace.h"

#if defined(CROSSINK_LATENCY_TRACE) && CROSSINK_LATENCY_TRACE

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include <cstddef>

namespace {

constexpr size_t kMaxEvents = 128;

struct Event {
  uint8_t id;
  int64_t us;
};

// Static storage: the trace must not perturb heap behaviour on the hot path.
Event s_events[kMaxEvents];
size_t s_count = 0;
int64_t s_lastInputEdgeUs = -1;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

const char* const kNames[freeink::lat::Count] = {
    "input_edge",    "page_turn",     "render_start", "display_call", "driver_start", "waveform_wait",
    "busy_released", "driver_return", "render_done",  "busy_wait",    "busy_done",
};

void append(uint8_t id, int64_t us) {
  if (s_count < kMaxEvents) {
    s_events[s_count].id = id;
    s_events[s_count].us = us;
    ++s_count;
  }
}

}  // namespace

// Called from the loop task (ids 0-1) and the render task (2-8). Both run on
// core 1, so the critical section is short and never contended for long.
extern "C" void freeink_latency_mark(uint8_t id) {
  const int64_t now = esp_timer_get_time();
  taskENTER_CRITICAL(&s_mux);
  switch (id) {
    case freeink::lat::InputEdge:
      // Press and release both land here; only the most recent edge before the
      // page turn matters, so remember it instead of appending.
      s_lastInputEdgeUs = now;
      break;
    case freeink::lat::PageTurn:
      s_count = 0;
      if (s_lastInputEdgeUs >= 0) append(freeink::lat::InputEdge, s_lastInputEdgeUs);
      append(id, now);
      break;
    case freeink::lat::RenderStart:
      // A render without a page turn (menus, status refresh): still anchor on
      // the last input edge so the trace has a t0.
      if (s_count == 0 && s_lastInputEdgeUs >= 0) append(freeink::lat::InputEdge, s_lastInputEdgeUs);
      append(id, now);
      break;
    default:
      append(id, now);
      break;
  }
  taskEXIT_CRITICAL(&s_mux);
}

void LatencyTrace::dump() {
  Event copy[kMaxEvents];
  size_t count;
  taskENTER_CRITICAL(&s_mux);
  count = s_count;
  for (size_t i = 0; i < count; ++i) copy[i] = s_events[i];
  s_count = 0;
  taskEXIT_CRITICAL(&s_mux);
  if (count == 0) return;

  const int64_t t0 = copy[0].us;
  int64_t prev = t0;
  Serial.printf("LAT: ---- trace (%u marks, t0 = %s) ----\n", static_cast<unsigned>(count),
                copy[0].id < freeink::lat::Count ? kNames[copy[0].id] : "?");
  for (size_t i = 0; i < count; ++i) {
    const Event& e = copy[i];
    const char* name = e.id < freeink::lat::Count ? kNames[e.id] : "?";
    Serial.printf("LAT: %9.2f ms  (+%8.2f)  %s\n", (e.us - t0) / 1000.0, (e.us - prev) / 1000.0, name);
    prev = e.us;
  }
  Serial.printf("LAT: total %.2f ms\n", (copy[count - 1].us - t0) / 1000.0);
}

#endif  // CROSSINK_LATENCY_TRACE
