# Hospital Automated Chemical Dosing System

This repository contains the IoT project implementing an automated chemical dosing solution for hospital use. It includes hardware schematics, system diagrams, WokWi/Arduino simulation sketches, and the LaTeX source for the final report.

Project layout:

- `simulation/` — WokWi and Arduino sketches used for prototype simulation.
- `simulation/central/` — central ESP32 firmware for the split-node simulation.
- `simulation/WOKWI_TEST_GUIDE.md` — step-by-step Wokwi test guide for the simulation.
- `images/` — diagrams and figures used in the report.
- `hospital_iot_dispensing_report.tex` — LaTeX source for the project report (build to PDF with your LaTeX toolchain).

Quick notes:

- To preview the simulation, open the sketch in WokWi and use the guide in `simulation/WOKWI_TEST_GUIDE.md`.
- The simulation now includes the RGB LED fix, ultrasonic distance override, and a two-ESP32 split-node workflow over WiFi.
- To rebuild the report, run your usual `pdflatex`/`latexmk` commands on `hospital_iot_dispensing_report.tex`.

That's the short project summary and structure.
