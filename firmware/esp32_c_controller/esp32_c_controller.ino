/*
 * ============================================================
 *  AgriTech Rover — ESP32-C Controller
 *  Role : RC transmitter — joystick + buttons → ESP-NOW
 *  v1.0
 * ============================================================
 *
 *  ARCHITECTURE
 *  ─────────────────────────────────────────────────────────
 *  Joystick/Buttons ──► ESP32-C ──ESP-NOW──► ESP32-D
 *
 *  This device has NO WiFi connection to the Pi.
 *  It is a pure real-time transmitter.
 *
 *  FUNCTION
 *  ─────────────────────────────────────────────────────────
 *  - Reads analog joystick (X=steering, Y=throttle)
 *  - Reads 4 debounced buttons
 *  - Applies deadzone + normalization
 *  - Sends ControlPacket_t via ESP-NOW at ~30 Hz
 *  - Shows live status on 2.8" touch TFT (ILI9341)
 *
 *  ESP-NOW PACKET  (8 bytes, packed)
 *  ─────────────────────────────────────────────────────────
 *    int16_t  throttle    — -255..+255
 *    int16_t  steering    — -255..+255
 *    uint8_t  buttons     — Bit0:Pump Bit1:ESTOP Bit2:B3 Bit3:B4
 *    uint8_t  seq         — wrapping sequence counter
 *    uint16_t crc16       — CRC-16/CCITT over first 6 bytes
 *
 *  HARDWARE PIN MAP
 *  ─────────────────────────────────────────────────────────
 *  Joystick X (steering)  : GPIO 34 (ADC1_CH6 — input only)
 *  Joystick Y (throttle)  : GPIO 35 (ADC1_CH7 — input only)
 *  Joystick button        : GPIO 32
 *  Button 1 (Pump toggle) : GPIO 25
 *  Button 2 (E-STOP)      : GPIO 26
 *  Button 3               : GPIO 27
 *  Button 4               : GPIO 14
 *  TFT ILI9341 CS         : GPIO 5
 *  TFT DC                 : GPIO 21
 *  TFT RST                : GPIO 22
 *  TFT SCK                : GPIO 18  (VSPI_SCK)
 *  TFT MOSI               : GPIO 23  (VSPI_MOSI)
 *  TFT MISO               : GPIO 19  (VSPI_MISO)
 *  XPT2046 Touch CS       : GPIO 4
 *  XPT2046 Touch IRQ      : GPIO 2
 *
 *  CONFIGURATION
 *  ─────────────────────────────────────────────────────────
 *  Set ROVER_MAC to the MAC address of ESP32-D.
 *  Flash ESP32-D first, check its Serial output for MAC.
 * ============================================================
 *
 *  Libraries required:
 *    esp_now.h, WiFi.h  (Arduino ESP32 core)
 *    Adafruit_GFX, Adafruit_ILI9341
 *    XPT2046_Touchscreen
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <esp_now.h>

// ─── TARGET MAC ─────────────────────────────────────────────
// Set this to the MAC address of your ESP32-D (rover hub).
// Flash ESP32-D and read its MAC from Serial output.
uint8_t ROVER_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─── PIN DEFINITIONS ────────────────────────────────────────
#define JOY_X_PIN   34
#define JOY_Y_PIN   35
#define JOY_BTN     32

#define BTN_PUMP    25   // Toggles pump on rover
#define BTN_ESTOP   26   // Emergency stop
#define BTN_3       27
#define BTN_4       14

#define TFT_CS      5
#define TFT_DC      21
#define TFT_RST     22
#define TOUCH_CS    4
#define TOUCH_IRQ   2

// ─── CONSTANTS ──────────────────────────────────────────────
#define JOY_CENTER        2048   // 12-bit ADC midpoint
#define JOY_DEADZONE       150   // Raw ADC units
#define JOY_MIN             0
#define JOY_MAX          4095
#define SEND_INTERVAL_MS   33   // ~30 Hz
#define DEBOUNCE_MS        50

// ─── PACKET STRUCTURE ────────────────────────────────────────
typedef struct __attribute__((packed)) {
    int16_t  throttle;
    int16_t  steering;
    uint8_t  buttons;
    uint8_t  seq;
    uint16_t crc16;
} ControlPacket_t;

// ─── DISPLAY ────────────────────────────────────────────────
Adafruit_ILI9341     tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen  touch(TOUCH_CS, TOUCH_IRQ);

// ─── STATE ──────────────────────────────────────────────────
static uint8_t  pktSeq       = 0;
static bool     pumpOn        = false;
static bool     estopActive   = false;
static uint32_t lastSendMs    = 0;
static bool     lastDelivered = false;
static uint32_t txCount       = 0;
static uint32_t txFail        = 0;

// Button debounce timestamps
static uint32_t btnLastMs[4]  = {0, 0, 0, 0};
static bool     btnState[4]   = {false, false, false, false};

// ─── CRC-16/CCITT ────────────────────────────────────────────
uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

// ─── ESP-NOW CALLBACK ────────────────────────────────────────
void onSendCb(const uint8_t *mac, esp_now_send_status_t status) {
    lastDelivered = (status == ESP_NOW_SEND_SUCCESS);
    if (!lastDelivered) txFail++;
}

// ─── ADC NORMALIZATION ───────────────────────────────────────
// Maps raw ADC (0-4095) with deadzone to -255..+255
int16_t normalizeAxis(int raw, bool invert) {
    if (invert) raw = JOY_MAX - raw;
    int delta = raw - JOY_CENTER;
    if (abs(delta) <= JOY_DEADZONE) return 0;

    if (delta > 0)
        return (int16_t)map(delta, JOY_DEADZONE, JOY_CENTER, 0, 255);
    else
        return (int16_t)-map(-delta, JOY_DEADZONE, JOY_CENTER, 0, 255);
}

// ─── BUTTON HANDLER ──────────────────────────────────────────
// Returns true on rising edge (press), with debounce
bool buttonPressed(int btnIndex, int pin) {
    bool current = (digitalRead(pin) == LOW);
    uint32_t now = millis();
    if (current && !btnState[btnIndex] && (now - btnLastMs[btnIndex] > DEBOUNCE_MS)) {
        btnState[btnIndex] = true;
        btnLastMs[btnIndex] = now;
        return true;
    }
    if (!current) btnState[btnIndex] = false;
    return false;
}

// ─── DISPLAY ─────────────────────────────────────────────────
void drawUI(int16_t throttle, int16_t steering, uint8_t buttons) {
    static int16_t lastThrottle = 9999, lastSteering = 9999;
    static bool    lastPump     = false, lastEstop = false;
    static bool    lastConn     = false;

    bool conn = lastDelivered;

    // Only redraw changed elements
    if (throttle == lastThrottle && steering == lastSteering &&
        pumpOn == lastPump && estopActive == lastEstop && conn == lastConn)
        return;

    // Header bar
    tft.fillRect(0, 0, 240, 30, ILI9341_NAVY);
    tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(2);
    tft.setCursor(50, 8); tft.print("AGRI ROVER");

    // Connection indicator
    tft.fillCircle(220, 15, 8, conn ? ILI9341_GREEN : ILI9341_RED);

    // Throttle bar
    tft.fillRect(10, 50, 220, 30, ILI9341_DARKGREY);
    tft.setTextColor(ILI9341_WHITE); tft.setTextSize(1);
    tft.setCursor(10, 42); tft.print("THROTTLE");
    int barW = map(abs(throttle), 0, 255, 0, 218);
    uint16_t barColor = throttle >= 0 ? ILI9341_GREEN : ILI9341_ORANGE;
    if (barW > 0) tft.fillRect(11, 51, barW, 28, barColor);
    tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(95, 55); tft.print(throttle > 0 ? "+" : ""); tft.print(throttle);

    // Steering bar
    tft.fillRect(10, 110, 220, 30, ILI9341_DARKGREY);
    tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 102); tft.print("STEERING");
    int sBarX  = 120;
    int sBarW  = map(abs(steering), 0, 255, 0, 109);
    uint16_t sColor = ILI9341_CYAN;
    if (steering > 0) tft.fillRect(sBarX, 111, sBarW, 28, sColor);
    else if (steering < 0) tft.fillRect(sBarX - sBarW, 111, sBarW, 28, sColor);
    tft.drawFastVLine(sBarX, 110, 30, ILI9341_WHITE);
    tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(95, 115); tft.print(steering > 0 ? "+" : ""); tft.print(steering);

    // Pump button indicator
    tft.fillRoundRect(10, 170, 100, 50, 8, pumpOn ? ILI9341_GREEN : ILI9341_DARKGREY);
    tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(28, 190); tft.print("PUMP");
    tft.setCursor(28, 208); tft.print(pumpOn ? " ON" : "OFF");

    // E-STOP indicator
    tft.fillRoundRect(130, 170, 100, 50, 8, estopActive ? ILI9341_RED : ILI9341_DARKGREY);
    tft.setCursor(135, 185); tft.print("E-STOP");
    tft.setCursor(152, 205); tft.print(estopActive ? " !" : " -");

    // Stats footer
    tft.fillRect(0, 295, 240, 25, ILI9341_BLACK);
    tft.setTextSize(1); tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setCursor(5, 300);
    tft.printf("TX:%lu  FAIL:%lu  SEQ:%d", txCount, txFail, pktSeq);

    lastThrottle = throttle;
    lastSteering = steering;
    lastPump     = pumpOn;
    lastEstop    = estopActive;
    lastConn     = conn;
}

// ════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println(F("[ESP32-C] Controller starting..."));

    // Buttons
    for (int p : {JOY_BTN, BTN_PUMP, BTN_ESTOP, BTN_3, BTN_4})
        pinMode(p, INPUT_PULLUP);

    // ADC attenuation for joystick (0–3.3V range)
    analogSetAttenuation(ADC_11db);

    // TFT
    tft.begin();
    tft.setRotation(2);
    tft.fillScreen(ILI9341_BLACK);
    touch.begin();

    tft.setTextColor(ILI9341_GREEN); tft.setTextSize(2);
    tft.setCursor(40, 100); tft.print("AGRI ROVER");
    tft.setTextColor(ILI9341_WHITE); tft.setTextSize(1);
    tft.setCursor(70, 130); tft.print("Controller v1.0");

    // WiFi in STA mode, no AP — required for ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.print(F("[ESP32-C] MAC: "));
    Serial.println(WiFi.macAddress());

    // ESP-NOW init
    if (esp_now_init() != ESP_OK) {
        Serial.println(F("[FATAL] ESP-NOW init failed"));
        tft.setTextColor(ILI9341_RED);
        tft.setCursor(30, 160); tft.print("ESP-NOW FAILED!");
        while (true) delay(1000);
    }
    esp_now_register_send_cb(onSendCb);

    // Register peer (ESP32-D)
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, ROVER_MAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    delay(1000);
    tft.fillScreen(ILI9341_BLACK);

    Serial.println(F("[ESP32-C] Ready — transmitting"));
}

// ════════════════════════════════════════════════════════════
void loop() {
    uint32_t now = millis();

    // ─── BUTTON EDGE DETECTION ──────────────────────────────
    if (buttonPressed(0, BTN_PUMP))  pumpOn      = !pumpOn;
    if (buttonPressed(1, BTN_ESTOP)) estopActive = !estopActive;
    // BTN_3, BTN_4: reserved, pass in bitmask for future use
    bool b3 = (digitalRead(BTN_3) == LOW);
    bool b4 = (digitalRead(BTN_4) == LOW);

    // ─── BUILD BUTTONS BYTE ─────────────────────────────────
    uint8_t btns = 0;
    if (pumpOn)      btns |= (1 << 0);
    if (estopActive) btns |= (1 << 1);
    if (b3)          btns |= (1 << 2);
    if (b4)          btns |= (1 << 3);

    // ─── READ JOYSTICK ──────────────────────────────────────
    int16_t throttle = 0;
    int16_t steering = 0;

    if (!estopActive) {
        throttle = normalizeAxis(analogRead(JOY_Y_PIN), true);  // invert Y
        steering = normalizeAxis(analogRead(JOY_X_PIN), false);
    }

    // ─── SEND PACKET @ 30 Hz ────────────────────────────────
    if (now - lastSendMs >= SEND_INTERVAL_MS) {
        lastSendMs = now;

        ControlPacket_t pkt;
        pkt.throttle = throttle;
        pkt.steering = steering;
        pkt.buttons  = btns;
        pkt.seq      = pktSeq++;
        pkt.crc16    = crc16((uint8_t *)&pkt, sizeof(pkt) - 2);

        esp_now_send(ROVER_MAC, (uint8_t *)&pkt, sizeof(pkt));
        txCount++;
    }

    // ─── UPDATE DISPLAY (non-blocking, only on change) ──────
    drawUI(throttle, steering, btns);
}
