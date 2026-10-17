// Delta ESP32: recibe la secuencia por MQTT, responde con el plan y va informando el estado.
// Librerias: PubSubClient (Nick O'Leary) y ArduinoJson v7.
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

WiFiClient net;
PubSubClient mqtt(net);
String tSeq   = String("delta/") + ID + "/secuencia";   // solo este delta
String tTodos = "delta/todos/secuencia";                // todos los deltas
String tPlan  = String("delta/") + ID + "/plan";
String tEst   = String("delta/") + ID + "/estado";
String tOn    = String("delta/") + ID + "/online";
JsonDocument prog;
bool run = false, busy = false;

void onMsg(char* t, byte* p, unsigned int n) {
  if (busy) {
    mqtt.publish(tEst.c_str(), "{\"punto\":0,\"estado\":\"ocupado\"}");
    return;
  }
  String raw;
  raw.reserve(n);
  for (unsigned int i = 0; i < n; i++) raw += (char)p[i];
  JsonDocument d;
  if (deserializeJson(d, raw)) return;
  prog = d;
  mqtt.publish(tPlan.c_str(), raw.c_str());  // "asi me voy a mover"
  run = true;
}

void conectar() {
  while (!mqtt.connected()) {
    String cid = String("delta-esp-") + ID;
    // si el ESP se cae, el broker publica online = 0
    if (mqtt.connect(cid.c_str(), NULL, NULL, tOn.c_str(), 1, true, "0")) {
      mqtt.publish(tOn.c_str(), "1", true);
      mqtt.subscribe(tSeq.c_str(), 1);
      mqtt.subscribe(tTodos.c_str(), 1);
    } else {
      delay(2000);
    }
  }
}

void setup() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);
  mqtt.setServer(BROKER, 1883);
  mqtt.setBufferSize(4096);  // el default de 256 bytes no alcanza
  mqtt.setCallback(onMsg);
}

void loop() {
  if (!mqtt.connected()) conectar();
  mqtt.loop();
  if (run) {
    run = false;
    busy = true;
    int i = 0;
    for (JsonObject p : prog["puntos"].as<JsonArray>()) {
      i++;
      // aca mover el delta a p["x"], p["y"], p["z"] y ejecutar p["a"] (agarrar/soltar)
      String s = "{\"punto\":" + String(i) + ",\"estado\":\"moviendo\"}";
      mqtt.publish(tEst.c_str(), s.c_str());
      delay(1000);
      mqtt.loop();
    }
    mqtt.publish(tEst.c_str(), "{\"punto\":0,\"estado\":\"listo\"}");
    busy = false;
  }
}
