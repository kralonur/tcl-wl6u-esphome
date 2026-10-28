#pragma once

#include <Arduino.h>
#include <cstring>
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

// BENCH ONLY. Disconnect the AC. This sends test bytes, not AC commands.
namespace wl6u_loopback {

static uint16_t sequence = 0;
static bool active = false;
static bool alternate = false;
static uint8_t payload[12];

inline void release_pins() {
  for (uint8_t pin : {11, 12, 13, 14})
    pinMode(pin, INPUT);
}

inline void start() {
  const uint8_t tx = alternate ? 14 : 11;
  const uint8_t rx = alternate ? 13 : 12;
  alternate = !alternate;
  Serial0.end();
  release_pins();
  Serial0.begin(9600, SERIAL_8E1, rx, tx);
  if (!Serial0) {
    ESP_LOGE("loopback", "Could not start UART0 TX=PA%d RX=PA%d", tx, rx);
    release_pins();
    return;
  }
  ++sequence;
  const uint8_t next[] = {'W', 'L', '6', 'U', tx, rx,
                          uint8_t(sequence), uint8_t(sequence >> 8), 0x00, 0x55, 0xAA, 0xFF};
  std::memcpy(payload, next, sizeof(payload));
  active = true;
  ESP_LOGD("loopback", "TX PA%d / RX PA%d: %s", tx, rx,
           esphome::format_hex_pretty(payload, sizeof(payload)).c_str());
  Serial0.write(payload, sizeof(payload));
  Serial0.flush();
}

// Called by an ESPHome automation 100 ms after start(), without a blocking delay.
inline void finish() {
  if (!active)
    return;
  uint8_t received[64];
  size_t count = 0;
  while (Serial0.available() && count < sizeof(received))
    received[count++] = Serial0.read();
  const bool extra = Serial0.available() > 0;
  const bool match = !extra && count == sizeof(payload) &&
                     std::memcmp(received, payload, sizeof(payload)) == 0;
  Serial0.end();
  release_pins();
  active = false;
  ESP_LOGD("loopback", "RX: %s", esphome::format_hex_pretty(received, count).c_str());
  ESP_LOGI("loopback", "TX=PA%d RX=PA%d: %s%s", payload[4], payload[5],
           match ? "EXACT ECHO" : "NO EXACT ECHO", extra ? " (RX truncated)" : "");
}

}  // namespace wl6u_loopback
