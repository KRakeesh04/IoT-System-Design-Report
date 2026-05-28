#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>

// Use Wokwi default virtual WiFi (open, no password)
const char *AP_SSID = "Wokwi-GUEST";
const char *AP_PASSWORD = ""; // empty for open network
const char *CENTRAL_URL = "http://central.local";

constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 3000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;
const unsigned long DISCOVERY_RETRY_INTERVAL_MS = 2000;

#define TANK_REQUEST_PIN 13
#define SERVO_PIN 4
#define BLINK_PIN 5
#define HEARTBEAT_BTN_PIN 15

Servo valveServo;

bool refillActive = false;
bool pendingRefillRequest = false;
bool heartbeatEnabled = false;
bool pendingHeartbeatRequest = false;
unsigned long lastControlPoll = 0;
unsigned long lastButtonEventTime = 0;
int lastButtonState = HIGH;
unsigned long lastWiFiAttempt = 0;
bool wifiConnectedLogged = false;
unsigned long wifiConnectStartTime = 0;
unsigned long lastDiscoveryAttempt = 0;
bool centralDiscovered = false;

// Blink state (non-blocking)
unsigned long lastBlinkMillis = 0;
bool blinkState = false;
const unsigned long BLINK_INTERVAL_MS = 500;

// Heartbeat pattern when connected: two short blinks every HEARTBEAT_PERIOD_MS
const unsigned long HEARTBEAT_SHORT_MS = 80;
const unsigned long HEARTBEAT_GAP_MS = 100;
const unsigned long HEARTBEAT_PERIOD_MS = 1500;

static void setValveOpen(bool open)
{
  valveServo.write(open ? 90 : 0);
}

static String httpGet(const String &path)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[HTTP] WiFi not connected, skipping");
    return String();
  }

  String url = String(CENTRAL_URL) + path;
  Serial.print("[HTTP] GET ");
  Serial.println(url);

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url))
  {
    Serial.println("[HTTP] begin() failed");
    return String();
  }
  int code = http.GET();
  String payload = (code > 0) ? http.getString() : String();
  Serial.print("[HTTP] response code=");
  Serial.println(code);
  http.end();
  return payload;
}

static void connectToCentralWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  Serial.print("[WIFI] Connecting to ");
  Serial.println(AP_SSID);
  if (AP_PASSWORD[0] == '\0')
  {
    WiFi.begin(AP_SSID);
  }
  else
  {
    WiFi.begin(AP_SSID, AP_PASSWORD);
  }
  wifiConnectStartTime = millis();
}

static void maintainCentralWiFi()
{
  wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED)
  {
    if (!wifiConnectedLogged)
    {
      wifiConnectedLogged = true;
      Serial.print("[WIFI] Connected, IP=");
      Serial.println(WiFi.localIP());
      lastDiscoveryAttempt = 0;
    }
    return;
  }

  wifiConnectedLogged = false;

  if (wifiConnectStartTime > 0 && millis() - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT_MS)
  {
    Serial.println("[WIFI] Connection timeout, resetting...");
    WiFi.disconnect();
    wifiConnectStartTime = 0;
    lastWiFiAttempt = millis();
    return;
  }

  if (millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL_MS)
  {
    lastWiFiAttempt = millis();
    Serial.print("[WIFI] Status: ");
    Serial.println((int)status);
    connectToCentralWiFi();
  }
}

static void requestRefill()
{
  Serial.println("[NODE] Sending refill request to central controller");
  String response = httpGet("/request");
  if (response.length() == 0)
  {
    Serial.println("[NODE] Central request failed, will retry");
    refillActive = false;
    setValveOpen(false);
    return;
  }

  Serial.print("[NODE] Central response: ");
  Serial.println(response);
  if (response.indexOf("APPROVE") >= 0)
  {
    refillActive = true;
    pendingRefillRequest = false;
    setValveOpen(true);
  }
  else
  {
    refillActive = false;
    pendingRefillRequest = false;
    setValveOpen(false);
  }
}

static void processPendingRequest()
{
  if (!pendingRefillRequest)
  {
    return;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  requestRefill();
}

static void pollControl()
{
  if (!refillActive)
  {
    return;
  }
  if (millis() - lastControlPoll < 250)
  {
    return;
  }
  lastControlPoll = millis();
  String response = httpGet("/control");
  if (response.length() == 0)
  {
    return;
  }
  if (response.indexOf("OPEN") >= 0)
  {
    setValveOpen(true);
  }
  else if (response.indexOf("CLOSE") >= 0)
  {
    setValveOpen(false);
    refillActive = false;
  }
  else if (response.indexOf("FAULT") >= 0 || response.indexOf("DENY") >= 0)
  {
    setValveOpen(false);
    refillActive = false;
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(TANK_REQUEST_PIN, INPUT_PULLUP);
  pinMode(HEARTBEAT_BTN_PIN, INPUT_PULLUP);
  valveServo.attach(SERVO_PIN);
  setValveOpen(false);
  pinMode(BLINK_PIN, OUTPUT);
  digitalWrite(BLINK_PIN, LOW);
  lastWiFiAttempt = millis();
  connectToCentralWiFi();
}

void loop()
{
  maintainCentralWiFi();
  // LED heartbeat: fast `heartbeat` when WiFi connected, slow blink when disconnected
  unsigned long now = millis();
  bool wifiReady = (WiFi.status() == WL_CONNECTED);

  if (wifiReady)
  {
    if (heartbeatEnabled)
    {
      unsigned long t = now % HEARTBEAT_PERIOD_MS;
      if (t < HEARTBEAT_SHORT_MS)
      {
        digitalWrite(BLINK_PIN, HIGH);
      }
      else if (t < HEARTBEAT_SHORT_MS + HEARTBEAT_GAP_MS)
      {
        digitalWrite(BLINK_PIN, LOW);
      }
      else if (t < HEARTBEAT_SHORT_MS + HEARTBEAT_GAP_MS + HEARTBEAT_SHORT_MS)
      {
        digitalWrite(BLINK_PIN, HIGH);
      }
      else
      {
        digitalWrite(BLINK_PIN, LOW);
      }
      // send a lightweight heartbeat to central once per heartbeat period
      static unsigned long lastHeartbeatSent = 0;
      if (now - lastHeartbeatSent >= HEARTBEAT_PERIOD_MS)
      {
        lastHeartbeatSent = now;
        (void)httpGet("/heartbeat");
      }
    }
    else
    {
      // WiFi connected but heartbeat not enabled: keep LED off
      digitalWrite(BLINK_PIN, LOW);
    }
  }
  else
  {
    if (now - lastBlinkMillis >= BLINK_INTERVAL_MS)
    {
      lastBlinkMillis = now;
      blinkState = !blinkState;
      digitalWrite(BLINK_PIN, blinkState ? HIGH : LOW);
      Serial.print("[BLINK] pin ");
      Serial.print(BLINK_PIN);
      Serial.print(" -> ");
      Serial.println(blinkState ? "HIGH" : "LOW");
    }
  }

  // Handle tank request button (existing behavior)
  int buttonState = digitalRead(TANK_REQUEST_PIN);
  if (buttonState != lastButtonState)
  {
    unsigned long nowBtn = millis();
    if (nowBtn - lastButtonEventTime > 250)
    {
      delay(20);
      int stable = digitalRead(TANK_REQUEST_PIN);
      if (stable != lastButtonState && stable == LOW)
      {
        lastButtonEventTime = nowBtn;
        pendingRefillRequest = true;
        requestRefill();
      }
      lastButtonState = stable;
    }
  }

  // Handle heartbeat enable button: press to request heartbeat activation
  static int lastHbBtnState = HIGH;
  int hbState = digitalRead(HEARTBEAT_BTN_PIN);
  if (hbState != lastHbBtnState)
  {
    unsigned long nowHb = millis();
    if (nowHb - lastButtonEventTime > 250)
    {
      delay(20);
      int stable = digitalRead(HEARTBEAT_BTN_PIN);
      if (stable != lastHbBtnState && stable == LOW)
      {
        // user pressed heartbeat enable
        pendingHeartbeatRequest = true;
        Serial.println("[HEARTBEAT] enable button pressed");
      }
      lastHbBtnState = stable;
    }
  }

  processPendingRequest();

  // Attempt discovery of central controller
  if (WiFi.status() == WL_CONNECTED && millis() - lastDiscoveryAttempt >= DISCOVERY_RETRY_INTERVAL_MS)
  {
    lastDiscoveryAttempt = millis();
    if (!centralDiscovered)
    {
      Serial.println("[DISCOVERY] Pinging central...");
      String response = httpGet("/announce?ip=" + WiFi.localIP().toString());
      if (response.length() > 0)
      {
        centralDiscovered = true;
        Serial.println("[DISCOVERY] Central reachable!");
      }
    }
  }

  // process heartbeat pending request: enable when WiFi connected
  if (pendingHeartbeatRequest)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      heartbeatEnabled = true;
      pendingHeartbeatRequest = false;
      Serial.println("[HEARTBEAT] enabled (WiFi connected)");
    }
  }
  pollControl();
  delay(50);
}
