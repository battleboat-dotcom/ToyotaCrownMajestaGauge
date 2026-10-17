# Toyota Crown Majesta CAN Dashboard

**Vehicle:** 2009 Toyota Crown Majesta, 4.6-litre V8  
**Controller:** NodeMCU-32S / ESP32  
**Firmware:** `MajestaDashboard.ino`, current 15-page revision  
**Document date:** 17 October 2026

## Contents

- [Overview](#overview)
- [Hardware](#hardware)
- [System overview](#system-overview)
- [ESP32 pin allocation](#complete-esp32-pin-allocation)
- [OBD and transceiver wiring](#obd-socket-to-can-transceiver)
- [Power and grounding](#power-and-grounding)
- [OLED](#oled-wiring-and-layout)
- [Buttons](#external-page-buttons)
- [LEDs](#indicator-leds)
- [Pages](#current-page-order)
- [Readings and conversions](#diagnostic-readings-and-conversions)
- [CAN communication](#how-the-can-communication-works)
- [Update rates](#update-rates-and-responsiveness)
- [Minimum and maximum](#minimum-and-maximum-values)
- [Run timer](#engine-run-timer)
- [Status messages](#status-messages)
- [Setup and uploading](#programming-and-normal-use)
- [Troubleshooting](#troubleshooting)
- [Firmware structure](#firmware-structure)
- [Limitations](#current-limits-and-future-additions)
- [Project files](#project-files)
- [References](#technical-references)

## Overview

A standalone OLED dashboard that reads diagnostic information from the Majesta through its under-dash OBD socket. An ESP32 sends current-data requests over CAN through an SN65HVD230 transceiver, decodes the replies, and displays the results on a 128 × 64 OLED.

Two external buttons select pages. A blue LED indicates recent successful diagnostic communication; a red LED flashes with received CAN traffic. The firmware includes large single-reading pages, paired bar gauges, minimum/maximum tracking, an engine-run timer, and a CAN diagnostics page.

This project targets a 2009 Toyota Crown Majesta with the 4.6-litre V8, using a NodeMCU-32S ESP32 board. Other vehicles and ESP32 variants may require changes. All connections below use **ESP32 GPIO numbers**, not physical header positions.

### Verification status

- The initial diagnostic dashboard was tested on the target vehicle and reported working.
- The external buttons and LEDs were verified using the hardware-test sketch.
- The larger OLED layout and yellow/blue boundary adjustment have been checked on the fitted display.
- The newest fuel, run-time, statistics and page-order revisions have been written and source-reviewed; successful compilation/upload and vehicle operation of this exact latest revision have not yet been confirmed.
- Fuel-level support on the selected Majesta ECU is not yet confirmed.
- Throttle was observed near 19%; faster polling was added, but the cause of a seemingly fixed throttle reading has not yet been established.

## Hardware

| Part | Role / specification |
|---|---|
| NodeMCU-32S ESP32 board | Runs the dashboard; contains the TWAI CAN controller |
| SN65HVD230 CAN transceiver module | Converts between ESP32 logic and differential CAN-H/CAN-L |
| OBD-II breakout/extension cable | Accesses the vehicle CAN lines without cutting its wiring loom |
| SSD1306 I2C OLED | 0.96-inch, 128 × 64; I2C address `0x3C`; yellow upper region and blue lower region on the fitted display |
| Two momentary switches | Normally open; next and previous page |
| Blue indicator LED | Recent OBD connection |
| Red indicator LED | CAN reception activity |
| Two 1 kΩ resistors | One series resistor for each LED |
| Wiring / enclosure | Short, secure connections and mechanical protection |
| ESP32 power source | USB is established for programming/testing; permanent vehicle supply implementation has not been recorded |

**No MCP2515 is used.** The ESP32 provides the CAN controller; the SN65HVD230 provides the physical interface.

## System overview

```text
Crown Majesta under-dash OBD socket
       pin 6 CAN-H  ------------------- CAN-H
       pin 14 CAN-L ------------------- CAN-L
       signal ground ------------------ GND
                                        |
                               SN65HVD230 module
                               D / TXD <- ESP32 GPIO17
                               R / RXD -> ESP32 GPIO16
                               VCC     <- ESP32 3V3
                               GND     -- ESP32 GND
                                              |
                                   NodeMCU-32S ESP32
                                     | GPIO21/22 -> OLED
                                     | GPIO25/26 <- buttons
                                     | GPIO32   -> blue LED
                                     | GPIO33   -> red LED
                                     | USB      -> programming/power
```

The CAN lines go to the transceiver, **not directly to ESP32 GPIO pins**.

## Complete ESP32 pin allocation

| ESP32 label / GPIO | Connection | Direction relative to ESP32 |
|---|---|---|
| GPIO17 | Transceiver transmit-data input, `D`, often labelled `TXD` | Output |
| GPIO16 | Transceiver receive-data output, `R`, often labelled `RXD` | Input |
| GPIO21 | OLED `SDA` | I2C data |
| GPIO22 | OLED `SCL` | I2C clock |
| GPIO25 | Next-page switch; other contact to GND | Input with internal pull-up |
| GPIO26 | Previous-page switch; other contact to GND | Input with internal pull-up |
| GPIO32 | Blue LED through its own 1 kΩ resistor | Output, active high |
| GPIO33 | Red LED through its own 1 kΩ resistor | Output, active high |
| 3V3 | SN65HVD230 VCC; suitable 3.3 V OLED supply | Power |
| GND | Transceiver, OLED, buttons and LED returns; vehicle signal reference | Common ground |
| USB connector | Programming, Serial Monitor and bench power | USB |

The firmware defines signal assignments; it does not detect power or ground wiring. Check every physical connection against this table before powering the circuit.

## OBD socket to CAN transceiver

### Vehicle-side signal connections

| OBD pin | Standard function | Dashboard connection |
|---|---|---|
| **6** | CAN-H | Transceiver `CANH`, `CAN-H` or `H` |
| **14** | CAN-L | Transceiver `CANL`, `CAN-L` or `L` |
| **5** | Signal ground | Common ESP32/transceiver ground reference |
| **4** | Chassis ground | Identify separately; exact use in this build has not been documented |
| **16** | Vehicle battery positive | Not a direct logic supply; see power section |

Pins 6 and 14 were confirmed for the current connection. The prototype documentation does not establish whether the fitted cable uses pin 4, pin 5, or both for ground. **Pin 5 is the signal-reference connection shown in this manual; verify the fitted cable rather than assuming its wire colours.** Do not arbitrarily bridge ground pins while reconstructing the harness.

Use the connector's moulded pin numbers or continuity-test an unplugged breakout cable. A socket front view, a mating plug and a solder-side view can be mirrored. Do not identify pins solely by an unlabelled photograph.

References: [OBD connector overview](https://www.csselectronics.com/pages/obd2-explained-simple-intro) and [manufacturer CAN adapter pinout](https://www.csselectronics.com/products/obd2-db9-adapter-cable).

### ESP32-side transceiver connections

| SN65HVD230 module signal | Connect to |
|---|---|
| `VCC` / `3V3` | ESP32 **3V3** |
| `GND` | ESP32 **GND**, shared with the vehicle signal reference |
| `D` / driver input / normally `TXD` | ESP32 **GPIO17** |
| `R` / receiver output / normally `RXD` | ESP32 **GPIO16** |
| `CANH` / `H` | OBD **pin 6** |
| `CANL` / `L` | OBD **pin 14** |

Follow signal function if module labels differ: GPIO17 must feed the driver's input, and GPIO16 must receive the receiver's output. Header layout varies by module.

For the bare SN65HVD230 chip: pin 1 is D, 2 GND, 3 VCC, 4 R, 5 Vref, 6 CANL, 7 CANH, and 8 RS. These are **chip pins, not module header positions**. RS selects operating mode; the firmware does not control it. Retain the working module configuration. [Texas Instruments datasheet](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf)

### The removed termination resistor

For this vehicle connection, the module's **120 Ω resistor across CAN-H and CAN-L is removed**. Keep it removed for the same vehicle tap. The vehicle bus already has its termination; this reader is an additional node, rather than a new bus end. A separate bench CAN network may need its own end termination. [CAN termination guidance, TI datasheet](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf)

## Power and grounding

The SN65HVD230 is a **3.3 V device**. Supply it from 3V3, not directly from the vehicle supply. [TI datasheet](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf)

For the OLED, use a module rated for 3.3 V operation and keep I2C logic at 3.3 V. The exact fitted OLED supply connection was not recorded; the code only establishes SDA, SCL and address.

For bench tests, power the ESP32 by USB. Vehicle pin 16 must **never** connect directly to 3V3, a GPIO, OLED VCC or transceiver VCC.

A permanent vehicle-powered installation needs a suitable protected automotive supply feeding the board's documented power input, with appropriate fusing and transient/reverse-polarity protection. A permanent vehicle supply is not specified or validated by this repository. Avoid simultaneously feeding USB and an external board power input unless the board's power arrangement supports it.

The current firmware has no ignition-based shutdown or deep sleep. Account for battery drain if a future supply remains powered with the car off.

## OLED wiring and layout

| OLED terminal | ESP32 connection |
|---|---|
| SDA | GPIO21 |
| SCL | GPIO22 |
| GND | GND |
| VCC | Suitable 3.3 V supply as described above |

Firmware configuration:

```cpp
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Wire.begin(21, 22);
Wire.setClock(400000);
display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
```

There is no separate reset GPIO assigned. The header reads `Majesta Gauge` followed by the page reference, for example `2/15`. The home page reads Toyota / Crown / Majesta on three lines. Gauge labels begin at pixel row 16, below the yellow area. This colour division is a physical property of the OLED; the firmware draws monochrome pixels.

Single gauges use larger current values with min/max below. Combined gauges show two current values and horizontal bars. These bars show **current values**, not historical plots.

## External page buttons

```text
GPIO25 -------- [ normally-open NEXT switch ] -------- GND
GPIO26 -------- [ normally-open PREV switch ] -------- GND
```

Both inputs use `INPUT_PULLUP`: released reads HIGH, pressed reads LOW. No external pull-up is required for the existing short-wire arrangement. Do not connect either switch to 5 V or vehicle battery positive.

- One press moves one page; holding does not auto-repeat.
- Next wraps from page 15 to page 1.
- Previous wraps from page 1 to page 15.
- Software debounce is 30 ms.
- When both inputs are stably pressed, further page changes are ignored; staggered presses can still register the first press.
- BOOT no longer changes pages. EN/RST still restarts the board.

On a four-legged tactile switch, two legs may already be internally joined. Use contacts that are open at rest and joined only when pressed. Use the included hardware-test sketch to check the button wiring independently of CAN.

## Indicator LEDs

```text
GPIO32 ---- 1 kΩ ---- blue LED anode (+)
                         cathode (-) ---- GND

GPIO33 ---- 1 kΩ ---- red LED anode (+)
                         cathode (-) ---- GND
```

Use ordinary low-current indicator LEDs. Each LED needs its own resistor. The resistor can sit on either side of its LED as long as it is in series. The anode is usually the longer leg; the flat edge usually marks the cathode.

| LED | Exact meaning in the current code |
|---|---|
| Blue | CAN controller running and at least one accepted diagnostic reply within the last five seconds |
| Red | Flashes when any CAN frame is received, including frames unrelated to the dashboard's diagnostic requests |

The red pulse target is 40 ms, limited to about five flashes per second. Loop/display activity can lengthen pulses slightly. Blue does not merely indicate power or successful driver installation. Red activity without blue means raw CAN reception exists but recent valid diagnostic replies do not.

## Current page order

| Page | Display | Statistics / graphic |
|---|---|---|
| 1 | Toyota Crown Majesta | Home page; remains until a button press |
| 2 | Coolant temperature | Current, minimum, maximum |
| 3 | Intake-air temperature | Current, minimum, maximum |
| 4 | Engine RPM | Current, minimum, maximum |
| 5 | Road speed | Current, minimum, maximum |
| 6 | Calculated engine load | Current, minimum, maximum |
| 7 | Absolute throttle position | Current, minimum, maximum |
| 8 | Fuel level | Current, minimum, maximum, if supported |
| 9 | Engine run time | Current/latest run, shortest and longest completed runs |
| 10 | Coolant + intake temperature | Paired bar gauges |
| 11 | RPM + speed | Paired bar gauges |
| 12 | Load + throttle | Paired bar gauges |
| 13 | Run time + fuel | Timer and fuel bar gauge |
| 14 | Engine warning light + DTC count | MIL ON/OFF and stored emissions-code count |
| 15 | CAN/OBD diagnostics | Traffic status, frame count, reply count, ECU ID |

### Bar scales

| Reading | Bar range |
|---|---|
| Coolant | -40 to 130 °C |
| Intake | -40 to 100 °C |
| RPM | 0 to 8,000 rpm |
| Speed | 0 to 180 km/h |
| Load / throttle / fuel | 0 to 100% |

These are display scales, **not Toyota operating limits, redlines or alarm thresholds**. Numbers can exceed a bar range; the bar then remains full. No temperature alarm is implemented.

## Diagnostic readings and conversions

All transmitted diagnostic requests use **service 01: current data**. In the table below, A and B are the first and second value bytes after the response service and PID.

| Reading | PID (hex) | Conversion |
|---|---|---|
| Coolant | `05` | A - 40 °C |
| Intake air | `0F` | A - 40 °C |
| RPM | `0C` | (256 × A + B) / 4 |
| Speed | `0D` | A km/h |
| Calculated load | `04` | A × 100 / 255 % |
| Absolute throttle | `11` | A × 100 / 255 % |
| Fuel tank level | `2F` | A × 100 / 255 % |
| MIL / DTC count | `01` | A bit 7 = MIL; A bits 0–6 = count |

The MIL/count reading is not a list of actual fault codes. Fuel is a percentage, not litres or remaining range. Absolute throttle is not accelerator-pedal travel and need not read zero when closed. The observed 19% should not be corrected by simply subtracting 19. [Service 01 PID reference](https://www.csselectronics.com/pages/obd2-pid-table-on-board-diagnostics-j1979)

## How the CAN communication works

The original logger used listen-only mode and printed raw IDs such as `0x440`, `0x442`, `0x443`, `0x44D` and `0x45C`. Those proprietary message payloads have not been decoded in this project.

The dashboard instead uses the following configuration, taken from the current source:

- ESP32 TWAI controller at **500 kbit/s**.
- **Normal mode**, allowing transmission and CAN acknowledgements.
- Standard 11-bit request/reply identifiers.
- Receive filter accepts all IDs; receive queue holds 100 frames.
- One queued transmit frame; single-shot transmission requested.
- Only single-frame ISO-TP service 01 replies are decoded.

At startup it sends a functional request to `0x7DF` for PID `00`:

```text
02 01 00 00 00 00 00 00
```

It selects the first valid matching responder in `0x7E8`–`0x7EF`. Subsequent requests use that response ID minus eight: for example, responder `0x7E8` gives request ID `0x7E0`. It does not merge readings from multiple ECUs or prove that the first responder is the intended engine ECU. Serial Monitor reports the selected ID.

If the first support mask advertises PID `20`, the firmware requests the next support block from the same ECU to discover PID `2F` fuel support. This avoids assuming that every standard PID is implemented.

The firmware sends no clear-code commands, actuator tests or coding commands. It is nevertheless an **active diagnostic reader**, not a passive logger.

## Update rates and responsiveness

| Setting | Current value |
|---|---|
| Minimum interval between request attempts | 40 ms |
| Ordinary response timeout | 250 ms |
| Support-discovery response timeout | 700 ms |
| Discovery retry interval | 5 seconds |
| OLED redraw target | 50 ms / up to 20 redraws per second |
| Stale-reading threshold | More than 5 seconds |
| Special RPM request priority | About every 250 ms, subject to outstanding replies |

Three of every four normal scheduler selections prioritise the visible gauge(s). The fourth cycles background readings. Home, fault and diagnostics pages prioritise RPM/load/throttle. RPM additionally receives periodic priority on all pages for the run timer.

Only one request is outstanding at a time. Actual sample rates depend on ECU response time, timeouts, OLED transfers and other loop work. A 20 Hz display does not mean each sensor receives 20 fresh values per second. Faster polling improves responsiveness but cannot make a fixed ECU response change.

## Minimum and maximum values

Each accepted reading updates its minimum and maximum even when its page is not visible. Both start at the first valid sample, rather than an artificial zero. Later zero-RPM samples are legitimate and can become the RPM minimum.

Statistics remain in RAM only. Restarting, resetting, uploading or removing power clears them. Changing pages does not reset them. Min/max are sampled observations; very brief peaks between requests may be missed. The current value may display STALE while previously recorded extrema remain visible.

## Engine run timer

The timer is calculated locally from RPM, not from the standard ECU run-time PID.

1. First fresh RPM above zero starts a run at zero elapsed time.
2. Time accumulates while the engine is considered running and RPM data remains fresh.
3. A fresh zero-RPM reading completes the run.
4. The latest duration holds until the next positive-RPM transition.
5. The next start resets the current duration for a new run.
6. MIN/MAX on the timer page mean shortest/longest **completed runs** since ESP32 startup.

The display uses `hours:minutes:seconds`. Before the first completed run, timer MIN/MAX show dashes.

If the ESP32 starts while the engine is already running, it can measure only from its first observed positive RPM. If RPM stops arriving, the last sample remains fresh for up to five seconds; the timer then pauses and displays STALE. Missing traffic is not treated as a zero-RPM stop. An engine stop/start occurring entirely during lost communication cannot be reconstructed. The run may not complete if the ECU powers down before answering with zero RPM.

## Status messages

| Message | Meaning |
|---|---|
| WAIT | Required support discovery has not completed |
| N/A | Selected ECU does not advertise that PID |
| --- | Supported reading has not yet supplied a valid sample |
| STALE | Last valid sample is more than five seconds old |
| CAN: traffic | A CAN frame was received within the last two seconds |
| CAN: no recent RX | No frames yet, or no frame within two seconds |
| OBD: searching | No ECU selected yet |
| Driver FAILED | CAN driver installation or startup failed |
| CAN recovering | Bus-off recovery was initiated |

The CAN frame count includes all frames drained from the controller. The OBD reply count includes accepted support-discovery and current-data responses, not every raw frame printed in Serial Monitor. Counters reset on restart and are not persistent logs.

## Programming and normal use

### Software requirements

- Arduino IDE with the ESP32 board package installed.
- Adafruit GFX library.
- Adafruit SSD1306 library and any dependency requested by Library Manager.
- `Wire.h` and `driver/twai.h`, supplied with the selected ESP32 environment.

Retain the board selection and ESP32 package version that compiled the earlier working sketch; exact versions have not been recorded.

### Download

Download the repository as a ZIP using GitHub's **Code → Download ZIP**, extract it, and open the sketch under `firmware/MajestaDashboard/`. Alternatively, clone the repository using its GitHub URL.

### Upload procedure

1. Save a backup of the working sketch.
2. Open the complete `MajestaDashboard.ino` file in its matching `MajestaDashboard` folder.
3. Select the board and USB port used successfully before.
4. Click Verify, then Upload.
5. Open Serial Monitor at **115200 baud**.
6. Confirm the startup text and selected ECU/support-mask lines.
7. Test parked with ignition on; run the engine when checking live engine values.
8. Use the external buttons to scroll through the 15 pages.

Keep the entire file intact. If compilation reports `expected '}' at end of input`, make sure the file was downloaded in full. Do not repair a truncated download by adding closing braces.

## Serial Monitor examples

```text
OBD TX 0x7DF: 02 01 00 00 00 00 00 00
OBD RX 0x7E8: 06 41 00 ...
Selected ECU 0x7E8; support mask 0x...
OBD TX 0x7E0: 02 01 0C 00 00 00 00 00
OBD RX 0x7E8: 04 41 0C ...
```

These are illustrative prefixes, not a captured complete exchange. The actual ECU ID and supported readings must come from the car. `No reply to PID XX` means the outstanding request timed out; it does not itself prove that the PID is unsupported.

## Troubleshooting

| Symptom | Checks |
|---|---|
| Buttons and LEDs all fail | Confirm latest firmware, common GND and actual GPIO labels; run the hardware-test sketch |
| One button never changes | Check switch contact pairing; released should read 1, pressed 0 |
| LED never lights | Check polarity, series resistor and correct GPIO; use hardware test |
| Red flashes but blue stays off | CAN reception works; check ignition, diagnostic requests/replies and selected ECU |
| OLED blank | Check power, GND, SDA21, SCL22 and address `0x3C`; inspect serial startup errors |
| No CAN reception | Check CAN-H to OBD6, CAN-L to OBD14, common ground and transceiver power |
| Fuel shows N/A | Selected ECU does not advertise PID 2F; a Toyota-specific alternative is future work |
| Fuel stays WAIT | Extended support discovery has not completed; inspect PID 20 replies |
| Throttle stays near 19% | Inspect raw `41 11` responses; absolute throttle is not pedal position; issue remains unconfirmed |
| Run time never completes | ECU must report zero RPM; disappearing communication is not a stop event |
| Min/max vanished | Board reset or lost power; RAM-only statistics are expected to reset |
| Repeated bus-off recovery | Investigate wiring, power, transceiver mode, grounding and bus conditions |
| Compile error at file end | Check the sketch was copied/downloaded completely |

### Hardware-test sketch

`MajestaHardwareTest.ino` alternates the LEDs once per second and prints raw button states at 115200 baud. It does not need the OLED or car connection. Disconnect the vehicle cable and power by USB for that isolated test. This test has been used successfully on the prototype.

## Firmware structure

| Function / data | Purpose |
|---|---|
| `PageId` | Named page order shared by display and polling logic |
| `readings[]` | PID, label, units, current sample and timestamp |
| `supports()` | Checks the two discovered support masks |
| `receiveCAN()` | Receives frames, validates replies, decodes values, records min/max |
| `requestPID()` | Queues one service 01 request |
| `pollOBD()` | Discovery, request scheduling, timeouts and bus-off recovery |
| `updateRuntime()` | RPM-based start/stop timer and completed-run statistics |
| `updateIndicators()` | Blue connection indication and red pulse timing |
| `buttonTick()` | Debounced external next/previous buttons |
| `drawSingleGauge()` | Large current number and min/max |
| `drawGauge()` | Half-screen current number and bar |
| `drawRuntime()` | Full-screen or half-screen timer |
| `drawFaultStatus()` | MIL and stored-code count |
| `drawScreen()` | Header and current page drawing |

## Current limits and future additions

Not implemented: actual fault-code retrieval, code clearing, transmission temperature, oil temperature, pedal-position decoding, door/light/brake states, historical plots, temperature alarms, Wi-Fi dashboard, SD logging, persistent statistics or automatic power management.

The existing equipment is a working hobby prototype. No claim has been established for permanent automotive electrical qualification, environmental protection or calibrated instrument accuracy. The main remaining documentation gaps are the exact board/package versions, module header photograph, fitted OBD ground conductor, OLED supply wiring and permanent power arrangement.

## Project files

- Main firmware: [MajestaDashboard.ino](firmware/MajestaDashboard/MajestaDashboard.ino)
- Public documentation: this README.
- Isolated wiring test: [MajestaHardwareTest.ino](tools/MajestaHardwareTest/MajestaHardwareTest.ino)

```text
majesta-can-dashboard/
├── README.md
├── firmware/
│   └── MajestaDashboard/
│       └── MajestaDashboard.ino
└── tools/
    └── MajestaHardwareTest/
        └── MajestaHardwareTest.ino
```

Keep this README with the firmware. Future revisions should update the pin allocation, page list and behavioural details whenever the code changes.

## Technical references

- [Texas Instruments SN65HVD230 datasheet](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf) — transceiver signals, supply and termination.
- [Espressif TWAI documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/twai.html) — ESP32 CAN controller interface.
- [Espressif Arduino GPIO documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/gpio.html) — digital inputs/outputs and internal pull-ups.
- [CSS Electronics OBD connector overview](https://www.csselectronics.com/pages/obd2-explained-simple-intro) — connector signals.
- [CSS Electronics OBD/CAN adapter pinout](https://www.csselectronics.com/products/obd2-db9-adapter-cable) — CAN-H/CAN-L connections.
- [CSS Electronics service 01 PID reference](https://www.csselectronics.com/pages/obd2-pid-table-on-board-diagnostics-j1979) — standard readings and conversions.

The included sketch is the authority for GPIO assignments, display order and implemented behaviour. General diagnostic standards do not guarantee that this particular ECU supplies every reading.

