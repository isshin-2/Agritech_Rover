# AgriTech Rover — System Architecture

## Overview

A distributed agricultural rover system with strict separation between
real-time control and non-real-time monitoring/AI.

---

## Node Roles

| Node | Hardware | Role |
|---|---|---|
| **ESP32-C** | ESP32 DevKit | RC controller — joystick + buttons + touch TFT |
| **ESP32-D** | ESP32 DevKit | Rover hub — ESP-NOW → UART bridge + WiFi gateway |
| **Arduino Mega** | Mega 2560 | Motor execution + sensor telemetry |
| **ESP32-CAM** | AI Thinker | MJPEG video stream |
| **Raspberry Pi 3B** | Pi 3B | Backend, AI inference, OTA, dashboard |

---

## Control Path (Real-Time — INVIOLABLE)

```
ESP32-C ──ESP-NOW──► ESP32-D ──UART(Serial2)──► Arduino Mega ──► BTS7960
```

- **ESP-NOW**: 8-byte packets at ~30 Hz, CRC-16 validated
- **Failsafe**: ESP32-D stops motors if no packet for >300 ms
- **Local safety**: Mega stops if no UART for >500 ms
- **Obstacle hard-stop**: Mega's HC-SR04 sensors always override forward motion
- **Raspberry Pi NEVER sends motor commands**

---

## Telemetry Path (Non-Real-Time)

```
Arduino Mega ──USB Serial──► Raspberry Pi ──WebSocket──► React Dashboard
                                    │
                           ESP32-D ─┘  (pushes control state @ 2 Hz)
```

---

## Vision Path

```
ESP32-CAM ──MJPEG (port 81)──► Raspberry Pi ──► TFLite/Edge Impulse
                                     │
                            Results ──► Dashboard + ESP32-D display
```

AI results are **observational only** — no autonomous motor control.

---

## ESP-NOW Packet Structure

```c
typedef struct __attribute__((packed)) {
    int16_t  throttle;   // -255..+255  forward/back
    int16_t  steering;   // -255..+255  left/right
    uint8_t  buttons;    // Bit0:Pump  Bit1:ESTOP  Bit2:B3  Bit3:B4
    uint8_t  seq;        // Wrapping sequence counter (loss detection)
    uint16_t crc16;      // CRC-16/CCITT over first 6 bytes
} ControlPacket_t;       // 8 bytes total
```

---

## UART Protocol (ESP32-D → Mega, 115200 baud)

| Command | Description |
|---|---|
| `DRIVE L<n> R<n>\n` | Set left/right motor speed, n = -255..255 |
| `PUMP ON\n` | Pump 1 on |
| `PUMP OFF\n` | Pump 1 off |
| `PUMP2 ON/OFF\n` | Pump 2 |
| `ESTOP\n` | Immediate motor stop |
| `PING\n` | Heartbeat (resets Mega watchdog) |

Mega replies: `BLOCKED\n` when obstacle detected.

---

## Telemetry Format (Mega → Pi, USB 9600 baud)

```
M:<moisture>|W:<rain>|AT:<airTemp>|ST:<soilTemp>|H:<humidity>|O:<obstacle>
```

Example: `M:450|W:0|AT:28.5|ST:24.3|H:65|O:0`

---

## ESP32-D FreeRTOS Task Layout

```
Core 0 (PRO_CPU) — Real-Time
├── taskControl   priority MAX-2   ESP-NOW rx → CRC → queue → UART
│                                  + Mega echo parser
│                                  + 200ms PING heartbeat to Mega
│                                  + 300ms failsafe watchdog

Core 1 (APP_CPU) — Non-Real-Time
├── taskDisplay   priority 1       TFT refresh @ 8 Hz (non-blocking)
└── taskWifi      priority 2       WiFi + WebSocket to Pi @ 2 Hz
                                   + Pi → rover pump commands
```

Shared state protected by `stateMutex` (FreeRTOS mutex).
Control queue uses `xQueueOverwrite` — always latest, never stale.

---

## Failsafe Layers

| Layer | Trigger | Action |
|---|---|---|
| ESP32-C | E-STOP button | Sends ESTOP packet every frame |
| ESP32-D | No ESP-NOW for 300 ms | Sends ESTOP to Mega |
| Mega | No UART for 500 ms | Local motor stop |
| Mega | HC-SR04 < 25 cm (forward) | Local motor stop, sends BLOCKED |
| Pi | Obstacle flag in telemetry | Dashboard alert, optional pump-off |

---

## Flashing Order

1. **Mega**: `avrdude` via USB
2. **ESP32-D**: Flash first, note MAC from Serial output
3. **ESP32-C**: Set `ROVER_MAC[]` in firmware to ESP32-D MAC, then flash
4. **ESP32-CAM**: Flash with PSRAM enabled
5. **Pi**: `npm install && npm start` in backend/

---

## OTA Plan

- Pi pulls firmware from GitHub Actions CI artifacts
- ESP32 OTA served by Pi (ArduinoOTA or HTTP OTA)
- Mega: USB only (avrdude via Pi GPIO or direct)

---

## Scalability Roadmap

| Feature | Path |
|---|---|
| Robotic arm | Extra UART channel on ESP32-D → arm controller MCU |
| Semi-autonomy | Pi vision results → ESP32-D → conditional DRIVE commands |
| More sensors | I2C/SPI expansion on Mega or ESP32-D |
| Multi-rover | ESP-NOW channel isolation per rover |
