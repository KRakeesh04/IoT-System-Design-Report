#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Use Wokwi default virtual WiFi so both devices join the same network
const char *AP_SSID = "Wokwi-GUEST";
const char *AP_PASSWORD = ""; // open network on Wokwi
WebServer server(80);

#define FLOW_PULSE_PIN 26
#define CHEMICAL_OK_PIN 12
#define RGB_RED_PIN 14
#define RGB_GREEN_PIN 27
#define RGB_BLUE_PIN 33
#define BUZZER_PIN 25
#define TRIG_PIN 5
#define ECHO_PIN 18

volatile int flowPulseCount = 0;
bool refillSessionActive = false;
bool lowWaterAlertActive = false;
bool faultLatched = false;
unsigned long lastFlowPulseTime = 0;
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_LED_MS = 200;
const int TARGET_PULSES = 10;
const unsigned long FLOW_TIMEOUT = 5000;
const float TANK_HEIGHT_CM = 400.0;
const float LOW_WATER_THRESHOLD_CM = 20.0;
float simulatedDistanceCM = -1.0;
bool rgbCommonAnode = true;

void IRAM_ATTR flowISR()
{
  flowPulseCount++;
  lastFlowPulseTime = millis();
}

static void setRGBColor(bool r, bool g, bool b)
{
  if (rgbCommonAnode)
  {
    digitalWrite(RGB_RED_PIN, r ? LOW : HIGH);
    digitalWrite(RGB_GREEN_PIN, g ? LOW : HIGH);
    digitalWrite(RGB_BLUE_PIN, b ? LOW : HIGH);
  }
  else
  {
    digitalWrite(RGB_RED_PIN, r ? HIGH : LOW);
    digitalWrite(RGB_GREEN_PIN, g ? HIGH : LOW);
    digitalWrite(RGB_BLUE_PIN, b ? HIGH : LOW);
  }
}

static void displayMessage(const String &l1, const String &l2)
{
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println(l1);
  display.setCursor(0, 30);
  display.println(l2);
  display.display();
}

static float getDistanceCM()
{
  if (simulatedDistanceCM >= 0.0)
  {
    return simulatedDistanceCM;
  }

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration <= 0)
  {
    return TANK_HEIGHT_CM;
  }

  return duration * 0.034 / 2.0;
}

static float getWaterLevelCM()
{
  float distance = getDistanceCM();
  float waterLevel = TANK_HEIGHT_CM - distance;
  if (waterLevel < 0)
  {
    waterLevel = 0;
  }
  return waterLevel;
}

static void faultState()
{
  setRGBColor(true, false, false);
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(250);
    digitalWrite(BUZZER_PIN, LOW);
    delay(250);
  }
}

static void startRefillSession()
{
  refillSessionActive = true;
  flowPulseCount = 0;
  lastFlowPulseTime = millis();
  faultLatched = false;
  displayMessage("REFILL REQUEST", "Approved");
}

static void stopRefillSession()
{
  refillSessionActive = false;
  setRGBColor(false, true, false);
}

static void handleRequest()
{
  float waterLevel = getWaterLevelCM();
  bool chemicalOk = digitalRead(CHEMICAL_OK_PIN) == LOW;

  if (refillSessionActive)
  {
    server.send(409, "text/plain", "BUSY");
    return;
  }

  if (waterLevel < LOW_WATER_THRESHOLD_CM)
  {
    displayMessage("FAULT", "LOW WATER");
    faultLatched = true;
    faultState();
    server.send(200, "text/plain", "DENY");
    return;
  }

  if (!chemicalOk)
  {
    displayMessage("FAULT", "CHEMICAL EMPTY");
    faultLatched = true;
    faultState();
    server.send(200, "text/plain", "DENY");
    return;
  }

  startRefillSession();
  server.send(200, "text/plain", "APPROVE");
}

static void handleControl()
{
  if (faultLatched)
  {
    server.send(200, "text/plain", "FAULT");
    return;
  }

  if (!refillSessionActive)
  {
    server.send(200, "text/plain", "CLOSE");
    return;
  }

  if (flowPulseCount >= TARGET_PULSES)
  {
    stopRefillSession();
    displayMessage("REFILL DONE", "Target Reached");
    server.send(200, "text/plain", "CLOSE");
    return;
  }

  if (millis() - lastFlowPulseTime > FLOW_TIMEOUT)
  {
    displayMessage("FAULT", "NO FLOW");
    faultLatched = true;
    refillSessionActive = false;
    faultState();
    server.send(200, "text/plain", "FAULT");
    return;
  }

  server.send(200, "text/plain", "OPEN");
}

static void handleStatus()
{
  float waterLevel = getWaterLevelCM();
  bool chemicalOk = digitalRead(CHEMICAL_OK_PIN) == LOW;

  String payload = "water=" + String(waterLevel, 2) + " chemical=" + String(chemicalOk ? 1 : 0) + " pulses=" + String(flowPulseCount) + " active=" + String(refillSessionActive ? 1 : 0);
  server.send(200, "text/plain", payload);
}

static void handleHeartbeat()
{
  Serial.println("[HEARTBEAT] received from local node");
  lastHeartbeatTime = millis();
  server.send(200, "text/plain", "OK");
}

static void handleAnnounce()
{
  String ip = server.arg("ip");
  if (ip.length() == 0) ip = "unknown";
  Serial.print("[ANNOUNCE] Local node online, IP=");
  Serial.println(ip);
  lastHeartbeatTime = millis();
  server.send(200, "text/plain", "OK");
}

static void setupWiFiStationAndServer()
{
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  Serial.print("[WIFI] Connecting to ");
  Serial.println(AP_SSID);
  // Connect to Wokwi open AP
  WiFi.begin(AP_SSID);

  unsigned long start = millis();
  const unsigned long timeout = 5000;
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout)
  {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("[WIFI] Connected, IP=");
    Serial.println(WiFi.localIP());
    // advertise mDNS name so other boards can reach us via central.local
    if (MDNS.begin("central"))
    {
      Serial.println("[MDNS] responder started: central.local");
    }
  }
  else
  {
    Serial.println("[WIFI] WARNING: failed to join AP, server will still run but may not be reachable by other nodes");
  }

  server.on("/request", HTTP_GET, handleRequest);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/heartbeat", HTTP_GET, handleHeartbeat);
  server.on("/announce", HTTP_GET, handleAnnounce);
  server.begin();
  Serial.println("[HTTP] server started");
}

static void handleSerialCommands()
{
  if (Serial.available() == 0)
  {
    return;
  }

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "R" || cmd == "r")
  {
    simulatedDistanceCM = -1.0;
    Serial.println("[SIM] distance override disabled");
  }
  else if (cmd.startsWith("D") || cmd.startsWith("d"))
  {
    simulatedDistanceCM = cmd.substring(1).toFloat();
    Serial.print("[SIM] distance set to ");
    Serial.println(simulatedDistanceCM);
  }
  else if (cmd == "ANODE")
  {
    rgbCommonAnode = true;
    Serial.println("[CFG] RGB common anode");
  }
  else if (cmd == "CATHODE")
  {
    rgbCommonAnode = false;
    Serial.println("[CFG] RGB common cathode");
  }
}

static void monitorWaterLevel()
{
  float waterLevel = getWaterLevelCM();
  if (waterLevel < LOW_WATER_THRESHOLD_CM)
  {
    if (!lowWaterAlertActive)
    {
      lowWaterAlertActive = true;
      faultLatched = true;
      refillSessionActive = false;
      displayMessage("FAULT", "LOW WATER");
      faultState();
    }
    return;
  }

  if (lowWaterAlertActive)
  {
    lowWaterAlertActive = false;
    faultLatched = false;
  }

  if (!refillSessionActive && !faultLatched)
  {
    setRGBColor(false, true, false);
    displayMessage("Water: " + String(waterLevel, 1) + " cm", "System Ready");
  }
}

static void manageRefill()
{
  if (!refillSessionActive)
  {
    return;
  }

  if (flowPulseCount >= TARGET_PULSES)
  {
    return;
  }

  if (flowPulseCount == 0)
  {
    setRGBColor(true, true, false);
  }
  else
  {
    setRGBColor(false, false, true);
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("REFILL ACTIVE");
  display.print("Pulses: ");
  display.print(flowPulseCount);
  display.print("/");
  display.println(TARGET_PULSES);
  display.display();
}

void setup()
{
  Serial.begin(115200);
  pinMode(FLOW_PULSE_PIN, INPUT_PULLUP);
  pinMode(CHEMICAL_OK_PIN, INPUT_PULLUP);
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_PULSE_PIN), flowISR, FALLING);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    while (true)
    {
      delay(10);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  setRGBColor(false, true, false);
  setupWiFiStationAndServer();
  displayMessage("System Started", "Ready");
}


void loop()
{
  server.handleClient();
  handleSerialCommands();
  monitorWaterLevel();
  manageRefill();
  // heartbeat visual indicator: briefly flash red on controller RGB when heartbeat received
  if (millis() - lastHeartbeatTime < HEARTBEAT_LED_MS) {
    setRGBColor(true, false, false);
  }
  delay(50);
}
