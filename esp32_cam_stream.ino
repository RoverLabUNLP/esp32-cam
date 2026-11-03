/**
 * ============================================================================
 * Proyecto: ESP32-CAM AI-Thinker CameraWebServer Oficial (Sensor OV3660)
 * Framework: Arduino Core for ESP32 (Espressif)
 * ============================================================================
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>

// ============================================================================
// SELECCIÓN DE MODELO DE CÁMARA
// ============================================================================
#define CAMERA_MODEL_AI_THINKER // Placa AI-Thinker ESP32-CAM (con PSRAM)
#include "camera_pins.h"

// ============================================================================
// UART CON LA EDU-CIAA (esquematico Rev A, hoja 2)
// ============================================================================
// La EDU-CIAA va por la UART2 en GPIO13 / GPIO14. La UART0 (Serial, GPIO1/3)
// queda para programar y para los mensajes de depuracion: asi los mensajes de
// arranque de la ESP32 y los Serial.print no le llegan a la EDU-CIAA.
//   ESP32 GPIO13 (RX) <- RS232_TXD de la EDU-CIAA
//   ESP32 GPIO14 (TX) -> RS232_RXD de la EDU-CIAA
//   GND comun
// GPIO13/14 son tambien pines del slot microSD: no usar la SD con esta UART.
//
// Regla: SOLO sendTwistToCIAA() escribe en SerialCIAA y SOLO pollCIAA() lee.
// Todo log de depuracion va por Serial (UART0).
#define CIAA_UART_NUM 2
#define CIAA_RX_PIN   13
#define CIAA_TX_PIN   14
#define CIAA_BAUD     115200
HardwareSerial SerialCIAA(CIAA_UART_NUM);

// Poner en 1 para generar telemetria simulada (pruebas sin EDU-CIAA conectada)
#define TELEMETRY_SIMULATED 0

// ============================================================================
// RED WI-FI (WiFiManager: sin credenciales en el codigo)
// ============================================================================
// Al arrancar intenta conectarse a la ultima red guardada. Si no puede, la
// ESP32 crea su propia red WM_AP_NOMBRE: conectarse desde el celular o la PC,
// se abre una pagina (o entrar a http://192.168.4.1), elegir la red y poner la
// clave. Queda guardada en la flash de la ESP32 para los proximos arranques.
// Para cambiar de red estando conectada: mandar "$WIFI,RESET" por el WebSocket.
#define WM_AP_NOMBRE      "RoverCam-Config"
#define WM_AP_CLAVE       "rover2026"   // minimo 8 caracteres
#define WM_PORTAL_TIMEOUT 180           // s con el portal abierto; despues reinicia y reintenta
#define WM_CONEXION_TIMEOUT 20          // s intentando la red guardada
#define MDNS_NOMBRE       "rovercam"    // -> http://rovercam.local
static const char* CMD_WIFI_RESET = "$WIFI,RESET";

// ============================================================================
// SERVIDOR WEBSOCKET Y VARIABLES DE CONTROL / TELEMETRÍA
// ============================================================================
WebSocketsServer webSocket = WebSocketsServer(82);

unsigned long lastCmdTime = 0;
bool isMoving = false;

#if TELEMETRY_SIMULATED
unsigned long lastTelemetryTime = 0;
int simDist = 20;
int simStep = 2;
#endif

// Búfer de recepción de tramas de telemetría desde la EDU-CIAA
static const size_t CIAA_RX_BUF_SIZE = 128;
static char   ciaaRxBuf[CIAA_RX_BUF_SIZE];
static size_t ciaaRxLen = 0;
static bool   ciaaRxOverflow = false;

// Orden de parada cinemática (v_lineal = 0, w_angular = 0)
static const char* TWIST_STOP   = "$TWIST,0.00,0.00";
static const char* TWIST_PREFIX = "$TWIST,";

// Prototipos de funciones exportadas por app_httpd.cpp
void startCameraServer();
void setupLedFlash(int pin);

// ============================================================================
// ENVÍO DE COMANDOS HACIA LA EDU-CIAA (único punto de escritura en SerialCIAA)
// ============================================================================
// Envía una trama $TWIST terminada en '\n'. Cualquier otra trama se descarta y
// se reporta por la consola de depuración, manteniendo la UART2 limpia.
static bool sendTwistToCIAA(const uint8_t* data, size_t len) {
  const size_t prefixLen = strlen(TWIST_PREFIX);
  if (len < prefixLen || memcmp(data, TWIST_PREFIX, prefixLen) != 0) {
    Serial.printf("[CIAA] Trama rechazada (no es $TWIST): %.*s\n", (int)len, (const char*)data);
    return false;
  }

  SerialCIAA.write(data, len);
  SerialCIAA.write('\n');
  return true;
}

static inline void sendStopToCIAA() {
  sendTwistToCIAA((const uint8_t*)TWIST_STOP, strlen(TWIST_STOP));
}

// ============================================================================
// RECEPCIÓN Y PARSEO DE TELEMETRÍA DESDE LA EDU-CIAA
// ============================================================================
static void handleCIAAFrame(char* line, size_t len) {
  if (len == 0) return;

  if (strncmp(line, "$SENS,", 6) == 0 ||
      strncmp(line, "$ODOM,", 6) == 0 ||
      strncmp(line, "$IMU,", 5) == 0) {
    webSocket.broadcastTXT(line, len);
  } else {
    Serial.printf("[CIAA] Trama desconocida: %s\n", line);
  }
}

static void pollCIAA() {
  while (SerialCIAA.available() > 0) {
    int c = SerialCIAA.read();
    if (c < 0) break;

    if (c == '\n') {
      if (ciaaRxOverflow) {
        Serial.println("[CIAA] Trama descartada por exceso de longitud");
      } else {
        // Quitar '\r' final si la EDU-CIAA envía CRLF
        if (ciaaRxLen > 0 && ciaaRxBuf[ciaaRxLen - 1] == '\r') ciaaRxLen--;
        ciaaRxBuf[ciaaRxLen] = '\0';
        handleCIAAFrame(ciaaRxBuf, ciaaRxLen);
      }
      ciaaRxLen = 0;
      ciaaRxOverflow = false;
      continue;
    }

    if (c == '$') {
      // Inicio de trama: resincroniza aunque la anterior haya quedado incompleta
      ciaaRxLen = 0;
      ciaaRxOverflow = false;
    }

    if (ciaaRxOverflow) continue;

    if (ciaaRxLen < CIAA_RX_BUF_SIZE - 1) {
      ciaaRxBuf[ciaaRxLen++] = (char)c;
    } else {
      ciaaRxOverflow = true;
    }
  }
}

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
      sendStopToCIAA();  // Failsafe: parada inmediata
      isMoving = false;
      break;

    case WStype_TEXT: {
      // Eliminar terminadores de línea que pudiera traer el cliente
      size_t len = length;
      while (len > 0 && (payload[len - 1] == '\n' || payload[len - 1] == '\r')) {
        len--;
      }

      // Comando propio de la ESP32 (no se reenvia a la EDU-CIAA): olvidar la red
      if (len == strlen(CMD_WIFI_RESET) && memcmp(payload, CMD_WIFI_RESET, len) == 0) {
        Serial.println("[WIFI] Borrando la red guardada y reiniciando en modo configuracion");
        webSocket.sendTXT(num, "$WIFI,OK");
        sendStopToCIAA();
        delay(300);
        WiFiManager wm;
        wm.resetSettings();
        ESP.restart();
      }
      lastCmdTime = millis();
      // Reenvío de la trama $TWIST recibida hacia la EDU-CIAA (UART2)
      if (sendTwistToCIAA(payload, len)) {
        // Solo se arma el watchdog si el comando no es ya una parada
        isMoving = !(len == strlen(TWIST_STOP) &&
                     memcmp(payload, TWIST_STOP, len) == 0);
      }
      break;
    }

    default:
      break;
  }
}
// Conecta a la red guardada o abre el portal de configuracion
static void conectarWifi() {
  WiFiManager wm;
  wm.setTitle("Rover - ESP32-CAM");
  wm.setConnectTimeout(WM_CONEXION_TIMEOUT);
  wm.setConfigPortalTimeout(WM_PORTAL_TIMEOUT);

  Serial.println();
  Serial.println("[WIFI] Conectando a la red guardada...");
  if (!wm.autoConnect(WM_AP_NOMBRE, WM_AP_CLAVE)) {
    // Nadie configuro la red a tiempo (o la guardada no esta): se reintenta todo
    Serial.println("[WIFI] Sin red. Reinicio para reintentar.");
    delay(1000);
    ESP.restart();
  }
  WiFi.setSleep(false);
}

void setup() {
  // UART0: depuracion / consola USB hacia la PC (los logs del core ESP32 van aqui)
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // UART2: canal dedicado a la EDU-CIAA (8N1, RX=GPIO13, TX=GPIO14)
  SerialCIAA.begin(CIAA_BAUD, SERIAL_8N1, CIAA_RX_PIN, CIAA_TX_PIN);
  sendStopToCIAA();   // al arrancar, el auto quieto

  Serial.println();
  Serial.println("\n--- ESP32-CAM AI-Thinker CameraWebServer (OV3660) ---");
  Serial.printf("[CIAA] UART%d iniciada a %d baudios (RX=GPIO%d, TX=GPIO%d)\n",
                CIAA_UART_NUM, CIAA_BAUD, CIAA_RX_PIN, CIAA_TX_PIN);

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

  // Conexión Wi-Fi (red guardada o portal de configuración)
  conectarWifi();

  // Nombre fijo en la red local: http://rovercam.local (sin buscar la IP)
  if (MDNS.begin(MDNS_NOMBRE)) {
    MDNS.addService("http", "tcp", 80);
  }

  Serial.println();
  Serial.println("====================================================");
  Serial.print("[WIFI] Conectado a ");
  Serial.println(WiFi.SSID());
  Serial.println("[INFO] Tambien por nombre: http://" MDNS_NOMBRE ".local");
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

  // Recepción y reenvío de telemetría real desde la EDU-CIAA ($SENS/$ODOM/$IMU)
  pollCIAA();

  unsigned long now = millis();

#if TELEMETRY_SIMULATED
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
#endif

  // Temporizador de seguridad (watchdog): más de 500 ms sin recibir comandos de texto
  if (isMoving && (now - lastCmdTime > 500)) {
    sendStopToCIAA();  // Failsafe: parada de emergencia hacia la EDU-CIAA
    Serial.println("[WDT] Sin comandos > 500 ms: parada enviada a la EDU-CIAA");
    isMoving = false;
  }
}
