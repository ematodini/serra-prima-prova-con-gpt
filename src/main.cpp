#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <DHT.h>
#include <DNSServer.h>
#include <LittleFS.h>

#define DHTPIN D2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Include del file di secrets (non tracciato)
#include "secrets.h" // ssid/password in include/, NON committare

#define RELAY_PIN D0
#define LED_PIN LED_BUILTIN

ESP8266WebServer server(80);
DNSServer dnsServer;

// default 5 secondi irrigazione, 5 minuti intervallo
unsigned long tempoIrrigazione = 5000;
unsigned long tempoIntervallo = 5 * 60UL * 1000UL; // 5 minuti di default

unsigned long tempoInizioIrrigazione = 0;
bool irrigazioneInCorso = false;

// ciclo automatico tra irrigazioni (attivo di default)
bool cicloAutomatico = true;
unsigned long nextStartTime = 0; // millis quando partire la prossima irrigazione

unsigned long lastSensorMillis = 0;
const unsigned long sensorInterval = 8000;
float lastTemp = NAN;
float lastHum = NAN;

void leggiTempiDaEEPROM() {
  EEPROM.begin(512);
  unsigned long v1 = 0, v2 = 0;
  for (unsigned i = 0; i < sizeof(tempoIrrigazione); ++i) {
    v1 |= ((unsigned long)EEPROM.read(i)) << (8 * i);
  }
  for (unsigned i = 0; i < sizeof(tempoIntervallo); ++i) {
    v2 |= ((unsigned long)EEPROM.read(i + sizeof(tempoIrrigazione))) << (8 * i);
  }
  if (v1 >= 1000 && v1 <= 3600000) tempoIrrigazione = v1;
  if (v2 >= 60000 && v2 <= 86400000UL) tempoIntervallo = v2; // v2 in ms, minimo 1 min
  Serial.printf("Tempo irrigazione EEPROM: %u s\n", (unsigned)(tempoIrrigazione/1000));
  Serial.printf("Intervallo EEPROM: %u min\n", (unsigned)(tempoIntervallo/60000));
}

void salvaIntervalloSuEEPROM(unsigned long t) {
  EEPROM.begin(512);
  for (unsigned i = 0; i < sizeof(tempoIntervallo); ++i) {
    EEPROM.write(i + sizeof(tempoIrrigazione), (t >> (8 * i)) & 0xFF);
  }
  EEPROM.commit();
  tempoIntervallo = t;
  Serial.printf("Salvato intervallo: %u s\n", (unsigned)(tempoIntervallo/1000));
}

void salvaTempoSuEEPROM(unsigned long t) {
  EEPROM.begin(512);
  for (unsigned i = 0; i < sizeof(tempoIrrigazione); ++i) {
    EEPROM.write(i, (t >> (8 * i)) & 0xFF);
  }
  EEPROM.commit();
  tempoIrrigazione = t;
  Serial.printf("Salvato tempo irrigazione: %u s\n", (unsigned)(tempoIrrigazione/1000));
}

void startIrrigation() {
  // avvia/riavvia irrigazione (sovrascrive se già in corso)
  irrigazioneInCorso = true;
  tempoInizioIrrigazione = millis();
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_PIN, LOW);
  Serial.println("Irrigazione avviata");
}

void stopIrrigation() {
  if (irrigazioneInCorso) {
    irrigazioneInCorso = false;
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_PIN, HIGH);
    Serial.println("Irrigazione stoppata");
  }
}

void verificaStopAutomatico() {
  if (irrigazioneInCorso && millis() - tempoInizioIrrigazione >= tempoIrrigazione) {
    stopIrrigation();
    if (cicloAutomatico) {
      // programma la prossima partenza dopo l'intervallo
      nextStartTime = millis() + tempoIntervallo;
      Serial.printf("Prossima irrigazione prevista tra %u s\n", (unsigned)(tempoIntervallo/1000));
    } else {
      nextStartTime = 0;
    }
  }
}

/* --- Web handlers --- */

void handleRoot() {
  // (se usi LittleFS statici non usi più handleRoot; lasciare per fallback)
  String page = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Serra - Control</title>
<style>body{font-family:Arial;margin:10px;color:#fff;background:#222}button{padding:8px 12px;margin:6px}input{padding:6px}</style>
</head><body>
<h2>Serra - Controllo</h2>
<div id="status">Caricamento...</div>
</body></html>
)rawliteral";
  server.send(200, "text/html; charset=utf-8", page);
}

void handleStatus() {
  // assicurati di avere letture recenti
  if (isnan(lastTemp) || isnan(lastHum)) {
    lastTemp = dht.readTemperature();
    lastHum = dht.readHumidity();
    if (isnan(lastTemp) || isnan(lastHum)) { lastTemp = 0; lastHum = 0; }
  }

  // calcola secondi rimanenti alla prossima irrigazione (in secondi)
  unsigned long now = millis();
  unsigned long next_in_ms = 0;
  if (cicloAutomatico) {
    if (irrigazioneInCorso) {
      unsigned long planned = tempoInizioIrrigazione + tempoIrrigazione + tempoIntervallo;
      next_in_ms = (planned > now) ? (planned - now) : 0;
    } else if (nextStartTime != 0) {
      next_in_ms = (nextStartTime > now) ? (nextStartTime - now) : 0;
    } else {
      // se non programmato, indica l'intervallo totale (utile all'avvio)
      next_in_ms = tempoIntervallo;
    }
  } else {
    next_in_ms = 0;
  }

  String json = "{";
  json += "\"temperature\":" + String(lastTemp,1) + ",";
  json += "\"humidity\":" + String(lastHum,0) + ",";
  json += "\"irrigation\":" + String(irrigazioneInCorso ? "true" : "false") + ",";
  json += "\"irrigation_time\":" + String(tempoIrrigazione) + ",";
  json += "\"irrigation_interval\":" + String(tempoIntervallo) + ",";
  json += "\"next_in\":" + String((unsigned long)(next_in_ms / 1000));
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}

void handleStart() {
  // avvia il ciclo ripetuto: esegui subito la prima irrigazione e abilita cicloAutomatico
  cicloAutomatico = true;
  startIrrigation();
  nextStartTime = 0;
  server.send(200, "text/plain", "OK");
}
void handleStop() {
  // ferma ciclo e irrigazione corrente
  cicloAutomatico = false;
  nextStartTime = 0;
  stopIrrigation();
  server.send(200, "text/plain", "OK");
}
void handleSetTime() {
  if (server.hasArg("time")) {
    unsigned long t = (unsigned long)server.arg("time").toInt() * 1000UL;
    if (t >= 1000 && t <= 3600000) {
      salvaTempoSuEEPROM(t);

      // ricalcola immediatamente il prossimo avvio in base al nuovo tempo
      if (irrigazioneInCorso) {
        // la prossima irrigazione (dopo la fine di quella corrente) dipende dal nuovo tempo
        nextStartTime = tempoInizioIrrigazione + tempoIrrigazione + tempoIntervallo;
      } else if (cicloAutomatico) {
        // se non siamo in irrigazione, rimetti il prossimo start in tempoIntervallo da ora
        nextStartTime = millis() + tempoIntervallo;
      }

      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Invalid");
}
void handleSetInterval() {
  if (server.hasArg("interval")) {
    unsigned long minutes = (unsigned long)server.arg("interval").toInt();
    unsigned long t = minutes * 60UL * 1000UL;
    if (t >= 60000 && t <= 86400000UL) {
      salvaIntervalloSuEEPROM(t);

      // ricalcola immediatamente il prossimo avvio in base al nuovo intervallo
      if (irrigazioneInCorso) {
        nextStartTime = tempoInizioIrrigazione + tempoIrrigazione + tempoIntervallo;
      } else if (cicloAutomatico) {
        nextStartTime = millis() + tempoIntervallo;
      }

      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Invalid");
}

String getContentType(const String &path) {
  if (path.endsWith(".htm") || path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg")) return "image/jpeg";
  if (path.endsWith(".ico")) return "image/x-icon";
  return "text/plain; charset=utf-8";
}

/* --- setup/loop --- */

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH); // LED spento (LED integrato è attivo basso)
  dht.begin();

  leggiTempiDaEEPROM();

  // Avvia solo Access Point per configurazione locale
  WiFi.mode(WIFI_AP);
  const char* apSSID = "Automazione Serra";
  const char* apPass = nullptr;
  if (apPass && strlen(apPass) >= 8) {
    WiFi.softAP(apSSID, apPass);
  } else {
    WiFi.softAP(apSSID);
  }

  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("AP avviato: %s  IP: %s\n", apSSID, apIP.toString().c_str());

  if (!LittleFS.begin()) {
    Serial.println("Errore montando LittleFS");
  } else {
    Serial.println("LittleFS montato");
    Serial.println("Listing LittleFS root:");
    Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
      Serial.printf(" - %s  (%u bytes)\n", dir.fileName().c_str(), dir.fileSize());
    }
    Serial.println("End listing");
  }

  // serve file statici espliciti
  server.serveStatic("/index.html", LittleFS, "/index.html", "text/html; charset=utf-8");
  server.serveStatic("/style.css", LittleFS, "/style.css", "text/css; charset=utf-8");
  server.serveStatic("/app.js", LittleFS, "/app.js", "application/javascript; charset=utf-8");

  // root handler
  server.on("/", HTTP_GET, [](){
    Serial.println("GET /  -> serve /index.html");
    if (LittleFS.exists("/index.html")) {
      File f = LittleFS.open("/index.html", "r");
      server.streamFile(f, "text/html");
      f.close();
    } else {
      server.send(500, "text/plain", "index.html non presente su LittleFS");
    }
  });

  // API
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/set-time", HTTP_POST, handleSetTime);
  server.on("/set-interval", HTTP_POST, handleSetInterval);

  // onNotFound e captive endpoints (come già hai)
  server.onNotFound([](){
    String path = server.uri();
    Serial.printf("NotFound request URI: %s  Host: %s\n", path.c_str(), server.hostHeader().c_str());
    if (path == "/") path = "/index.html";
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      server.streamFile(f, getContentType(path));
      f.close();
      return;
    }
    if (LittleFS.exists("/index.html")) {
      File f = LittleFS.open("/index.html", "r");
      server.streamFile(f, "text/html");
      f.close();
      return;
    }
    server.send(404, "text/plain", "Not found");
  });

  server.on("/generate_204", HTTP_GET, [](){ server.send(204, "text/plain", ""); });
  server.on("/hotspot-detect.html", HTTP_GET, [](){ server.send(200, "text/html", ""); });
  server.on("/ncsi.txt", HTTP_GET, [](){ server.send(200, "text/plain", "Microsoft NCSI"); });
  server.on("/connecttest.txt", HTTP_GET, [](){ server.send(200, "text/plain", ""); });

  dnsServer.start(53, "*", WiFi.softAPIP());

  server.begin();
  // programma prima esecuzione automatica
  nextStartTime = millis() + tempoIntervallo;
  Serial.println("Server avviato (solo AP) con LittleFS");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  // aggiorna sensori periodicamente
  if (millis() - lastSensorMillis >= sensorInterval) {
    lastSensorMillis = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) { lastTemp = t; lastHum = h; }
    Serial.printf("Temp: %.1f C  Hum: %.0f %%\n", lastTemp, lastHum);
  }

  verificaStopAutomatico();

  // se ciclo automatico attivo e non siamo in irrigazione, verifica avvio prossimo ciclo
  if (!irrigazioneInCorso && cicloAutomatico && nextStartTime != 0 && millis() >= nextStartTime) {
    startIrrigation();
    nextStartTime = 0;
  }

  delay(10);
}