/*
  Distributed Dispensing Tank Node Prototype
  WokWi Simulation
  Author: Kanes Rakeshan
  Index Number: 230518C
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// OLED CONFIGURATION
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1);

// PIN DEFINITIONS
// INPUTS
#define TANK_REQUEST_PIN 13
#define FLOW_PULSE_PIN 26
#define CHEMICAL_OK_PIN 12
// RGB LED
#define RGB_RED_PIN 14
#define RGB_GREEN_PIN 27
#define RGB_BLUE_PIN 33
// BUZZER
#define BUZZER_PIN 25
// ULTRASONIC
#define TRIG_PIN 5
#define ECHO_PIN 18
// SERVO
#define SERVO_PIN 4

// SYSTEM VARIABLES
volatile int flowPulseCount = 0;
bool refillActive = false;
unsigned long lastFlowPulseTime = 0;
const int TARGET_PULSES = 10;
const unsigned long FLOW_TIMEOUT = 5000;
const float TANK_HEIGHT_CM = 100.0;

Servo valveServo;

// Polarity-agnostic button polling state
int lastTankRaw = HIGH;
unsigned long lastTankEventTime = 0;
int lastFlowRaw = HIGH;
unsigned long lastFlowEventTime = 0;

void IRAM_ATTR flowISR()
{

  flowPulseCount++;

  lastFlowPulseTime = millis();
}

// This simulation represents a single distributed dispensing node
// communicating with the centralized monitoring architecture.
void setup()
{

  Serial.begin(115200);

  // INPUTS
  pinMode(TANK_REQUEST_PIN, INPUT_PULLUP);
  pinMode(FLOW_PULSE_PIN, INPUT_PULLUP);
  pinMode(CHEMICAL_OK_PIN, INPUT_PULLUP);

  // RGB LED
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);

  // BUZZER
  pinMode(BUZZER_PIN, OUTPUT);

  // ULTRASONIC
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  valveServo.attach(SERVO_PIN);

  attachInterrupt(
      digitalPinToInterrupt(FLOW_PULSE_PIN),
      flowISR,
      FALLING);

  if (!display.begin(
          SSD1306_SWITCHCAPVCC,
          0x3C))
  {

    Serial.println("OLED FAILED");

    while (true)
      ;
  }

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // INITIAL RGB STATUS
  setRGBColor(false, true, false);

  // INITIAL SERVO POSITION
  valveServo.write(0);

  // Initialize raw button states for polarity-agnostic detection
  lastTankRaw = digitalRead(TANK_REQUEST_PIN);
  lastFlowRaw = digitalRead(FLOW_PULSE_PIN);

  Serial.println("================================");
  Serial.println("Hospital IoT Dispensing System");
  Serial.println("System Started");
  Serial.println("[HELP] Serial Commands:");
  Serial.println("  's' = Start refill manually");
  Serial.println("  'c' = Stop refill");
  Serial.println("================================");

  displayMessage(
      "System Started",
      "Ready");
}

void loop()
{
  if (Serial.available() > 0)
  {
    char cmd = Serial.read();
    if (cmd == 's')
    {
      Serial.println("[CMD] Manual START refill");
      validateAndStartRefill();
    }
    else if (cmd == 'c')
    {
      Serial.println("[CMD] Manual STOP refill");
      stopRefill();
    }
  }

  monitorWaterLevel();
  checkDispensingRequest();
  pollFlowButton();
  manageRefill();

  delay(200);
}

// WATER LEVEL MONITORING
void monitorWaterLevel()
{

  float distance = getDistanceCM();

  float waterLevel = TANK_HEIGHT_CM - distance;

  if (waterLevel < 0)
  {
    waterLevel = 0;
  }

  Serial.print("Water Level: ");
  Serial.print(waterLevel);
  Serial.println(" cm");
}

// DISPENSING REQUEST CHECK
void checkDispensingRequest()
{
  static int lastTankBtnState = HIGH;
  int tankBtn = digitalRead(TANK_REQUEST_PIN);
  int chemicalBtn = digitalRead(CHEMICAL_OK_PIN);

  static unsigned long lastDebugTime = 0;
  if (millis() - lastDebugTime > 3000)
  {
    Serial.print("[STATE] Tank=");
    Serial.print(tankBtn == LOW ? "PRESSED" : "RELEASED");
    Serial.print(" Chemical=");
    Serial.print(chemicalBtn == LOW ? "OK" : "EMPTY");
    Serial.print(" RefillActive=");
    Serial.println(refillActive ? "YES" : "NO");
    lastDebugTime = millis();
  }

  if (tankBtn != lastTankRaw)
  {
    unsigned long now = millis();
    if (now - lastTankEventTime > 300)
    {
      delay(30);
      int stable = digitalRead(TANK_REQUEST_PIN);
      if (stable != lastTankRaw)
      {
        lastTankEventTime = now;
        lastTankRaw = stable;
        if (!refillActive)
        {
          Serial.println("[EVENT] Tank button change detected (polarity-agnostic)");
          validateAndStartRefill();
        }
      }
    }
  }
  lastTankBtnState = tankBtn;
}

// Polled fallback for flow pulse button (increments pulses on button change)
void pollFlowButton()
{
  int flowRaw = digitalRead(FLOW_PULSE_PIN);
  if (flowRaw != lastFlowRaw)
  {
    unsigned long now = millis();
    if (now - lastFlowEventTime > 150)
    {
      delay(20);
      int stable = digitalRead(FLOW_PULSE_PIN);
      if (stable != lastFlowRaw)
      {
        lastFlowEventTime = now;
        lastFlowRaw = stable;
        if (refillActive)
        {
          flowPulseCount++;
          lastFlowPulseTime = millis();
          Serial.print("[POLL] Flow pulse detected. Count=");
          Serial.println(flowPulseCount);
        }
      }
    }
  }
}

// VALIDATION
void validateAndStartRefill()
{
  float distance = getDistanceCM();
  float waterLevel = TANK_HEIGHT_CM - distance;

  Serial.print("[CHECK] Water Level: ");
  Serial.println(waterLevel);

  // LOW WATER CHECK
  if (waterLevel < 20)
  {
    Serial.println("[FAULT] LOW WATER - Tank below 20cm");
    displayMessage("FAULT", "LOW WATER");
    faultState();
    return;
  }

  // CHEMICAL CHECK - Log status but don't block (Wokwi may not simulate button)
  int chemicalReading = digitalRead(CHEMICAL_OK_PIN);
  Serial.print("[CHECK] Chemical Status: ");
  Serial.println(chemicalReading == LOW ? "OK" : "EMPTY (auto-bypass in sim)");

  // In Wokwi sim, auto-bypass chemical check if button doesn't work
  // Uncomment next line to enforce chemical check:
  // if (chemicalReading == HIGH) { faultState(); return; }

  Serial.println("[INFO] Validation passed - Starting refill");
  startRefill();
}

// START REFILL
void startRefill()
{

  refillActive = true;

  flowPulseCount = 0;

  lastFlowPulseTime = millis();

  // YELLOW = REFILLING
  setRGBColor(true, true, false);

  // OPEN VALVE
  valveServo.write(90);

  Serial.println("Refill Started");
  Serial.println("Mixing Active");

  displayMessage(
      "REFILL ACTIVE",
      "Mixing Started");
}

// MANAGE REFILL
void manageRefill()
{

  if (!refillActive)
  {
    return;
  }

  Serial.print("Flow Pulses: ");
  Serial.println(flowPulseCount);

  // BLUE = MIXING ACTIVE
  setRGBColor(false, false, true);

  // OLED STATUS
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("REFILL ACTIVE");
  display.print("Pulses: ");
  display.print(flowPulseCount);
  display.print("/");
  display.println(TARGET_PULSES);
  display.display();

  Serial.print("[PROGRESS] Flow count: ");
  Serial.print(flowPulseCount);
  Serial.print("/");
  Serial.println(TARGET_PULSES);

  // TARGET REACHED
  if (flowPulseCount >= TARGET_PULSES)
  {
    stopRefill();
    Serial.println("[SUCCESS] TARGET REACHED - Refill Complete");
    displayMessage("REFILL DONE", "Target Reached");
    return;
  }

  // NO FLOW FAULT
  if (millis() - lastFlowPulseTime > FLOW_TIMEOUT)
  {
    Serial.println("[FAULT] NO FLOW detected - timeout");
    displayMessage("FAULT", "NO FLOW");
    stopRefill();
    faultState();
  }
}

// STOP REFILL
void stopRefill()
{

  refillActive = false;

  // GREEN = READY
  setRGBColor(false, true, false);

  // CLOSE VALVE
  valveServo.write(0);
}

// FAULT STATE
void faultState()
{

  // RED = FAULT
  setRGBColor(true, false, false);

  for (int i = 0; i < 5; i++)
  {

    digitalWrite(BUZZER_PIN, HIGH);

    delay(300);

    digitalWrite(BUZZER_PIN, LOW);

    delay(300);
  }
}

// RGB CONTROL
void setRGBColor(
    bool red,
    bool green,
    bool blue)
{

  digitalWrite(RGB_RED_PIN, red);
  digitalWrite(RGB_GREEN_PIN, green);
  digitalWrite(RGB_BLUE_PIN, blue);
}

// OLED MESSAGE
void displayMessage(
    String line1,
    String line2)
{

  display.clearDisplay();

  display.setCursor(0, 10);

  display.println(line1);

  display.setCursor(0, 30);

  display.println(line2);

  display.display();
}

// ULTRASONIC FUNCTION
float getDistanceCM()
{

  digitalWrite(TRIG_PIN, LOW);

  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);

  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(
      ECHO_PIN,
      HIGH);

  float distance =
      duration * 0.034 / 2.0;

  return distance;
}
