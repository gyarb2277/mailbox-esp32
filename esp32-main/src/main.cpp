#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "DHT.h"

// WiFi
const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "PASSWORD";

// ESP32-CAM
const char* CAM_HOST = "192.168.0.60";
const int   CAM_PORT = 80;

// Pins (fast)
const int LED_G = 16;     // Grön: lucka öppen
const int LED_Y = 17;     // Gul: IR bruten
const int RELAY = 21;     // aktiv LOW
const int DHTPIN = 19;
const int IRPIN = 18;     // LOW = bruten
const int LIDPIN = 5;     // INPUT_PULLUP, LOW = öppen

// DHT
DHT dht(DHTPIN, DHT22);
float tLast = NAN, hLast = NAN;
unsigned long dhtMs = 0;

// Web
WebServer server(80);

// Status
bool mailDetected = false;
uint32_t imgVer = 0;

// Timing
const unsigned long WINDOW_MS   = 4000;  // aktivt fönster efter luckhändelse
const unsigned long COOLDOWN_MS = 2000;  // min tid mellan foton
const unsigned long LAMP_PRE_MS = 200;
const unsigned long LAMP_POST_MS = 600;

unsigned long activeUntil = 0;
unsigned long lastShotMs  = 0;

bool lidOpen()   { return digitalRead(LIDPIN) == LOW; }
bool irBroken()  { return digitalRead(IRPIN)  == LOW; }
void lamp(bool on){ digitalWrite(RELAY, on ? LOW : HIGH); }

void readDhtSometimes() {
  if (millis() - dhtMs < 4000) return;
  dhtMs = millis();
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(t)) tLast = t;
  if (!isnan(h)) hLast = h;
}

bool camCapture() {
  HTTPClient http;
  String url = String("http://") + CAM_HOST + ":" + CAM_PORT + "/capture";
  http.begin(url);
  http.setTimeout(6000);
  int code = http.GET();
  http.end();
  return code == 200;
}

void takePhoto() {
  unsigned long now = millis();
  if (now - lastShotMs < COOLDOWN_MS) return;
  lastShotMs = now;


  lamp(true);
  delay(LAMP_PRE_MS);

  if (camCapture()) {
    imgVer++;
    mailDetected = true;
  }

  delay(LAMP_POST_MS);
  lamp(false);
}

String page() {
  readDhtSometimes();

  String s;
  s += "<!doctype html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>Smart Brevlåda</title></head><body>";
  s += "<h2>Smart Brevlåda</h2>";

  s += "<p><b>Status:</b> ";
  s += mailDetected ? "POST HAR KOMMIT" : "Ingen post just nu";
  s += "</p>";

  s += "<p><b>Temperatur:</b> ";
  s += isnan(tLast) ? "?" : String(tLast,1);
  s += " &deg;C<br><b>Luftfuktighet:</b> ";
  s += isnan(hLast) ? "?" : String(hLast,1);
  s += " %</p>";

  s += "<p><b>Lucka:</b> ";
  s += lidOpen() ? "Öppen" : "Stängd";
  s += "<br><b>IR:</b> ";
  s += irBroken() ? "Bruten" : "Fritt";
  s += "</p>";

  s += "<p><a href='/cap'><button style='font-size:18px;padding:10px'>Ta bild nu</button></a> ";
  s += "<a href='/clr'><button style='font-size:18px;padding:10px'>Nollställ</button></a></p>";

  s += "<h3>Senaste bild</h3>";
s += "<img src='http://" + String(CAM_HOST) + "/jpg?v=" + String(imgVer) + "' style='max-width:100%;border:1px solid #ccc'>";  s += "<p><a href='/'>Uppdatera</a></p>";
  s += "</body></html>";
  return s;
}

void hRoot(){ server.send(200, "text/html", page()); }
void hCap() {

  takePhoto();

  delay(800);   // låt kameran bli klar

  server.sendHeader("Location","/");
  server.send(302,"text/plain","");
}
void hClr(){ mailDetected=false; server.sendHeader("Location","/"); server.send(302,"text/plain",""); }

void setup() {
  pinMode(LED_G, OUTPUT);
  pinMode(LED_Y, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(IRPIN, INPUT);
  pinMode(LIDPIN, INPUT_PULLUP);

  lamp(false);
  dht.begin();
  delay(1200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  server.on("/", hRoot);
  server.on("/cap", hCap);
  server.on("/clr", hClr);
  server.begin();
}

void loop() {
  server.handleClient();

  // LED direktstatus
  digitalWrite(LED_G, lidOpen() ? HIGH : LOW);
  digitalWrite(LED_Y, irBroken() ? HIGH : LOW);

  // Aktiv-fönster: medan luckan är öppen förlängs det hela tiden
  unsigned long now = millis();
  static bool prevLidOpen = false;
  bool lo = lidOpen();

  if (lo) {
    activeUntil = now + WINDOW_MS;        // håller sig aktivt medan luckan är öppen
  } else if (prevLidOpen && !lo) {
    activeUntil = now + WINDOW_MS;        // luckan stängde nyss -> fortsätt vara aktiv
  }
  prevLidOpen = lo;

  // Trigger: IR bruten inom aktiv-fönster
  if (irBroken() && now < activeUntil) {
    takePhoto();
  }
}                         
