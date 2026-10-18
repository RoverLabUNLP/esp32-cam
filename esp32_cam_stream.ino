/**
 * ============================================================================
 * Proyecto: ESP32-CAM AI-Thinker CameraWebServer Oficial (Sensor OV3660)
 * Framework: Arduino Core for ESP32 (Espressif)
 * ============================================================================
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebSocketsServer.h>

// ============================================================================
// SELECCIÓN DE MODELO DE CÁMARA
// ============================================================================
#define CAMERA_MODEL_AI_THINKER // Placa AI-Thinker ESP32-CAM (con PSRAM)
#include "camera_pins.h"
#include "wifi_credentials.h"

// ============================================================================
// CREDENCIALES DE RED WI-FI
// ============================================================================

// ============================================================================
// SERVIDOR WEBSOCKET Y VARIABLES DE CONTROL / TELEMETRÍA
// ============================================================================
WebSocketsServer webSocket = WebSocketsServer(82);

unsigned long lastCmdTime = 0;
unsigned long lastTelemetryTime = 0;
bool isMoving = false;

int simDist = 20;
int simStep = 2;

// Orden de parada cinemática (v_lineal = 0, w_angular = 0)
static const char* TWIST_STOP = "$TWIST,0.00,0.00";

// Prototipos de funciones exportadas por app_httpd.cpp
void startCameraServer();
void setupLedFlash(int pin);

// ============================================================================
// EVENTOS WEBSOCKET
// ============================================================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[WS] Cliente #%u conectado desde %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      lastCmdTime = millis();
      isMoving = false;
      break;
    }

    case WStype_DISCONNECTED:
      Serial.printf("[WS] Cliente #%u desconectado\n", num);
      Serial.println(TWIST_STOP);  // Failsafe: parada inmediata
      isMoving = false;
      break;

    case WStype_TEXT:
      lastCmdTime = millis();
      // Reenvío transparente de la trama recibida hacia UART
      Serial.write(payload, length);
      Serial.println();
      // Solo se arma el watchdog si el comando no es ya una parada
      isMoving = !(length == strlen(TWIST_STOP) &&
                   memcmp(payload, TWIST_STOP, length) == 0);
      break;

    default:
      break;
  }
}
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
  Serial.print("[INFO] Servidor WebSocket Control/Telemetría: ws://");
  Serial.print(WiFi.localIP());
  Serial.println(":82");
  Serial.println("====================================================");

  // Iniciar servidor WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  lastCmdTime = millis();

  // Iniciar servidor web y servidor de streaming
  startCameraServer();
}
void loop() {
  webSocket.loop();

  unsigned long now = millis();

  // Generador periódico de telemetría simulada (cada 100 ms)
  if (now - lastTelemetryTime >= 100) {
    lastTelemetryTime = now;

    // Oscilación de distancia entre 20 y 100 cm
    simDist += simStep;
    if (simDist >= 100) {
      simDist = 100;
      simStep = -2;
    } else if (simDist <= 20) {
      simDist = 20;
      simStep = 2;
    }

    float bateria = 7.8f;
    int odomL = 120;
    int odomR = 118;

    char telBuffer[64];

    // $SENS,<distancia_cm>,<bateria_V>
    snprintf(telBuffer, sizeof(telBuffer), "$SENS,%d,%.1f", simDist, bateria);
    webSocket.broadcastTXT(telBuffer);

    // $ODOM,<rueda_izq>,<rueda_der>
    snprintf(telBuffer, sizeof(telBuffer), "$ODOM,%d,%d", odomL, odomR);
    webSocket.broadcastTXT(telBuffer);

    // $IMU,<v1>,<v2>,<v3> (valores simulados)
    snprintf(telBuffer, sizeof(telBuffer), "$IMU,%.1f,%.1f,%.1f", 0.0f, 0.0f, 25.0f);
    webSocket.broadcastTXT(telBuffer);
  }

  // Temporizador de seguridad (watchdog): más de 500 ms sin recibir comandos de texto
  if (isMoving && (now - lastCmdTime > 500)) {
    Serial.println(TWIST_STOP);  // Failsafe: parada de emergencia hacia UART
    isMoving = false;
  }
}

