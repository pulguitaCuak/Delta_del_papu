/*
  DELTA - COMUNICACION MQTT BIDIRECCIONAL
  Basado en delta_esp32_monitor.ino y compatible con el protocolo del servidor Delta.

  Publica:
    delta/N/online  -> "1" al conectar; "0" como Last Will si pierde conexión
    delta/N/estado  -> JSON periódico para el dashboard
    delta/N/evento  -> disponible para eventos reales del robot (ver publicarEvento)

  Recibe:
    delta/N/secuencia       -> secuencia dirigida a este Delta
    delta/todos/secuencia   -> secuencia para todos
    delta/todos/comando     -> comandos globales, por ejemplo "parar"
    delta/N/comando         -> comandos dirigidos a este Delta

  IMPORTANTE:
  Este archivo es una base de comunicación/prueba, no reemplaza el firmware que controla
  motores y sensores. En procesarComando() y obtenerEstadoRobot() hay que integrar las
  funciones/variables reales del firmware del robot para ejecutar comandos y reportar
  punto, estado, sensores, ciclos, agarres, etc.

  Requiere: PubSubClient y ArduinoJson 7.x.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ---------------- CONFIGURACION ----------------
static const char* WIFI_SSID = "TU_WIFI";
static const char* WIFI_PASS = "TU_CLAVE";
static const char* MQTT_HOST = "192.168.1.100"; // IP de la Raspberry Pi
static const uint16_t MQTT_PORT = 1883;
static const uint8_t DELTA_ID = 1;              // CAMBIAR: 1, 2, 3...
static const unsigned long INTERVALO_ESTADO_MS = 5000;
static const unsigned long INTERVALO_ONLINE_MS = 15000;

// ------------------------------------------------
WiFiClient wifi;
PubSubClient mqtt(wifi);
char clientId[32];
char topicEstado[40];
char topicOnline[40];
char topicEvento[40];
char topicSecuencia[40];
char topicComando[40];
char topicWill[40];
unsigned long ultimoEstado = 0;
unsigned long ultimoOnline = 0;
unsigned long ultimoIntento = 0;
unsigned long mensajesRx = 0;

// Estado provisional hasta conectar esto con las variables reales del robot.
// No inventa lecturas de sensores: solo informa que el firmware de comunicación está activo.
static const char* obtenerEstadoRobot() {
  return "telemetria_activa";
}
static int obtenerPuntoActual() {
  return 0; // TODO: devolver el punto real del recorrido, si corresponde
}

void publicarEstado() {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  doc["estado"] = obtenerEstadoRobot();
  doc["punto"] = obtenerPuntoActual();
  doc["uptime_ms"] = millis();
  doc["wifi_rssi_dbm"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();

  char payload[384];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n == 0 || n >= sizeof(payload)) {
    Serial.println("[TX] No se pudo serializar estado");
    return;
  }

  if (mqtt.publish(topicEstado, payload, false)) {
    Serial.printf("[TX] %s: %s\n", topicEstado, payload);
  } else {
    Serial.println("[TX] Error publicando estado");
  }
}

void publicarEvento(const char* tipo, int punto, const char* detalle,
                    unsigned long ciclos = 0, unsigned long agarres = 0) {
  if (!mqtt.connected()) return;
  JsonDocument doc;
  doc["tipo"] = tipo;
  if (punto >= 0) doc["punto"] = punto;
  if (detalle && detalle[0]) doc["detalle"] = detalle;
  doc["ms"] = millis();
  doc["ciclos"] = ciclos;
  doc["agarres"] = agarres;

  char payload[384];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n && n < sizeof(payload)) {
    if (mqtt.publish(topicEvento, payload, false)) {
      Serial.printf("[TX] %s: %s\n", topicEvento, payload);
    } else {
      Serial.println("[TX] Error publicando evento");
    }
  }
}

// Conectar esta función con la lógica real de motores/robot.
void procesarComando(const String& comando) {
  Serial.printf("[COMANDO] recibido: %s\n", comando.c_str());
  if (comando == "parar") {
    Serial.println("[ATENCION] Comando PARAR recibido; falta vincularlo con la funcion real de parada del robot.");
    // TODO SEGURIDAD: llamar aquí a la función real que detiene los motores.
  }
}

void procesarSecuencia(const char* topic, const byte* payload, unsigned int len) {
  Serial.printf("[SECUENCIA] recibida en %s (%u bytes)\n", topic, len);
  // El formato esperado por la interfaz es JSON: {"puntos":[...],"lazo":true/false}.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("[SECUENCIA] JSON invalido: %s\n", err.c_str());
    return;
  }
  JsonArray puntos = doc["puntos"].as<JsonArray>();
  Serial.printf("[SECUENCIA] puntos=%u, lazo=%s\n",
                (unsigned)puntos.size(), doc["lazo"] | false ? "true" : "false");
  serializeJsonPretty(doc, Serial);
  Serial.println();
  // TODO: entregar los puntos a la cola/planificador real del robot.
}

void alRecibir(char* topic, byte* payload, unsigned int len) {
  mensajesRx++;
  Serial.printf("\n[RX #%lu] %s (%u bytes): ", mensajesRx, topic, len);
  for (unsigned int i = 0; i < len; i++) Serial.write(payload[i]);
  Serial.println();

  String t(topic);
  if (t == topicSecuencia || t == "delta/todos/secuencia") {
    procesarSecuencia(topic, payload, len);
    return;
  }

  if (t == topicComando || t == "delta/todos/comando") {
    String comando;
    comando.reserve(len);
    for (unsigned int i = 0; i < len; i++) comando += (char)payload[i];
    comando.trim();
    procesarComando(comando);
  }
}

void suscribirTopics() {
  char topicTodosSecuencia[] = "delta/todos/secuencia";
  char topicTodosComando[] = "delta/todos/comando";
  mqtt.subscribe(topicSecuencia, 1);
  mqtt.subscribe(topicTodosSecuencia, 1);
  mqtt.subscribe(topicComando, 1);
  mqtt.subscribe(topicTodosComando, 1);
  Serial.printf("[MQTT] Suscrito a %s, %s, %s y %s\n",
                topicSecuencia, topicTodosSecuencia, topicComando, topicTodosComando);
}

void intentarConectar() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - ultimoIntento >= 8000) {
      ultimoIntento = millis();
      Serial.println("[WiFi] Reintentando conexion...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }
  if (mqtt.connected() || millis() - ultimoIntento < 5000) return;
  ultimoIntento = millis();

  Serial.printf("[MQTT] Conectando a %s:%u...\n", MQTT_HOST, MQTT_PORT);
  // Last Will retenido: si el ESP32 se apaga o pierde MQTT inesperadamente, broker publica 0.
  if (mqtt.connect(clientId, topicWill, 1, true, "0")) {
    Serial.println("[MQTT] Conectado");
    mqtt.publish(topicOnline, "1", true);
    Serial.printf("[TX] %s: 1\n", topicOnline);
    suscribirTopics();
    ultimoEstado = millis() - INTERVALO_ESTADO_MS;
    ultimoOnline = millis();
  } else {
    Serial.printf("[MQTT] Error %d; se reintentara\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  snprintf(clientId, sizeof(clientId), "delta-%u-%04X", DELTA_ID,
           (unsigned)((ESP.getEfuseMac() >> 16) & 0xFFFF));
  snprintf(topicEstado, sizeof(topicEstado), "delta/%u/estado", DELTA_ID);
  snprintf(topicOnline, sizeof(topicOnline), "delta/%u/online", DELTA_ID);
  snprintf(topicEvento, sizeof(topicEvento), "delta/%u/evento", DELTA_ID);
  snprintf(topicSecuencia, sizeof(topicSecuencia), "delta/%u/secuencia", DELTA_ID);
  snprintf(topicComando, sizeof(topicComando), "delta/%u/comando", DELTA_ID);
  snprintf(topicWill, sizeof(topicWill), "delta/%u/online", DELTA_ID);

  Serial.printf("\nDELTA %u - MQTT bidireccional\n", DELTA_ID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(alRecibir);
  mqtt.setBufferSize(4096);
  mqtt.setSocketTimeout(3);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] Todavia sin conexion; se reintentara en loop");
  }
  ultimoIntento = millis() - 5000;
  intentarConectar();
}

void loop() {
  intentarConectar();
  mqtt.loop();

  unsigned long ahora = millis();
  if (mqtt.connected() && ahora - ultimoEstado >= INTERVALO_ESTADO_MS) {
    ultimoEstado = ahora;
    publicarEstado();
  }
  // Reafirma online periodicamente. No genera eventos repetidos en el servidor si sigue en 1.
  if (mqtt.connected() && ahora - ultimoOnline >= INTERVALO_ONLINE_MS) {
    ultimoOnline = ahora;
    mqtt.publish(topicOnline, "1", true);
  }
}
