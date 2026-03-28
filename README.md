# 🌾 AgriTech Rover

An agricultural rover system with RC control, real-time obstacle avoidance, sensor telemetry, AI plant health monitoring, and a React dashboard.

---

## Architecture

```
[ESP32-C Controller]  ──ESP-NOW──►  [ESP32-D Rover Hub]  ──UART──►  [Arduino Mega]
   Joystick + Buttons                FreeRTOS dual-core              Motors + Sensors
   Touch TFT UI                      Failsafe watchdog               6x Ultrasonic
                                          │
                                          └──WiFi──►  [Raspberry Pi 3B]
                                                       Node.js + React
                                                       AI inference
                                                            ▲
                                     [ESP32-CAM]  ──MJPEG──┘
```

**Control path is RC-only.** The Pi is never involved in motor control.

See [`docs/architecture.md`](docs/architecture.md) for full protocol specs, task layout, and failsafe design.

---

## Repository Structure

```
Agritech_Rover/
├── firmware/
│   ├── mega_controller/         Arduino Mega — motors + sensors
│   │   ├── mega_controller.ino
│   │   └── build/               Pre-built .hex/.bin for flashing
│   ├── esp32_c_controller/      ESP32-C — joystick RC transmitter
│   │   └── esp32_c_controller.ino
│   ├── esp32_d_rover/           ESP32-D — rover hub (FreeRTOS)
│   │   └── esp32_d_rover.ino
│   └── esp32_cam/               ESP32-CAM — MJPEG stream
│       └── esp32_cam.ino
├── agri_rover_pi/
│   ├── backend/                 Node.js API + WebSocket
│   │   ├── server.js
│   │   ├── database.js          SQLite (sensor_data, rover_log)
│   │   ├── routes/              /api/sensors, /api/rover, /api/health
│   │   └── services/            serial.js, edge_impulse.js
│   └── frontend/                React dashboard
│       └── src/components/      SensorPanel, ControlPanel, StreamPanel
├── docs/
│   └── architecture.md
└── README.md
```

---

## Quick Start

### 1. Arduino Mega
Flash pre-built: `firmware/mega_controller/build/mega_controller_ino.hex`
```bash
avrdude -p m2560 -c wiring -P /dev/ttyUSB0 -b 115200 \
  -U flash:w:firmware/mega_controller/build/mega_controller_ino.hex:i
```

Or compile `firmware/mega_controller/mega_controller.ino` in Arduino IDE.

**Libraries:** `DHT sensor library`, `DallasTemperature`, `OneWire`

### 2. ESP32-D (flash first — you need its MAC)
Open `firmware/esp32_d_rover/esp32_d_rover.ino`, set `WIFI_SSID`/`WIFI_PASS`/`PI_HOST`, flash.  
Note the MAC printed on Serial: `[ESP32-D] MAC: AA:BB:CC:DD:EE:FF`

**Libraries:** `ArduinoWebsockets`, `ArduinoJson`, `Adafruit_GFX`, `Adafruit_ILI9341`

### 3. ESP32-C
Open `firmware/esp32_c_controller/esp32_c_controller.ino`, paste ESP32-D MAC into `ROVER_MAC[]`, flash.

**Libraries:** `Adafruit_GFX`, `Adafruit_ILI9341`, `XPT2046_Touchscreen`

### 4. ESP32-CAM
Open `firmware/esp32_cam/esp32_cam.ino`, set WiFi credentials, flash.  
Board: `AI Thinker ESP32-CAM`, PSRAM: `Enabled`

### 5. Raspberry Pi
```bash
cd agri_rover_pi/backend
npm install
npm start

cd ../frontend
npm install
npm start
```

---

## Sensor Telemetry Format

Emitted by Mega over USB Serial every 2 s:
```
M:<soil_raw>|W:<0/1>|AT:<airTemp>|ST:<soilTemp>|H:<humidity>|O:<obstacle>
```

---

## Hardware

| Component | Qty | Notes |
|---|---|---|
| Arduino Mega 2560 | 1 | Motor + sensor controller |
| ESP32 DevKit | 2 | C (controller) + D (rover hub) |
| ESP32-CAM AI Thinker | 1 | MJPEG stream |
| Raspberry Pi 3B | 1 | Backend + AI |
| BTS7960 H-Bridge | 2 | Left/right drive |
| DS18B20 | 1 | Soil temperature |
| DHT11 | 1 | Air temperature + humidity |
| HC-SR04 | 6 | Obstacle detection |
| Analog moisture sensor | 1 | Soil moisture |
| ILI9341 2.8" TFT + XPT2046 | 1 | Controller touch display |
| ILI9341 2.4" TFT | 1 | Rover hub status display |
| Water pump + relay | 2 | Irrigation |
