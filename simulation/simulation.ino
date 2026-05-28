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
const float LOW_WATER_THRESHOLD_CM = 20.0;

// Simulation override: if >= 0, `getDistanceCM()` will return this value (cm)
float simulatedDistanceCM = -1.0;

// RGB wiring mode: false = common-cathode (COM -> GND, write HIGH to light)
// true = common-anode (COM -> VCC, write LOW to light)
bool rgbCommonAnode = true;

Servo valveServo;

// Polarity-agnostic button polling state
int lastTankRaw = HIGH;
unsigned long lastTankEventTime = 0;
int lastFlowRaw = HIGH;
unsigned long lastFlowEventTime = 0;
bool lowWaterAlertActive = false;

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

  setupCentralWorkflow();
  setupDispensingNodeWorkflow();

  // INITIAL RGB STATUS
  setRGBColor(false, true, false);

  // INITIAL SERVO POSITION
  valveServo.write(0);

  // Initialize raw button states for polarity-agnostic detection
  lastTankRaw = digitalRead(TANK_REQUEST_PIN);
  lastFlowRaw = digitalRead(FLOW_PULSE_PIN);

  Serial.println("================================");
  Serial.println("Hospital IoT Dispensing System");
  Serial.println("[ARCH] Split-node simulation enabled");
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
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0)
    {
      if (cmd == "s")
      {
        Serial.println("[CMD] Manual START refill");
        validateAndStartRefill();
      }
      else if (cmd == "c")
      {
        Serial.println("[CMD] Manual STOP refill");
        stopRefill();
      }
      else if (cmd.startsWith("D") || cmd.startsWith("d"))
      {
        // Set simulated distance in cm: e.g. D25.3
        String num = cmd.substring(1);
        float v = num.toFloat();
        simulatedDistanceCM = v;
        Serial.print("[SIM] Simulated distance set to ");
        Serial.print(simulatedDistanceCM);
        Serial.println(" cm");
      }
      else if (cmd == "R" || cmd == "r")
      {
        simulatedDistanceCM = -1.0;
        Serial.println("[SIM] Simulated distance disabled");
      }
      else if (cmd == "ANODE")
      {
        rgbCommonAnode = true;
        Serial.println("[CFG] RGB mode: COMMON ANODE (pins LOW = ON)");
      }
      else if (cmd == "CATHODE")
      {
        rgbCommonAnode = false;
        Serial.println("[CFG] RGB mode: COMMON CATHODE (pins HIGH = ON)");
      }
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

  if (waterLevel < LOW_WATER_THRESHOLD_CM)
  {
    if (!lowWaterAlertActive)
    {
      lowWaterAlertActive = true;
      Serial.println("[ALERT] LOW WATER threshold reached");
      displayMessage("FAULT", "LOW WATER");
      setRGBColor(true, false, false);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(120);
      digitalWrite(BUZZER_PIN, LOW);
    }
    return;
  }

  sendNodeStatusToCentral(waterLevel, digitalRead(CHEMICAL_OK_PIN) == LOW, refillActive);

  if (lowWaterAlertActive)
  {
    lowWaterAlertActive = false;
    Serial.println("[INFO] Water level back to normal");
    if (!refillActive)
    {
      setRGBColor(false, true, false);
      displayMessage("System Started", "Ready");
    }
  }
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
          Serial.println("[INPUT] Tank request received");
          displayMessage("TANK REQUEST", "Received");
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
  if (waterLevel < LOW_WATER_THRESHOLD_CM)
  {
    Serial.println("[FAULT] LOW WATER - Tank below threshold");
    displayMessage("FAULT", "LOW WATER");
    faultState();
    return;
  }

    // CHEMICAL CHECK - refill must not start unless chemical is OK
  int chemicalReading = digitalRead(CHEMICAL_OK_PIN);
  Serial.print("[CHECK] Chemical Status: ");
    Serial.println(chemicalReading == LOW ? "OK" : "EMPTY");

    if (chemicalReading == HIGH)
    {
      Serial.println("[FAULT] CHEMICAL NOT OK - refill blocked");
      displayMessage("FAULT", "CHEMICAL EMPTY");
      faultState();
      return;
    }

    if (!sendNodeStatusToCentral(waterLevel, true, refillActive))
    {
      Serial.println("[CENTRAL] Refill denied by central workflow");
      displayMessage("FAULT", "CENTRAL DENIED");
      faultState();
      return;
    }

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

  if (rgbCommonAnode)
  {
    // Common anode: drive pins LOW to turn color on
    digitalWrite(RGB_RED_PIN, red ? LOW : HIGH);
    digitalWrite(RGB_GREEN_PIN, green ? LOW : HIGH);
    digitalWrite(RGB_BLUE_PIN, blue ? LOW : HIGH);
  }
  else
  {
    // Common cathode: drive pins HIGH to turn color on
    digitalWrite(RGB_RED_PIN, red ? HIGH : LOW);
    digitalWrite(RGB_GREEN_PIN, green ? HIGH : LOW);
    digitalWrite(RGB_BLUE_PIN, blue ? HIGH : LOW);
  }
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

  // If a simulated distance has been set via serial, use it (for Wokwi testing)
  if (simulatedDistanceCM >= 0.0)
  {
    return simulatedDistanceCM;
  }

  digitalWrite(TRIG_PIN, LOW);

  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);

  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(
      ECHO_PIN,
      HIGH,
      30000); // timeout 30ms

  if (duration <= 0)
  {
    // No echo received; return a large distance so waterLevel stays near 0
    Serial.println("[DEBUG] pulseIn timeout or no echo");
    return TANK_HEIGHT_CM; // assume empty reading
  }

  float distance = duration * 0.034 / 2.0;

  Serial.print("[DEBUG] pulse duration= ");
  Serial.print(duration);
  Serial.print(" us, distance= ");
  Serial.print(distance);
  Serial.println(" cm");

  return distance;
}
