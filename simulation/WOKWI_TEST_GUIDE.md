# Wokwi Simulation Testing Guide

## Prerequisites
- Reload Wokwi with the latest `simulation/controller/controller.ino.bin` and `simulation/local_node/local_node.ino.bin`
  Open **Wokwi Serial Monitor** (bottom panel)

---

## Communication Method

The local node connects to the central controller over WiFi.
The central ESP32 hosts a WiFi access point and the local node sends HTTP requests to it.
The request button and servo valve stay on the local node.
The sensors, RGB LED, buzzer, and OLED stay on the central controller.

## Full Refill Cycle Test (Button Press Method)

**Refill denied immediately:**
- ✅ Make sure the central ESP32 is loaded with `simulation/controller/controller.ino.bin`
- ✅ Make sure the local node is connected to the `HospitalCentral` WiFi network
- ✅ Check that the request button is wired to `localNodeEsp:D13` and the valve servo is wired to `localNodeEsp:D4`

**Expected Serial Output:**
```
================================
Hospital IoT Dispensing System
System Started
[HELP] Serial Commands:
  's' = Start refill manually
  'c' = Stop refill
================================
```

**Visual Check:**
- ✅ RGB LED should be **GREEN** (ready state)
- ✅ OLED should display "System Started / Ready"
- ✅ Central controller should report that it is waiting for local node requests

---

### Step 2: Monitor Idle State (Water Level Monitoring)
**Time: 5-10 seconds**

**Expected Serial Output (every 200ms):**
```
Water Level: 98.03 cm
[STATE] Tank=RELEASED Chemical=OK RefillActive=NO
[STATE] Tank=RELEASED Chemical=OK RefillActive=NO
```

**Visual Check:**
- ✅ RGB LED remains **GREEN**
- ✅ OLED shows water level updates
- ✅ No alarms or faults

---

### Step 3: Press TANK_REQUEST Button (Pin D13)
**Action:** Click the **green "tank request" button** in Wokwi

**Expected Serial Output (immediate):**
```
[EVENT] Tank Refill Request - Starting validation
[CHECK] Water Level: 98.03
[CHECK] Chemical Status: OK
[NODE] Sending refill request to central controller
[NODE] Central response: APPROVE
[INFO] Validation passed - Starting refill
Refill Started
Mixing Active
```

**Visual Check:**
- ✅ RGB LED turns **YELLOW** (refilling)
- ✅ OLED displays "REFILL ACTIVE / Mixing Started"
- ✅ Buzzer may beep (optional)

---

### Step 4: Simulate Flow Pulses (Pin D26)
**Action:** Click the **green "flow pulse" button** 10+ times rapidly

**Expected Serial Output (each click):**
```
[PROGRESS] Flow count: 1/10
[PROGRESS] Flow count: 2/10
[PROGRESS] Flow count: 3/10
...
[PROGRESS] Flow count: 10/10
```

**Visual Check:**
- ✅ RGB LED turns **BLUE** (mixing active)
- ✅ OLED updates: "REFILL ACTIVE / Pulses: X/10"
- ✅ Counter increments each button click

---

### Step 5: Complete Refill (10 Pulses Reached)
**Expected Serial Output:**
```
[SUCCESS] TARGET REACHED - Refill Complete
```

**Visual Check:**
- ✅ RGB LED returns to **GREEN** (ready)
- ✅ OLED displays "REFILL DONE / Target Reached"
- ✅ Servo resets to 0° (valve closes)

---

## Visual Indicators Checklist

| State | RGB Color | OLED Display | Meaning |
|-------|-----------|--------------|---------|
| Idle/Ready | GREEN | System Started / Ready | Waiting for request |
| Refilling | YELLOW | REFILL ACTIVE / Mixing Started | Valve open, accepting pulses |
| Mixing | BLUE | REFILL ACTIVE / Pulses: X/10 | Counting flow pulses |
| Complete | GREEN | REFILL DONE / Target Reached | Cycle finished |
| Fault | RED | FAULT / (reason) | Low water / No chemical / No flow |

---

## Fault Conditions to Test

### Low Water Fault
1. Modify `TANK_HEIGHT_CM` to 50 (instead of 100) to simulate low water
2. Press TANK_REQUEST → should show: `[FAULT] LOW WATER - Tank below 20cm`
3. RGB turns **RED**, buzzer beeps 5x

### No Flow Timeout Fault
1. Start refill normally (RGB = YELLOW)
2. **Don't click FLOW_PULSE button** for 5+ seconds
3. Should show: `[FAULT] NO FLOW detected - timeout`
4. RGB turns **RED**, buzzer beeps 5x

---

## Serial Command Fallback (If Buttons Don't Work)

Open Wokwi serial monitor and type:

| Command | Action |
|---------|--------|
| `s` | Start refill manually |
| `c` | Stop refill |

Example:
```
Type: s
Output: [CMD] Manual START refill
```

---

## Troubleshooting

**Buttons don't respond:**
- ✅ Try serial command `s` instead
- ✅ Check Wokwi diagram connections for the local node request button and central controller sensors

**Water level stuck at 98.03:**
- ✅ This is normal (HC-SR04 in Wokwi is simulated at fixed distance)

**No serial output:**
- ✅ Make sure serial monitor is open and set to **115200 baud**
- ✅ Reload Wokwi page

**RGB LED doesn't change:**
- ✅ Verify resistors (r1, r2, r3) are connected to RGB
- ✅ Check RGB pinout (D14=RED, D27=GREEN, D33=BLUE)

**Refill denied immediately:**
- ✅ Make sure the central ESP32 is loaded with `simulation/controller/controller.ino.bin`
- ✅ Check that the local node is connected to the `HospitalCentral` WiFi network
- ✅ Check that the request button is wired to `localNodeEsp:D13` and the valve servo is wired to `localNodeEsp:D4`

---

## Success Criteria

✅ **Full test passes if:**
1. System starts with GREEN LED
2. TANK_REQUEST button triggers validation
3. Refill cycle starts (RGB → YELLOW)
4. FLOW_PULSE increments counter
5. 10 pulses reached → completion (RGB → GREEN)
6. Serial monitor shows all `[EVENT]`, `[CHECK]`, `[PROGRESS]`, `[SUCCESS]` messages

**Total time: ~15-20 seconds for full cycle**
