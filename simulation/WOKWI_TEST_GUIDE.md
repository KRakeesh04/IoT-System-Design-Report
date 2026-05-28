# Wokwi Simulation Testing Guide

## Prerequisites
- Build both PlatformIO environments so Wokwi can use the latest `.pio/build/controller/firmware.bin` and `.pio/build/local_node/firmware.bin`
- Open the **Wokwi Serial Monitor** and keep it attached to the central controller
- Start the simulation after the build completes

---

## Communication Method

The local node connects to the central controller over WiFi.
The central ESP32 hosts a WiFi access point and the local node sends HTTP requests to it.
The request button and servo valve stay on the local node.
The sensors, RGB LED, buzzer, and OLED stay on the central controller.

## Full Refill Cycle Test

**Before you start:**
- ✅ Make sure the central ESP32 is built from the `controller` PlatformIO environment
- ✅ Make sure the local node is built from the `local_node` PlatformIO environment
- ✅ Check that the request button is wired to `localNodeEsp:D13` and the valve servo is wired to `localNodeEsp:D4`
- ✅ Leave the controller serial monitor open so you can see the Wi‑Fi and refill logs

**Expected Serial Output:**
```
[WIFI] AP started at 192.168.4.1
System Started
Ready
```

**Visual Check:**
- ✅ RGB LED should be **GREEN** (ready state)
- ✅ OLED should display "System Started / Ready"
- ✅ Local node should eventually log that it connected to the controller AP

---

### Step 2: Monitor Idle State (Water Level Monitoring)
**Time: 5-10 seconds**

**Expected Serial Output (every 200ms):**
```
Water Level: 98.03 cm
[WIFI] Connected, IP=192.168.4.x
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
[NODE] Sending refill request to central controller
[NODE] Central response: APPROVE
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
[REFILL DONE]
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

## Troubleshooting

**Buttons don't respond:**
- ✅ Check Wokwi diagram connections for the local node request button and central controller sensors
- ✅ Confirm that both firmware files were rebuilt after your last code change

**Water level stuck at 98.03:**
- ✅ This is normal (HC-SR04 in Wokwi is simulated at fixed distance)

**No serial output:**
- ✅ Make sure serial monitor is open and set to **115200 baud**
- ✅ Reload Wokwi page

**RGB LED doesn't change:**
- ✅ Verify resistors (r1, r2, r3) are connected to RGB
- ✅ Check RGB pinout (D14=RED, D27=GREEN, D33=BLUE)

**Refill denied immediately:**
- ✅ Make sure the central ESP32 is built from the `controller` PlatformIO environment
- ✅ Check that the local node prints a Wi‑Fi connection line in its serial output
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
