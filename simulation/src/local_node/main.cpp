#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>

const char *AP_SSID = "HospitalCentral";
const char *AP_PASSWORD = "Hospital123";
const char *CENTRAL_URL = "http://192.168.4.1";

#define TANK_REQUEST_PIN 13
#define SERVO_PIN 4

Servo valveServo;

bool refillActive = false;
unsigned long lastControlPoll = 0;
unsigned long lastButtonEventTime = 0;
int lastButtonState = HIGH;

static void setValveOpen(bool open)
{
  valveServo.write(open ? 90 : 0);
}

static String httpGet(const String &path)
{
  WiFiClient client;
  HTTPClient http;
  String url = String(CENTRAL_URL) + path;
  if (!http.begin(client, url))
  {
    return String();
  }
  int code = http.GET();
  String payload = (code > 0) ? http.getString() : String();
  http.end();
  return payload;
}

static void connectToCentralWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(AP_SSID, AP_PASSWORD);
  Serial.print("[WIFI] Connecting to ");
  Serial.println(AP_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
  {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("[WIFI] Connected, IP=");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("[WIFI] Connection failed, will retry");
  }
}

static void requestRefill()
{
  String response = httpGet("/request");
  if (response.length() == 0)
  {
    refillActive = false;
    setValveOpen(false);
    return;
  }
  if (response.indexOf("APPROVE") >= 0)
  {
    refillActive = true;
    setValveOpen(true);
  }
  else
  {
    refillActive = false;
    setValveOpen(false);
  }
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
  valveServo.attach(SERVO_PIN);
  setValveOpen(false);
  connectToCentralWiFi();
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToCentralWiFi();
  }

  int buttonState = digitalRead(TANK_REQUEST_PIN);
  if (buttonState != lastButtonState)
  {
    unsigned long now = millis();
    if (now - lastButtonEventTime > 250)
    {
      delay(20);
      int stable = digitalRead(TANK_REQUEST_PIN);
      if (stable != lastButtonState && stable == LOW)
      {
        lastButtonEventTime = now;
        requestRefill();
      }
      lastButtonState = stable;
    }
  }

  pollControl();
  delay(50);
}
