# ESP32-CAM (AI-Thinker) MJPEG Video Streaming

Firmware completo en C++ para el módulo ESP32-CAM AI-Thinker con sensor Omnivision OV2640, que implementa un servidor de streaming de video en vivo (MJPEG) sobre HTTP.

## Estructura del Proyecto

- `esp32_cam_stream.ino`: Código principal de inicialización, Wi-Fi, servidor HTTP nativo (`esp_http_server.h`) y bucle de streaming.
- `camera_pins.h`: Definición de pines GPIO para el modelo AI-Thinker.
- `platformio.ini`: Configuración lista para compilar con PlatformIO / VS Code.

## Conexiones para Programar con Conversor USB-TTL (FTDI)

El módulo ESP32-CAM no cuenta con chip USB-Serial integrado, por lo que se requiere un módulo conversor (FTDI, CP2102 o CH340):

| FTDI / Adaptador USB | ESP32-CAM | Notas |
|---|---|---|
| **5V** | **5V** | Se recomienda alimentar con 5V / 2A estables |
| **GND** | **GND** | Masa común |
| **TX** | **U0R (GPIO 3)** | Transmisión hacia el receptor del ESP32 |
| **RX** | **U0T (GPIO 1)** | Recepción desde el transmisor del ESP32 |
| — | **GPIO 0 a GND** | **Puente obligatorio durante el flasheo**. Desconectar para ejecutar el firmware |

> **Nota:** Al terminar la carga, desconecta el puente entre **GPIO 0** y **GND**, y presiona el botón **RST** de la placa.

## Configuración en Arduino IDE

1. Instalar el paquete de placas **esp32** de Espressif en el Gestor de Tarjetas (`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`).
2. Configurar el menú **Herramientas**:
   - **Placa (Board):** `AI Thinker ESP32-CAM`
   - **CPU Frequency:** `240MHz (WiFi/BT)`
   - **Flash Frequency:** `80MHz`
   - **Flash Mode:** `QIO`
   - **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)`
   - **PSRAM:** `Enabled`
   - **Upload Speed:** `115200` o `921600`
   - **Puerto:** Seleccionar el puerto COM asignado al FTDI.

## Endpoints Disponibles

- `GET http://<IP_LOCAL>/`: Página web moderna con reproductor responsivo.
- `GET http://<IP_LOCAL>/stream`: Flujo MJPEG continuo (`multipart/x-mixed-replace`).
