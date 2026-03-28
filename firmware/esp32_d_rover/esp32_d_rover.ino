/*
 * ============================================================
 *  AgriTech Rover — ESP32-D Rover Hub
 *  Role : Real-time control hub + telemetry gateway
 *  v1.0
 * ============================================================
 *
 *  ARCHITECTURE
 *  ─────────────────────────────────────────────────────────
 *  ESP32-C ──ESP-NOW──► ESP32-D ──UART──► Arduino Mega
 *                           │
 *                           └──WiFi──► Raspberry Pi
 *
 *  DUAL-CORE TASK LAYOUT
 *  ─────────────────────────────────────────────────────────
 *  Core 0 (PRO_CPU) — REAL-TIME — NEVER BLOCKED
 *    taskControl   : ESP-NOW rx → validate → CRC → queue
 *    taskUart      : Dequeue → build command → Serial2 write
 *                    + parse Mega telemetry ← Serial2
 *                    + UART watchdog / failsafe heartbeat
 *
 *  Core 1 (APP_CPU) — NON-REAL-TIME
 *    taskDisplay   : TFT refresh @ 8 Hz (non-blocking)
 *    taskWifi      : WebSocket push to Pi @ 2 Hz
 *                    + receive AI result / pump overrides
 *
 *  QUEUES / SHARED STATE
 *  ─────────────────────────────────────────────────────────
 *    ctrlQueue     : ControlPacket_t — size 2 (discard old)
 *    telemQueue    : TelemetryData_t — size 4
 *    Mutex on shared display state (displayMutex)
 *
 *  ESP-NOW PACKET  (matches ESP32-C exactly)
 *  ─────────────────────────────────────────────────────────
 *    int16_t  throttle    — -255..+255
 *    int16_t  steering    — -255..+255
 *    uint8_t  buttons     — Bit0:Pump Bit1:ESTOP Bit2:B3 Bit3:B4
 *    uint8_t  seq         — wrapping sequence counter
 *    uint16_t crc16       — CRC-16/CCITT over first 6 bytes
 *
 *  UART PROTOCOL (ESP32-D → Arduino Mega, 115200)
 *  ─────────────────────────────────────────────────────────
 *    DRIVE L<n> R<n>\n    — differential drive
 *    PUMP ON|OFF\n
 *    ESTOP\n
 *    PING\n               — watchdog heartbeat (every 200 ms)
 *
 *  TELEMETRY FROM MEGA (raw, 9600 — via Serial2 shared)
 *  Actually Mega sends telemetry to Pi via USB directly.
 *  ESP32-D only taps Mega UART for status echoes (BLOCKED etc).
 *
 *  HARDWARE PIN MAP
 *  ─────────────────────────────────────────────────────────
 *  UART to Mega TX        : GPIO 17  (Serial2)
 *  UART to Mega RX        : GPIO 16  (Serial2)
 *  TFT ST7789/ILI9341 CS  : GPIO 4
 *  TFT DC                 : GPIO 21
 *  TFT RST                : GPIO 22
 *  TFT SCK                : GPIO 18
 *  TFT MOSI               : GPIO 23
 *
 *  CONFIGURATION
 *  ─────────────────────────────────────────────────────────
 *  Set PI_HOST to Pi's static IP (default: 10.48.169.241)
 *  Set PI_WS_PORT to Pi WebSocket port (default: 5000)
 *  Set WIFI_SSID / WIFI_PASS for your network
 * ============================================================
 *
 *  Libraries required:
 *    esp_now.h, WiFi.h (Arduino ESP32 core)
 *    ArduinoWebsockets   (by Gil Maimon)
 *    ArduinoJson
 *    Adafruit_GFX, Adafruit_ILI9341
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

using namespace websockets;

// ─── CONFIGURATION ───────────────────────────────────────────
#define WIFI_SSID       "YOUR_SSID"
#define WIFI_PASS       "YOUR_PASS"
#define PI_HOST         "10.48.169.241"
#define PI_WS_PORT      5000

// ─── TIMING ──────────────────────────────────────────────────
#define CTRL_TIMEOUT_MS      300    // Failsafe: stop if no packet
#define HEARTBEAT_MS         200    // PING to Mega interval
#define DISPLAY_REFRESH_MS   125    // 8 Hz
#define TELEMETRY_PUSH_MS    500    // 2 Hz to Pi
#define WIFI_RETRY_DELAY_MS  5000

// ─── PINS ────────────────────────────────────────────────────
#define TFT_CS    4
#define TFT_DC    21
#define TFT_RST   22
#define MEGA_TX   17   // Serial2 TX → Mega RX (connect to Mega RX3/D15)
#define MEGA_RX   16   // Serial2 RX ← Mega TX (connect to Mega TX3/D14)

// ─── PACKET STRUCTURES ───────────────────────────────────────
typedef struct __attribute__((packed)) {
    int16_t  throttle;
    int16_t  steering;
    uint8_t  buttons;
    uint8_t  seq;
    uint16_t crc16;
} ControlPacket_t;

typedef struct {
    int   soilMoisture;
    float airTemp;
    float soilTemp;
    int   humidity;
    bool  isRaining;
    bool  obstacleDetected;
    bool  pumpOn;
    bool  connected;
    int16_t throttle;
    int16_t steering;
    bool  failsafe;
} RoverState_t;

// ─── FREERTOS HANDLES ────────────────────────────────────────
static QueueHandle_t     ctrlQueue;
static SemaphoreHandle_t stateMutex;

// ─── SHARED STATE (protected by stateMutex) ──────────────────
static volatile RoverState_t gState = {};

// ─── DISPLAY ─────────────────────────────────────────────────
static Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ─── WEBSOCKET ───────────────────────────────────────────────
static WebsocketsClient ws;
static bool             wsConnected = false;

// ─── CRC-16/CCITT ────────────────────────────────────────────
static uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

// ════════════════════════════════════════════════════════════
//  ESP-NOW RECEIVE CALLBACK  (runs in WiFi task context)
//  MUST BE FAST — only validate + enqueue, no processing
// ════════════════════════════════════════════════════════════
static void onReceiveCb(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(ControlPacket_t)) return;

    ControlPacket_t pkt;
    memcpy(&pkt, data, sizeof(pkt));

    // CRC validation
    uint16_t expected = crc16((uint8_t *)&pkt, sizeof(pkt) - 2);
    if (pkt.crc16 != expected) return;  // Silently drop corrupt frames

    // Overwrite queue (keep only latest — discard stale)
    xQueueOverwrite(ctrlQueue, &pkt);
}

// ════════════════════════════════════════════════════════════
//  TASK: CONTROL + UART  (Core 0 — REAL-TIME)
// ════════════════════════════════════════════════════════════
static void taskControl(void *pvParams) {
    ControlPacket_t  pkt;
    uint32_t         lastPktMs    = millis();
    uint32_t         lastPingMs   = millis();
    bool             failsafe     = false;
    bool             pumpOn       = false;
    uint8_t          lastSeq      = 255;

    // Watchdog: reset if task stalls >2s
    esp_task_wdt_add(NULL);

    for (;;) {
        esp_task_wdt_reset();
        uint32_t now = millis();

        // ── RECEIVE PACKET (non-blocking, 5ms tick) ──────────
        if (xQueueReceive(ctrlQueue, &pkt, pdMS_TO_TICKS(5)) == pdTRUE) {
            lastPktMs = now;

            // Log dropped packets (for debugging, no action)
            if (pkt.seq != (uint8_t)(lastSeq + 1) && lastSeq != 255) {
                // Sequence jump — packet lost, proceed with latest anyway
            }
            lastSeq = pkt.seq;

            if (failsafe) {
                failsafe = false;
                Serial2.println(F("PING")); // Revive Mega watchdog
            }

            // Handle E-STOP button
            bool estop = (pkt.buttons & (1 << 1));
            if (estop) {
                Serial2.println(F("ESTOP"));
            } else {
                // Tank-mix: already done in ESP32-C and passed as throttle/steering
                int left  = constrain((int)pkt.throttle + (int)pkt.steering, -255, 255);
                int right = constrain((int)pkt.throttle - (int)pkt.steering, -255, 255);

                char cmd[32];
                snprintf(cmd, sizeof(cmd), "DRIVE L%d R%d", left, right);
                Serial2.println(cmd);
            }

            // Pump toggle
            bool pumpCmd = (pkt.buttons & (1 << 0));
            if (pumpCmd != pumpOn) {
                pumpOn = pumpCmd;
                Serial2.println(pumpOn ? F("PUMP ON") : F("PUMP OFF"));
            }

            // Update shared state
            if (xSemaphoreTake(stateMutex, 0) == pdTRUE) {
                gState.throttle  = pkt.throttle;
                gState.steering  = pkt.steering;
                gState.pumpOn    = pumpOn;
                gState.failsafe  = false;
                gState.connected = true;
                xSemaphoreGive(stateMutex);
            }
        }

        // ── FAILSAFE: no packet for CTRL_TIMEOUT_MS ──────────
        if (now - lastPktMs > CTRL_TIMEOUT_MS) {
            if (!failsafe) {
                Serial2.println(F("ESTOP"));
                failsafe = true;
                if (xSemaphoreTake(stateMutex, 0) == pdTRUE) {
                    gState.failsafe  = true;
                    gState.connected = false;
                    gState.throttle  = 0;
                    gState.steering  = 0;
                    xSemaphoreGive(stateMutex);
                }
            }
        }

        // ── HEARTBEAT to Mega (prevent Mega's watchdog firing) ─
        if (now - lastPingMs >= HEARTBEAT_MS) {
            lastPingMs = now;
            Serial2.println(F("PING"));
        }

        // ── PARSE MEGA ECHOES (BLOCKED alerts etc) ───────────
        while (Serial2.available()) {
            String line = Serial2.readStringUntil('\n');
            line.trim();
            if (line == F("BLOCKED")) {
                if (xSemaphoreTake(stateMutex, 0) == pdTRUE) {
                    gState.obstacleDetected = true;
                    xSemaphoreGive(stateMutex);
                }
            }
        }
    }
}

// ════════════════════════════════════════════════════════════
//  TASK: DISPLAY  (Core 1 — NON-REAL-TIME, 8 Hz max)
// ════════════════════════════════════════════════════════════
static void taskDisplay(void *pvParams) {
    tft.begin();
    tft.setRotation(3);
    tft.fillScreen(ILI9341_BLACK);

    // Draw static header (never redrawn)
    tft.fillRect(0, 0, 320, 28, ILI9341_NAVY);
    tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(2);
    tft.setCursor(60, 6); tft.print("AGRI ROVER HUB");

    TickType_t     lastWake = xTaskGetTickCount();
    RoverState_t   snap;

    for (;;) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(DISPLAY_REFRESH_MS));

        // Snapshot shared state safely
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(&snap, (void*)&gState, sizeof(snap));
            xSemaphoreGive(stateMutex);
        } else {
            continue;
        }

        // ── FAILSAFE BANNER ───────────────────────────────────
        if (snap.failsafe) {
            tft.fillRect(0, 30, 320, 40, ILI9341_RED);
            tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2);
            tft.setCursor(60, 42); tft.print("!! FAILSAFE !!");
        } else {
            tft.fillRect(0, 30, 320, 40, ILI9341_DARKGREEN);
            tft.setTextColor(ILI9341_WHITE); tft.setTextSize(1);
            char buf[32];
            snprintf(buf, sizeof(buf), "T:%+4d  S:%+4d",
                     snap.throttle, snap.steering);
            tft.setCursor(60, 45); tft.print(buf);
        }

        // ── SENSOR ROW 1 ──────────────────────────────────────
        tft.fillRect(0, 75, 320, 55, ILI9341_BLACK);
        tft.setTextSize(1); tft.setTextColor(ILI9341_LIGHTGREY);

        char row[48];
        tft.setCursor(5, 80);
        snprintf(row, sizeof(row), "Soil Moisture: %d%%", snap.soilMoisture);
        tft.print(row);

        tft.setCursor(5, 96);
        snprintf(row, sizeof(row), "Air Temp:  %.1fC  Soil: %.1fC",
                 snap.airTemp, snap.soilTemp);
        tft.print(row);

        tft.setCursor(5, 112);
        snprintf(row, sizeof(row), "Humidity:  %d%%   Rain: %s",
                 snap.humidity, snap.isRaining ? "YES" : "NO");
        tft.print(row);

        // ── STATUS ROW ────────────────────────────────────────
        tft.fillRect(0, 135, 320, 20, ILI9341_BLACK);
        tft.setCursor(5, 138);
        tft.setTextColor(snap.obstacleDetected ? ILI9341_RED : ILI9341_GREEN);
        tft.print(snap.obstacleDetected ? "[OBSTACLE]" : "[CLEAR]");

        tft.setCursor(120, 138);
        tft.setTextColor(snap.pumpOn ? ILI9341_CYAN : ILI9341_DARKGREY);
        tft.print(snap.pumpOn ? "[PUMP ON]" : "[PUMP OFF]");

        tft.setCursor(230, 138);
        tft.setTextColor(wsConnected ? ILI9341_GREEN : ILI9341_ORANGE);
        tft.print(wsConnected ? "[WIFI OK]" : "[NO WIFI]");

        // Reset obstacle flag after display (edge-only)
        if (snap.obstacleDetected) {
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                gState.obstacleDetected = false;
                xSemaphoreGive(stateMutex);
            }
        }
    }
}

// ════════════════════════════════════════════════════════════
//  TASK: WIFI + WEBSOCKET  (Core 1 — NON-REAL-TIME)
// ════════════════════════════════════════════════════════════
static void taskWifi(void *pvParams) {
    // WiFi connect loop
    Serial.printf("[WiFi] Connecting to %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (millis() - wifiStart > 15000) {
            Serial.println(F("[WiFi] Timeout — continuing without WiFi"));
            goto wifi_loop; // Run loop without WiFi
        }
    }
    Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());

    // WebSocket connect
    {
        char wsUrl[64];
        snprintf(wsUrl, sizeof(wsUrl), "ws://%s:%d/", PI_HOST, PI_WS_PORT);
        ws.onMessage([](WebsocketsMessage msg) {
            // Handle Pi → rover commands (pump override, AI result)
            StaticJsonDocument<256> doc;
            if (deserializeJson(doc, msg.data()) != DeserializationError::Ok) return;

            const char *cmd = doc["cmd"];
            if (cmd && strcmp(cmd, "PUMP_ON") == 0) {
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    gState.pumpOn = true;
                    xSemaphoreGive(stateMutex);
                }
                Serial2.println(F("PUMP ON"));
            } else if (cmd && strcmp(cmd, "PUMP_OFF") == 0) {
                if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    gState.pumpOn = false;
                    xSemaphoreGive(stateMutex);
                }
                Serial2.println(F("PUMP OFF"));
            }
        });
        ws.onEvent([](WebsocketsEvent event, String data) {
            wsConnected = (event == WebsocketsEvent::ConnectionOpened);
        });
        ws.connect(wsUrl);
    }

wifi_loop:
    TickType_t   lastWake  = xTaskGetTickCount();
    uint32_t     lastPush  = 0;
    RoverState_t snap;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));

        // Reconnect WiFi if lost
        if (WiFi.status() != WL_CONNECTED) {
            wsConnected = false;
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            continue;
        }

        ws.poll();

        // Push telemetry to Pi @ 2 Hz
        uint32_t now = millis();
        if (now - lastPush >= TELEMETRY_PUSH_MS && wsConnected) {
            lastPush = now;

            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                memcpy(&snap, (void*)&gState, sizeof(snap));
                xSemaphoreGive(stateMutex);
            }

            StaticJsonDocument<256> doc;
            doc["throttle"]  = snap.throttle;
            doc["steering"]  = snap.steering;
            doc["pump"]      = snap.pumpOn;
            doc["failsafe"]  = snap.failsafe;
            doc["obstacle"]  = snap.obstacleDetected;
            doc["wifi_rssi"] = WiFi.RSSI();

            String out;
            serializeJson(doc, out);
            ws.send(out);
        }
    }
}

// ════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n[ESP32-D] Rover Hub starting..."));

    // UART to Mega
    Serial2.begin(115200, SERIAL_8N1, MEGA_RX, MEGA_TX);

    // Create synchronization primitives
    ctrlQueue  = xQueueCreate(2, sizeof(ControlPacket_t));
    stateMutex = xSemaphoreCreateMutex();

    // WiFi in STA — needed for both ESP-NOW and WiFi (same radio)
    // Note: ESP-NOW + WiFi coexistence requires same channel as AP
    WiFi.mode(WIFI_STA);

    Serial.print(F("[ESP32-D] MAC: "));
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println(F("[FATAL] ESP-NOW init failed"));
        while (true) delay(1000);
    }
    esp_now_register_recv_cb(onReceiveCb);

    // Hardware watchdog — 5s global timeout
    esp_task_wdt_init(5, true);

    // ── LAUNCH TASKS ─────────────────────────────────────────
    // Control task: Core 0, HIGH priority (1 below IDLE-level WiFi)
    xTaskCreatePinnedToCore(
        taskControl, "ctrl",
        4096, NULL,
        configMAX_PRIORITIES - 2,
        NULL, 0
    );

    // Display task: Core 1, low priority
    xTaskCreatePinnedToCore(
        taskDisplay, "disp",
        8192, NULL,
        1, NULL, 1
    );

    // WiFi task: Core 1, medium priority
    xTaskCreatePinnedToCore(
        taskWifi, "wifi",
        8192, NULL,
        2, NULL, 1
    );

    Serial.println(F("[ESP32-D] Tasks launched"));
}

// setup() creates all tasks — loop() is unused
void loop() {
    vTaskDelay(portMAX_DELAY);
}
