/*
 * ============================================================
 *  AgriTech Rover — Arduino Mega 2560 Controller
 *  Role : Motor execution + sensor telemetry
 *  v2.0 — Revised for ESP32-D UART control architecture
 * ============================================================
 *
 *  ARCHITECTURE
 *  ─────────────────────────────────────────────────────────
 *  ESP32-D  ──UART(Serial3)──►  Mega  ──USB(Serial)──►  Pi
 *
 *  Motor commands come ONLY from ESP32-D via Serial3.
 *  iBUS RC removed — joystick is now handled by ESP32-C/D.
 *  Obstacle detection remains LOCAL as a hard safety layer.
 *
 *  UART COMMAND PROTOCOL (from ESP32-D, Serial3 @ 115200):
 *    DRIVE L<n> R<n>\n   — set motors, n = -255..255
 *    PUMP ON\n / PUMP OFF\n
 *    ESTOP\n              — immediate motor stop
 *    PING\n               — heartbeat (resets watchdog)
 *
 *  TELEMETRY OUTPUT (USB Serial @ 9600 → Raspberry Pi):
 *    M:<moisture>|W:<rain>|AT:<airTemp>|ST:<soilTemp>|H:<humidity>|O:<obstacle>\n
 *    Emitted every 2 s.
 *
 *  PIN MAP
 *  ─────────────────────────────────────────────────────────
 *  DS18B20 soil temp   : D2  (OneWire)
 *  Soil moisture       : A0  (analog)
 *  DHT11 air temp/hum  : D3
 *  Rain sensor         : D4  (digital, LOW = raining)
 *  Pump 1              : D6
 *  Pump 2              : D7
 *  BTS7960 Left  PWM   : D8 (L_PWM_L), D9  (L_PWM_R)
 *  BTS7960 Left  EN    : D25 (L_EN_L), D26 (L_EN_R)
 *  BTS7960 Right PWM   : D10 (R_PWM_L), D11 (R_PWM_R)
 *  BTS7960 Right EN    : D27 (R_EN_L),  D28 (R_EN_R)
 *  HC-SR04 ×6          : D29–D40 (trig/echo pairs)
 *  ESP32-D UART        : Serial3 TX=D14 RX=D15
 * ============================================================
 */

#include <DHT.h>
#include <DallasTemperature.h>
#include <OneWire.h>

// ─── PIN DEFINITIONS ────────────────────────────────────────
#define PIN_DS18B20   2
#define PIN_SOIL      A0
#define PIN_DHT       3
#define PIN_RAIN      4

#define PIN_PUMP_1    6
#define PIN_PUMP_2    7

// BTS7960 — Left side
#define L_PWM_L   8
#define L_PWM_R   9
#define L_EN_L    25
#define L_EN_R    26

// BTS7960 — Right side
#define R_PWM_L   10
#define R_PWM_R   11
#define R_EN_L    27
#define R_EN_R    28

// HC-SR04 ultrasonic sensors [trig, echo]
const int US_PINS[6][2] = {
    {29, 30}, {31, 32}, {33, 34},
    {35, 36}, {37, 38}, {39, 40}
};

// ─── OBJECTS ────────────────────────────────────────────────
OneWire           oneWire(PIN_DS18B20);
DallasTemperature sensorsDS(&oneWire);
DHT               dht(PIN_DHT, DHT11);

// ─── TIMING & STATE ─────────────────────────────────────────
#define UART_WATCHDOG_MS  500   // Stop motors if no UART for this long
#define TELEMETRY_MS     2000   // Sensor report interval

unsigned long lastUartMs   = 0;
unsigned long lastTelemetry = 0;
bool          watchdogFired = false;

// ─── FORWARD DECLARATIONS ───────────────────────────────────
void  setMotor(int pL, int pR, int spd);
void  stopMotors();
long  readUS(int idx);
bool  obstacleAhead();
void  handleUart();
void  reportTelemetry();

// ════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(9600);     // USB → Raspberry Pi (telemetry)
    Serial3.begin(115200);  // UART ← ESP32-D (commands)

    sensorsDS.begin();
    dht.begin();
    pinMode(PIN_RAIN, INPUT);

    for (int i = 0; i < 6; i++) {
        pinMode(US_PINS[i][0], OUTPUT);
        pinMode(US_PINS[i][1], INPUT);
    }

    // BTS7960 enable pins — hold HIGH permanently; direction via PWM
    for (int p : {L_EN_L, L_EN_R, R_EN_L, R_EN_R})
        pinMode(p, OUTPUT), digitalWrite(p, HIGH);

    for (int p : {L_PWM_L, L_PWM_R, R_PWM_L, R_PWM_R})
        pinMode(p, OUTPUT);

    pinMode(PIN_PUMP_1, OUTPUT); digitalWrite(PIN_PUMP_1, LOW);
    pinMode(PIN_PUMP_2, OUTPUT); digitalWrite(PIN_PUMP_2, LOW);

    // Seed watchdog so we don't immediately fire on boot
    lastUartMs = millis();

    Serial.println(F("MEGA_READY"));
    Serial3.println(F("MEGA_READY"));
}

// ════════════════════════════════════════════════════════════
void loop() {
    unsigned long now = millis();

    // 1. Parse incoming UART commands from ESP32-D
    handleUart();

    // 2. Local hard-stop: no UART heartbeat → stop motors
    if (now - lastUartMs > UART_WATCHDOG_MS) {
        if (!watchdogFired) {
            stopMotors();
            watchdogFired = true;
            Serial.println(F("WARN:UART_TIMEOUT"));
        }
    } else {
        watchdogFired = false;
    }

    // 3. Periodic telemetry to Pi
    if (now - lastTelemetry >= TELEMETRY_MS) {
        lastTelemetry = now;
        reportTelemetry();
    }
}

// ─── UART COMMAND PARSER ─────────────────────────────────────
void handleUart() {
    while (Serial3.available()) {
        String cmd = Serial3.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() == 0) continue;

        lastUartMs = millis(); // Any valid byte resets watchdog

        if (cmd == F("PING")) {
            // Heartbeat only — watchdog already reset above
            return;
        }

        if (cmd == F("ESTOP")) {
            stopMotors();
            return;
        }

        if (cmd.startsWith(F("PUMP "))) {
            bool on = cmd.endsWith(F("ON"));
            digitalWrite(PIN_PUMP_1, on ? HIGH : LOW);
            return;
        }

        if (cmd.startsWith(F("PUMP2 "))) {
            bool on = cmd.endsWith(F("ON"));
            digitalWrite(PIN_PUMP_2, on ? HIGH : LOW);
            return;
        }

        // DRIVE L<n> R<n>
        if (cmd.startsWith(F("DRIVE "))) {
            int lIdx = cmd.indexOf('L');
            int rIdx = cmd.indexOf('R', lIdx + 1);
            if (lIdx < 0 || rIdx < 0) return;

            int leftSpd  = cmd.substring(lIdx + 1, rIdx).trim().toInt();
            int rightSpd = cmd.substring(rIdx + 1).trim().toInt();

            leftSpd  = constrain(leftSpd,  -255, 255);
            rightSpd = constrain(rightSpd, -255, 255);

            // Local obstacle override — never move forward into obstacle
            if ((leftSpd > 0 || rightSpd > 0) && obstacleAhead()) {
                stopMotors();
                Serial3.println(F("BLOCKED"));
                return;
            }

            setMotor(L_PWM_L, L_PWM_R, leftSpd);
            setMotor(R_PWM_L, R_PWM_R, rightSpd);
        }
    }
}

// ─── MOTOR DRIVER ────────────────────────────────────────────
void setMotor(int pL, int pR, int spd) {
    if (spd > 0) {
        analogWrite(pL, spd);
        analogWrite(pR, 0);
    } else if (spd < 0) {
        analogWrite(pL, 0);
        analogWrite(pR, -spd);
    } else {
        analogWrite(pL, 0);
        analogWrite(pR, 0);
    }
}

void stopMotors() {
    setMotor(L_PWM_L, L_PWM_R, 0);
    setMotor(R_PWM_L, R_PWM_R, 0);
}

// ─── ULTRASONIC ──────────────────────────────────────────────
long readUS(int idx) {
    digitalWrite(US_PINS[idx][0], LOW);
    delayMicroseconds(2);
    digitalWrite(US_PINS[idx][0], HIGH);
    delayMicroseconds(10);
    digitalWrite(US_PINS[idx][0], LOW);
    long dur = pulseIn(US_PINS[idx][1], HIGH, 5000);
    return (dur == 0) ? 999 : (dur * 0.034 / 2);
}

bool obstacleAhead() {
    for (int i = 0; i < 6; i++) {
        if (readUS(i) < 25) return true;
    }
    return false;
}

// ─── TELEMETRY ───────────────────────────────────────────────
void reportTelemetry() {
    sensorsDS.requestTemperatures();
    float soilTemp = sensorsDS.getTempCByIndex(0);
    float airTemp  = dht.readTemperature();
    int   humidity = dht.readHumidity();
    int   moisture = analogRead(PIN_SOIL);
    int   rain     = !digitalRead(PIN_RAIN);  // 1 = raining
    int   obstacle = obstacleAhead() ? 1 : 0;

    // Format: M:<raw>|W:<0/1>|AT:<float>|ST:<float>|H:<int>|O:<0/1>
    Serial.print(F("M:")); Serial.print(moisture);
    Serial.print(F("|W:")); Serial.print(rain);
    Serial.print(F("|AT:")); Serial.print(airTemp, 1);
    Serial.print(F("|ST:")); Serial.print(soilTemp, 1);
    Serial.print(F("|H:")); Serial.print(humidity);
    Serial.print(F("|O:")); Serial.println(obstacle);
}
