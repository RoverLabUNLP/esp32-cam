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

## Conexión con la EDU-CIAA

La comunicación con la EDU-CIAA va por la **UART2** en GPIO13 / GPIO14 (esquemático del auto, hoja 2). La UART0 (U0T / U0R) queda solo para programar y para los mensajes de depuración (`Serial.print`), así no le llegan a la EDU-CIAA.

| ESP32-CAM | EDU-CIAA | Notas |
|---|---|---|
| **GPIO 13** (RX) | **RS232_TXD** | Cruzado |
| **GPIO 14** (TX) | **RS232_RXD** | Cruzado |
| **GND** | **GND** | Masa común (usar el GND que está al lado del pin 5V) |
| **5V** | — | Fuente propia de 5 V (LM2596), no desde la EDU-CIAA |

115200 bps, 8N1. En el código: `CIAA_RX_PIN`, `CIAA_TX_PIN` y `CIAA_BAUD`.

No conectar a nada IO0, IO2, IO12 ni IO15: definen cómo arranca la ESP32 (con IO12 en alto al encender, no bootea).

## Configurar la red Wi-Fi (sin tocar el código)

El firmware usa **WiFiManager**: la red no está escrita en el código y `wifi_credentials.h` ya no se usa.

1. Al arrancar intenta conectarse a la **última red guardada** (hasta 20 s).
2. Si no la encuentra, la ESP32 crea su propia red:
   - **Nombre:** `RoverCam-Config`
   - **Clave:** `rover2026`
3. Conectarse a esa red desde el celular o la PC. Se abre sola una página; si no, entrar a `http://192.168.4.1`.
4. Elegir **Configure WiFi**, seleccionar la red, poner la clave y guardar. La ESP32 se conecta y la red queda guardada para los próximos arranques.
5. Si nadie configura nada en 3 minutos, se reinicia y vuelve a probar la red guardada.

**Cambiar de red estando conectada:** mandar el texto `$WIFI,RESET` por el WebSocket (puerto 82). La ESP32 contesta `$WIFI,OK`, borra la red guardada y se reinicia en modo configuración (paso 2). Este comando no se reenvía a la EDU-CIAA.

**Sin buscar la IP:** una vez conectada, el panel también responde en `http://rovercam.local` (en Windows hace falta tener el servicio mDNS/Bonjour, que suele venir instalado). La IP igual sale por el monitor serie.

Nombre de la red, clave y tiempos se cambian en los `#define WM_...` de `esp32_cam_stream.ino`.

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
3. Instalar las librerías **WebSockets** (Markus Sattler / links2004) y **WiFiManager** (tzapu). Con PlatformIO se instalan solas (`lib_deps` en `platformio.ini`).

## Endpoints Disponibles

- `GET http://<IP_LOCAL>/`: Página web moderna con reproductor responsivo.
- `GET http://<IP_LOCAL>/stream`: Flujo MJPEG continuo (`multipart/x-mixed-replace`).
