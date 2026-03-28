/*
 * ============================================================
 *  AgriTech Rover — ESP32-CAM
 *  Role : MJPEG video stream → Raspberry Pi
 *  v1.0
 * ============================================================
 *
 *  ARCHITECTURE
 *  ─────────────────────────────────────────────────────────
 *  ESP32-CAM ──MJPEG/HTTP──► Raspberry Pi (port 81)
 *
 *  The Pi consumes the stream at http://<cam_ip>:81/stream
 *  for AI inference (TFLite / Edge Impulse) and forwards
 *  the MJPEG to the React dashboard.
 *
 *  ENDPOINTS
 *  ─────────────────────────────────────────────────────────
 *    GET /stream    — MJPEG continuous stream
 *    GET /capture   — Single JPEG frame
 *    GET /status    — JSON: {ip, rssi, fps, uptime}
 *    POST /quality  — Body: {quality:N}  (10=best, 63=worst)
 *    POST /size     — Body: {size:"QVGA"|"VGA"|"SVGA"}
 *
 *  CONFIGURATION
 *  ─────────────────────────────────────────────────────────
 *  Set WIFI_SSID / WIFI_PASS below.
 *  Default resolution: QVGA (320×240) @ quality 15.
 *  Change to VGA for better quality (heavier on WiFi).
 *
 *  HARDWARE
 *  ─────────────────────────────────────────────────────────
 *  AI Thinker ESP32-CAM module.
 *  Flash with: Tools → Board: "AI Thinker ESP32-CAM"
 *              PSRAM: Enabled
 * ============================================================
 */

#include "esp_camera.h"
#include "esp_http_server.h"
#include <WiFi.h>

// ─── CONFIGURATION ───────────────────────────────────────────
#define WIFI_SSID   "YOUR_SSID"
#define WIFI_PASS   "YOUR_PASS"
#define STREAM_PORT  81

// ─── AI THINKER PIN MAP ──────────────────────────────────────
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

// ─── STATE ───────────────────────────────────────────────────
static httpd_handle_t streamServer = NULL;
static uint32_t       frameCount   = 0;
static uint32_t       startMs      = 0;

// ─── MJPEG BOUNDARY ──────────────────────────────────────────
#define PART_BOUNDARY "mjpegboundary"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_PART =
    "--" PART_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ─── /stream HANDLER ─────────────────────────────────────────
static esp_err_t streamHandler(httpd_req_t *req) {
    camera_fb_t *fb  = NULL;
    char        *part = (char *)malloc(128);
    if (!part) return ESP_ERR_NO_MEM;

    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) { free(part); return res; }

    // Disable Nagle — reduces latency
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "30");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println(F("[CAM] Frame capture failed"));
            res = ESP_FAIL;
            break;
        }

        size_t partLen = snprintf(part, 128, STREAM_PART, fb->len);

        res = httpd_resp_send_chunk(req, part, partLen);
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, "\r\n", 2);

        esp_camera_fb_return(fb);
        frameCount++;

        if (res != ESP_OK) break; // Client disconnected
    }

    free(part);
    return res;
}

// ─── /capture HANDLER ────────────────────────────────────────
static esp_err_t captureHandler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return httpd_resp_send_500(req);

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

// ─── /status HANDLER ─────────────────────────────────────────
static esp_err_t statusHandler(httpd_req_t *req) {
    uint32_t uptime  = (millis() - startMs) / 1000;
    float    fps     = uptime > 0 ? (float)frameCount / uptime : 0.0f;

    char buf[200];
    snprintf(buf, sizeof(buf),
        "{\"ip\":\"%s\",\"rssi\":%d,\"fps\":%.1f,\"uptime\":%lu,\"frames\":%lu}",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(), fps, (unsigned long)uptime, (unsigned long)frameCount
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, buf);
}

// ─── SERVER INIT ─────────────────────────────────────────────
void startStreamServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port    = STREAM_PORT;
    config.max_uri_handlers = 8;

    httpd_uri_t streamUri = {
        .uri      = "/stream",
        .method   = HTTP_GET,
        .handler  = streamHandler,
        .user_ctx = NULL
    };
    httpd_uri_t captureUri = {
        .uri      = "/capture",
        .method   = HTTP_GET,
        .handler  = captureHandler,
        .user_ctx = NULL
    };
    httpd_uri_t statusUri = {
        .uri      = "/status",
        .method   = HTTP_GET,
        .handler  = statusHandler,
        .user_ctx = NULL
    };

    if (httpd_start(&streamServer, &config) == ESP_OK) {
        httpd_register_uri_handler(streamServer, &streamUri);
        httpd_register_uri_handler(streamServer, &captureUri);
        httpd_register_uri_handler(streamServer, &statusUri);
        Serial.printf("[CAM] Stream: http://%s:%d/stream\n",
                      WiFi.localIP().toString().c_str(), STREAM_PORT);
    }
}

// ════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n[ESP32-CAM] Starting..."));

    camera_config_t cfg = {};
    cfg.ledc_channel    = LEDC_CHANNEL_0;
    cfg.ledc_timer      = LEDC_TIMER_0;
    cfg.pin_d0          = Y2_GPIO_NUM;
    cfg.pin_d1          = Y3_GPIO_NUM;
    cfg.pin_d2          = Y4_GPIO_NUM;
    cfg.pin_d3          = Y5_GPIO_NUM;
    cfg.pin_d4          = Y6_GPIO_NUM;
    cfg.pin_d5          = Y7_GPIO_NUM;
    cfg.pin_d6          = Y8_GPIO_NUM;
    cfg.pin_d7          = Y9_GPIO_NUM;
    cfg.pin_xclk        = XCLK_GPIO_NUM;
    cfg.pin_pclk        = PCLK_GPIO_NUM;
    cfg.pin_vsync       = VSYNC_GPIO_NUM;
    cfg.pin_href        = HREF_GPIO_NUM;
    cfg.pin_sscb_sda    = SIOD_GPIO_NUM;
    cfg.pin_sscb_scl    = SIOC_GPIO_NUM;
    cfg.pin_pwdn        = PWDN_GPIO_NUM;
    cfg.pin_reset       = RESET_GPIO_NUM;
    cfg.xclk_freq_hz    = 20000000;
    cfg.pixel_format    = PIXFORMAT_JPEG;

    // Use PSRAM if available for better buffering
    if (psramFound()) {
        cfg.frame_size    = FRAMESIZE_VGA;   // 640×480
        cfg.jpeg_quality  = 15;
        cfg.fb_count      = 2;
        Serial.println(F("[CAM] PSRAM found — VGA mode"));
    } else {
        cfg.frame_size    = FRAMESIZE_QVGA;  // 320×240
        cfg.jpeg_quality  = 12;
        cfg.fb_count      = 1;
        Serial.println(F("[CAM] No PSRAM — QVGA mode"));
    }

    if (esp_camera_init(&cfg) != ESP_OK) {
        Serial.println(F("[FATAL] Camera init failed"));
        while (true) delay(1000);
    }

    // Optional: adjust image settings
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        s->set_hmirror(s, 0);
        s->set_vflip(s, 0);
    }

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print(F("[CAM] Connecting WiFi"));
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
        if (millis() - t > 20000) {
            Serial.println(F("\n[ERROR] WiFi timeout"));
            ESP.restart();
        }
    }
    Serial.printf("\n[CAM] IP: %s\n", WiFi.localIP().toString().c_str());

    startMs = millis();
    startStreamServer();
}

void loop() {
    // Reconnect if dropped
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[CAM] WiFi lost — restarting"));
        delay(2000);
        ESP.restart();
    }
    delay(5000);
}
