/**
 * ============================================================================
 * Proyecto: ESP32-CAM AI-Thinker CameraWebServer Oficial (Sensor OV3660)
 * Framework: Arduino Core for ESP32 (Espressif)
 * ============================================================================
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>

// ============================================================================
// SELECCIÓN DE MODELO DE CÁMARA
// ============================================================================
#define CAMERA_MODEL_AI_THINKER // Placa AI-Thinker ESP32-CAM (con PSRAM)
#include "camera_pins.h"

// ============================================================================
// CREDENCIALES DE RED WI-FI
// ============================================================================
const char* ssid = "WIFI_DE_LA_FACU";
const char* password = "CLAVE_DE_LA_FACU";

// Prototipos de funciones exportadas por app_httpd.cpp
void startCameraServer();
void setupLedFlash(int pin);

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("\n--- ESP32-CAM AI-Thinker CameraWebServer (OV3660) ---");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 8000000;          // 8 MHz
  config.frame_size   = FRAMESIZE_UXGA;   // Tamaño máximo pre-asignado en búfer
  config.pixel_format = PIXFORMAT_JPEG;   // Streaming JPEG
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count     = 1;

  // Optimización de búferes si se detecta PSRAM en placa
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      Serial.println("[CÁMARA] PSRAM externa detectada. Habilitando doble búfer.");
      config.jpeg_quality = 10;
      config.fb_count     = 2;
      config.grab_mode    = CAMERA_GRAB_LATEST;
    } else {
      Serial.println("[AVISO] No se detectó PSRAM. Limitando resolución.");
      config.frame_size   = FRAMESIZE_SVGA;
      config.fb_location  = CAMERA_FB_IN_DRAM;
    }
  }

  // Inicialización de hardware de la cámara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] Falló la inicialización de la cámara con código: 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    // Calibración de fábrica oficial de Espressif para sensor OV3660
    if (s->id.PID == OV3660_PID) {
      Serial.println("[CÁMARA] Sensor OV3660 detectado. Aplicando calibración oficial Espressif.");
      s->set_vflip(s, 1);        // Corrige orientación invertida
      s->set_brightness(s, 1);   // Incremento sutil de brillo
      s->set_saturation(s, -2);  // Reduce saturación inicial para neutralizar tintes
    }

    // Resolución inicial para fluidez en la transmisión
    if (config.pixel_format == PIXFORMAT_JPEG) {
      s->set_framesize(s, FRAMESIZE_VGA);
    }
  }

  // Configuración del LED Flash incorporado (GPIO 4)
#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  // Conexión Wi-Fi
  Serial.println();
  Serial.print("[WIFI] Conectando a ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("====================================================");
  Serial.println("[WIFI] Conectado exitosamente.");
  Serial.print("[INFO] Panel Web de Control y Video: http://");
  Serial.println(WiFi.localIP());
  Serial.print("[INFO] Flujo Directo de Video (Stream): http://");
  Serial.print(WiFi.localIP());
  Serial.println(":81/stream");
  Serial.println("====================================================");

  // Iniciar servidor web y servidor de streaming
  startCameraServer();
}

void loop() {
  // El servidor web, WebSocket y streaming corren en tareas FreeRTOS independientes
  delay(10000);
}
