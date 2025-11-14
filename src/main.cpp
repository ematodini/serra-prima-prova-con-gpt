#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <DHT.h>
#include <DNSServer.h>

#define DHTPIN D2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Include del file di secrets (non tracciato)
#include "secrets.h" // ssid/password in include/, NON committare

#define RELAY_PIN D0
#define LED_PIN LED_BUILTIN

ESP8266WebServer server(80);
DNSServer dnsServer;

// default 5 secondi
unsigned long tempoIrrigazione = 5000;
unsigned long tempoInizioIrrigazione = 0;
bool irrigazioneInCorso = false;

unsigned long lastSensorMillis = 0;
const unsigned long sensorInterval = 8000;
float lastTemp = NAN;
float lastHum = NAN;

void leggiTempoDaEEPROM() {
  EEPROM.begin(512);
  unsigned long v = 0;
  for (unsigned i = 0; i < sizeof(tempoIrrigazione); ++i) {
    v |= ((unsigned long)EEPROM.read(i)) << (8 * i);
  }
  if (v >= 1000 && v <= 3600000) tempoIrrigazione = v;
  Serial.printf("Tempo irrigazione EEPROM: %u s\n", (unsigned)(tempoIrrigazione/1000));
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
  if (!irrigazioneInCorso) {
    irrigazioneInCorso = true;
    tempoInizioIrrigazione = millis();
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(LED_PIN, LOW);
    Serial.println("Irrigazione avviata");
  }
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
  }
}

/* --- Web handlers --- */

void handleRoot() {
  String page = R"rawliteral(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Serra - Control</title>
<style>body{font-family:Arial;margin:10px;color:#fff;background:#222}button{padding:8px 12px;margin:6px}input{padding:6px}</style>
</head><body>
<h2>Serra - Controllo</h2>
<div id="status">Caricamento...</div>
<button onclick="doStart()">Avvia</button>
<button onclick="doStop()">Ferma</button>
<br><br>
<label>Tempo irrigazione (s): <input id="time" type="number" min="1" max="3600"></label>
<button onclick="saveTime()">Salva</button>
<p id="msg"></p>
<script>
async function fetchStatus(){
  try{
    let r = await fetch('/status'); let j = await r.json();
    document.getElementById('status').innerHTML =
      'Temp: '+j.temperature.toFixed(1)+' °C &nbsp; Umid: '+j.humidity.toFixed(0)+' % <br>' +
      'Irrigazione: ' + (j.irrigation ? 'ON' : 'OFF') + ' &nbsp; Tempo impostato: '+ (j.irrigation_time/1000)+' s';
    let timeInput = document.getElementById('time');
    if (document.activeElement !== timeInput) {
      timeInput.value = j.irrigation_time/1000;
    }
  }catch(e){ document.getElementById('status').innerText='Errore connessione'; }
}
async function doStart(){ await fetch('/start',{method:'POST'}); fetchStatus(); }
async function doStop(){ await fetch('/stop',{method:'POST'}); fetchStatus(); }
async function saveTime(){
  let t = parseInt(document.getElementById('time').value)||5;
  await fetch('/set-time', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'time=' + encodeURIComponent(t)
  });
  document.getElementById('msg').innerText='Salvato';
  setTimeout(()=>document.getElementById('msg').innerText='',1500);
  fetchStatus();
}
setInterval(fetchStatus,3000);
fetchStatus();
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", page);
}

void handleStatus() {
  // assicurati di avere letture recenti
  if (isnan(lastTemp) || isnan(lastHum)) {
    lastTemp = dht.readTemperature();
    lastHum = dht.readHumidity();
    if (isnan(lastTemp) || isnan(lastHum)) { lastTemp = 0; lastHum = 0; }
  }
  String json = "{";
  json += "\"temperature\":" + String(lastTemp,1) + ",";
  json += "\"humidity\":" + String(lastHum,0) + ",";
  json += "\"irrigation\":" + String(irrigazioneInCorso ? "true" : "false") + ",";
  json += "\"irrigation_time\":" + String(tempoIrrigazione);
  json += "}";
  server.send(200, "application/json", json);
}

void handleStart() {
  startIrrigation();
  server.send(200, "text/plain", "OK");
}
void handleStop() {
  stopIrrigation();
  server.send(200, "text/plain", "OK");
}
void handleSetTime() {
  if (server.hasArg("time")) {
    unsigned long t = (unsigned long)server.arg("time").toInt() * 1000UL;
    if (t >= 1000 && t <= 3600000) {
      salvaTempoSuEEPROM(t);
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Invalid");
}

/* --- setup/loop --- */

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH); // LED spento (LED integrato è attivo basso)
  dht.begin();

  leggiTempoDaEEPROM();

  // Avvia solo Access Point per configurazione locale
  WiFi.mode(WIFI_AP);
  const char* apSSID = "Automazione Serra";
  // Se vuoi proteggere l'AP, sostituisci nullptr con una password (min 8 char)
  const char* apPass = nullptr; // es. "mypass123"
  if (apPass && strlen(apPass) >= 8) {
    WiFi.softAP(apSSID, apPass);
  } else {
    WiFi.softAP(apSSID);
  }

  IPAddress apIP = WiFi.softAPIP(); // di solito 192.168.4.1
  Serial.printf("AP avviato: %s  IP: %s\n", apSSID, apIP.toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/set-time", HTTP_POST, handleSetTime);

  // captive portal endpoints per Android/iOS/Windows
  server.on("/generate_204", HTTP_GET, [](){ server.send(204, "text/plain", ""); });
  server.on("/hotspot-detect.html", HTTP_GET, [](){ server.send(200, "text/html", ""); });
  server.on("/ncsi.txt", HTTP_GET, [](){ server.send(200, "text/plain", "Microsoft NCSI"); });
  server.on("/connecttest.txt", HTTP_GET, [](){ server.send(200, "text/plain", ""); });

  // avvia DNS catch-all: risponde con IP dell'AP per ogni hostname
  dnsServer.start(53, "*", apIP);

  server.begin();
  Serial.println("Server avviato (solo AP)");
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
  delay(10);
}