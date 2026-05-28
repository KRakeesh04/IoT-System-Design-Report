/*
  Distributed workflow scaffold for the hospital dispensing simulation.

  This file is a small placeholder for the next build steps:
  - central ESP32 logic
  - dispensing node logic
  - status handoff between nodes
  - Wokwi diagram updates for the second ESP32

  Keep this file light so it does not change the current working sketch yet.
*/

void setupCentralWorkflow()
{
  Serial.println("[CENTRAL] Split-node central workflow online");
}

void setupDispensingNodeWorkflow()
{
  Serial.println("[NODE] Dispensing node workflow online");
}

bool sendNodeStatusToCentral(float waterLevel, bool chemicalOk, bool refillActive)
{
  Serial.print("[CENTRAL] STATUS water=");
  Serial.print(waterLevel);
  Serial.print(" chemical=");
  Serial.print(chemicalOk ? "OK" : "EMPTY");
  Serial.print(" refill=");
  Serial.println(refillActive ? "YES" : "NO");

  return chemicalOk;
}
