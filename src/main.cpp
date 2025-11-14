#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <DHT.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>

#define DHTPIN D2         // Pin a cui è collegato il sensore DHT
#define DHTTYPE DHT22     // Tipo di sensore DHT
DHT dht(DHTPIN, DHTTYPE);

// rimuovi o commenta queste linee:
// const char* ssid = "LillifyFast";       // Nome della rete Wi-Fi
// const char* password = "lillifyfast";   // Password della rete Wi-Fi

// aggiungi include del file di secrets (non tracciato)
#include "secrets.h"

const char* serverIP = "192.168.1.209"; // Indirizzo IP del Raspberry Pi
const int serverPort = 5000;            // Porta del server Flask

unsigned long tempoInizioIrrigazione = 0;
bool statoIrrigazione = false;

// Durata dell'irrigazione in millisecondi, default 5 secondi
unsigned long tempoIrrigazione = 5000;

// Soglia di luminosità in percentuale, default 30%
unsigned int sogliaLuminosita = 30;

// Pin collegato all'elettrovalvola
#define RELAY_PIN D0
#define LED_PIN LED_BUILTIN  // LED integrato nel NodeMCU

unsigned long tempoUltimaMisura = 0;
const long intervalloMisuraSensori = 10000;  // 10 secondi per il monitoraggio dei sensori

WiFiClient wifiClient;
ESP8266WebServer server(80); // Crea un server che ascolta sulla porta 80

// Funzione per leggere il tempo di irrigazione dalla EEPROM
void leggiTempoIrrigazioneDaEEPROM() {
  EEPROM.begin(512);
  unsigned long eepromVal = 0;
  for (unsigned int i = 0; i < sizeof(tempoIrrigazione); i++) {
    eepromVal |= EEPROM.read(i) << (8 * i);
  }

  if (eepromVal > 0 && eepromVal <= 3600000) {  // Limiti tra 1 secondo e 1 ora
    tempoIrrigazione = eepromVal;
  }

  Serial.print("Tempo irrigazione letto da EEPROM: ");
  Serial.println(tempoIrrigazione / 1000);
}

// Funzione per salvare il tempo di irrigazione nella EEPROM
void salvaTempoIrrigazioneSuEEPROM(unsigned long nuovoTempo) {
  EEPROM.begin(512);
  for (unsigned int i = 0; i < sizeof(tempoIrrigazione); i++) {
    EEPROM.write(i, (nuovoTempo >> (8 * i)) & 0xFF);
  }
  EEPROM.commit();
  tempoIrrigazione = nuovoTempo;
  Serial.print("Nuovo tempo di irrigazione salvato: ");
  Serial.println(tempoIrrigazione / 1000);
}

// Funzione per leggere la soglia di luminosità dalla EEPROM
void leggiSogliaLuminositaDaEEPROM() {
  EEPROM.begin(512);
  unsigned int eepromVal = EEPROM.read(sizeof(tempoIrrigazione));

  if (eepromVal >= 0 && eepromVal <= 100) {  // Limiti validi per la soglia di luminosità
    sogliaLuminosita = eepromVal;
  }

  Serial.print("Soglia di luminosità letta da EEPROM: ");
  Serial.println(sogliaLuminosita);
}

// Funzione per salvare la soglia di luminosità nella EEPROM
void salvaSogliaLuminositaSuEEPROM(unsigned int nuovaSoglia) {
  EEPROM.begin(512);
  EEPROM.write(sizeof(tempoIrrigazione), nuovaSoglia & 0xFF);  // Salva la soglia come un byte
  EEPROM.commit();
  sogliaLuminosita = nuovaSoglia;
  Serial.print("Nuova soglia di luminosità salvata: ");
  Serial.println(sogliaLuminosita);
}

// Funzione per leggere la luminosità e mappare il valore in una percentuale
int leggiLuminosita() {
  int valoreLDR = analogRead(A0);  // Legge il valore analogico dal pin A0
  int luminositaPercentuale = map(valoreLDR, 0, 1023, 0, 100);  // Mappa il valore tra 0 e 100
  Serial.print("Luminosità letta (percentuale): ");
  Serial.println(luminositaPercentuale);
  return luminositaPercentuale;
}

// Funzione per inviare i dati di temperatura, umidità e luminosità al server
void sendDataToServer(float temp, float hum, int luminosity) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(wifiClient, String("http://") + serverIP + ":" + String(serverPort) + "/data");
    String postData = "temperature=" + String(temp) + "&humidity=" + String(hum) + "&luminosity=" + String(luminosity);

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(postData);

    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println("Dati inviati: " + payload);
    } else {
      Serial.println("Errore durante la connessione al server");
    }

    http.end();
  } else {
    Serial.println("Connessione Wi-Fi non disponibile.");
  }
}

// Funzione per verificare lo stato dell'irrigazione e spegnerla dopo il tempo definito
void verificaIrrigazione() {
  if (statoIrrigazione && millis() - tempoInizioIrrigazione >= tempoIrrigazione) {
    statoIrrigazione = false;
    digitalWrite(RELAY_PIN, LOW);   // Disattiva l'elettrovalvola
    digitalWrite(LED_PIN, HIGH);    // Spegni il LED integrato
    Serial.println("Irrigazione disattivata");
  }
}

// Funzione per attivare l'irrigazione con controllo sulla soglia di luminosità
void attivaIrrigazione() {
  unsigned int luminosita = leggiLuminosita();  // Legge la luminosità attuale

  if (luminosita < sogliaLuminosita) {
    Serial.println("Luminosità troppo bassa. Irrigazione inibita.");
    return;  // Esce dalla funzione se la luminosità è inferiore alla soglia
  }

  Serial.println("Entrato in attivaIrrigazione()");
  tempoInizioIrrigazione = millis();
  statoIrrigazione = true;
  digitalWrite(RELAY_PIN, HIGH);   // Attiva l'elettrovalvola
  digitalWrite(LED_PIN, LOW);      // Accendi il LED integrato
  Serial.println("Irrigazione attivata per " + String(tempoIrrigazione / 1000) + " secondi");
}

// Funzione per riconnettersi al Wi-Fi se la connessione è persa
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Riconnessione alla rete Wi-Fi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.println("Connessione in corso...");
    }
    Serial.println("Connesso alla rete Wi-Fi");
  }
}

// Funzione per impostare il tempo di irrigazione (NUOVA)
void handleSetIrrigationTime() {
  if (server.hasArg("time")) {
    unsigned long newTime = server.arg("time").toInt() * 1000; // Converti in millisecondi

    if (newTime > 0 && newTime <= 3600000) {  // Validazione: tra 1 secondo e 1 ora
      salvaTempoIrrigazioneSuEEPROM(newTime);
      server.send(200, "text/plain", "Tempo di irrigazione aggiornato");
    } else {
      server.send(400, "text/plain", "Tempo di irrigazione non valido (deve essere tra 1s e 1h)");
    }
  } else {
    server.send(400, "text/plain", "Parametro 'time' mancante");
  }
}

// Funzione per gestire la richiesta di avvio dell'irrigazione
void handleStartIrrigation() {
  Serial.println("Richiesta /start-irrigation ricevuta");
  Serial.print("URL richiesto: ");
  Serial.println(server.uri());
  Serial.print("Parametri ricevuti: ");
  Serial.println(server.args());
  Serial.print("manual (hasArg): ");
  Serial.println(server.hasArg("manual"));

  if (server.hasArg("manual")) {
      String manualStr = server.arg("manual"); // Dichiarazione UNA SOLA VOLTA
      Serial.print("manual (arg): ");
      Serial.println(manualStr);
      bool manual = (manualStr == "true");

      Serial.print("Irrigazione manuale richiesta: ");
      Serial.println(manual);

      if (manual) {
          attivaIrrigazione();
          server.send(200, "text/plain", "Irrigazione manuale avviata");
      } else {
          unsigned int luminosita = leggiLuminosita();
          if (luminosita >= sogliaLuminosita) {
              attivaIrrigazione();
              server.send(200, "text/plain", "Irrigazione automatica avviata");
          } else {
              Serial.println("Luminosità troppo bassa. Irrigazione automatica inibita.");
              server.send(200, "text/plain", "Irrigazione automatica inibita");
          }
      }
  } else {
      Serial.println("Parametro 'manual' mancante. Considero irrigazione automatica.");
      unsigned int luminosita = leggiLuminosita();
      if (luminosita >= sogliaLuminosita) {
          attivaIrrigazione();
          server.send(200, "text/plain", "Irrigazione automatica avviata");
      } else {
          Serial.println("Luminosità troppo bassa. Irrigazione automatica inibita.");
          server.send(200, "text/plain", "Irrigazione automatica inibita");
      }
  }
}
// Funzione per impostare la soglia di luminosità
void handleSetThreshold() {
  if (server.hasArg("threshold")) {
    unsigned int newThreshold = server.arg("threshold").toInt();
    if (newThreshold >= 0 && newThreshold <= 100) {
      salvaSogliaLuminositaSuEEPROM(newThreshold);
      server.send(200, "text/plain", "Soglia di luminosità aggiornata");
    } else {
      server.send(400, "text/plain", "Valore 'threshold' non valido");
    }
  } else {
    server.send(400, "text/plain", "Parametro 'threshold' mancante");
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(RELAY_PIN, OUTPUT);    // Configura il pin del relay come output
  pinMode(LED_PIN, OUTPUT);      // Configura il LED integrato come output
  digitalWrite(LED_PIN, HIGH);   // Spegni il LED integrato all'avvio
  dht.begin();                   // Inizializza il sensore DHT

  leggiTempoIrrigazioneDaEEPROM();     // Leggi il tempo di irrigazione dalla EEPROM
  leggiSogliaLuminositaDaEEPROM();     // Leggi la soglia di luminosità dalla EEPROM

  WiFi.begin(ssid, password);
  Serial.println("Connessione alla rete Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connessione in corso...");
  }
  Serial.println("Connesso alla rete Wi-Fi");
  Serial.print("Indirizzo IP: ");
  Serial.println(WiFi.localIP());

  // Configura le rotte del server HTTP
  server.on("/start-irrigation", handleStartIrrigazione);
  server.on("/set-irrigation-time", handleSetIrrigazioneTime);
  server.on("/set-threshold", handleSetThreshold);

  server.begin();
  Serial.println("Server HTTP avviato");
}

void loop() {
  unsigned long tempoCorrente = millis();

  checkWiFiConnection();

  // Gestisce le richieste in arrivo
  server.handleClient();

  // Lettura periodica dei sensori
  if (tempoCorrente - tempoUltimaMisura >= intervalloMisuraSensori) {
    tempoUltimaMisura = tempoCorrente;
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    int luminosita = leggiLuminosita();  // Legge la luminosità mappata in percentuale

    if (isnan(temp) || isnan(hum)) {
      Serial.println("Errore nella lettura del sensore DHT. Uso valori predefiniti.");
      temp = 25.0;
      hum = 60.0;
    }

    Serial.print("Temperatura: ");
    Serial.print(temp);
    Serial.print(" °C, Umidità: ");
    Serial.print(hum);
    Serial.print(" %, Luminosità: ");
    Serial.println(luminosita);

    sendDataToServer(temp, hum, luminosita);  // Invia i dati al server
  }

  verificaIrrigazione();  // Verifica lo stato dell'irrigazione

  delay(10);  // Breve pausa per evitare un uso eccessivo della CPU
}