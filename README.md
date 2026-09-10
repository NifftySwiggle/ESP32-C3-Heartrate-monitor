# ❤️ ESP32-C3 MAX30102 Heart Rate Monitor

<div align="center">

![Platform](https://img.shields.io/badge/Platform-ESP32--C3-blue?logo=espressif&logoColor=white)
![Language](https://img.shields.io/badge/Language-C%2B%2B%20%2F%20Arduino-00599C?logo=c%2B%2B&logoColor=white)
![Sensor](https://img.shields.io/badge/Sensor-MAX30102-red)
![Display](https://img.shields.io/badge/Display-SSD1306%2072x40-brightgreen)
![Status](https://img.shields.io/badge/Status-Experimental-orange)

**A compact ESP32-C3 pulse and blood-oxygen monitor with live waveform display, local reading history, beat indicators, and configurable buzzer feedback.**

</div>

> [!WARNING]
> This is an educational and experimental electronics project. It is **not a medical device** and must not be used for diagnosis, treatment, emergency monitoring, or clinical decisions. Readings are estimates and have not been clinically validated.

---

## 📑 Table of Contents

- [Overview](#-overview)
- [Key Features](#-key-features)
- [Hardware and Wiring](#-hardware-and-wiring)
- [Control Reference](#-control-reference)
- [Display Modes](#-display-modes)
- [Measurement Behavior](#-measurement-behavior)
- [Quick Start and Installation](#-quick-start-and-installation)
- [Troubleshooting](#-troubleshooting)
- [Project Architecture](#-project-architecture)
- [Limitations and Safety](#-limitations-and-safety)
- [Contributing](#-contributing)
- [License](#-license)

---

## 🔍 Overview

This project turns an ESP32-C3 development board into a small, self-contained optical heart-rate and SpO2 monitor. A MAX30102 sensor provides red and infrared photoplethysmography data. The firmware processes the signal, renders a scrolling waveform on a 72x40 SSD1306 OLED, and gives immediate visual and audio feedback for detected beats.

The interface is designed for one onboard BOOT button. Short presses move between screens, while long presses perform the action associated with the current screen.

## ⚡ Key Features

- **❤️ Live heart-rate estimate** displayed in beats per minute (BPM).
- **🩸 Estimated SpO2** calculated from the red/infrared signal ratio.
- **📈 Perfusion index** shown on the Settings screen as a signal-strength indicator.
- **〰️ Scrolling pulse waveform** rendered on the OLED.
- **👆 Lead-off detection** when the sensor does not see a usable finger signal.
- **🗂️ Three display screens**: Live, History, and Settings.
- **💾 Local history** storing up to eight readings in RAM.
- **⏱️ Automatic logging** approximately every 10 seconds when the reading is valid.
- **🔘 Manual logging** from the Live screen with a long button press.
- **🔔 Beat feedback** through an LED and either a passive piezo or active buzzer.
- **⚙️ Persistent buzzer setting** saved in ESP32-C3 flash using `Preferences`.

## 🛠️ Hardware and Wiring

### Required Hardware

- ESP32-C3 development board
- MAX30102 pulse-oximeter sensor module
- 72x40 SSD1306 I2C OLED, address `0x3C`
- Passive piezo or active buzzer
- LED and resistor
- 10 uF capacitor
- Breadboard and jumper wires

### ESP32-C3 Pinout

| Component | ESP32-C3 pin | Notes |
| :--- | :---: | :--- |
| OLED SDA | `GPIO 5` | I2C data; shared with MAX30102 SDA |
| OLED SCL | `GPIO 6` | I2C clock; shared with MAX30102 SCL |
| MAX30102 INT | `GPIO 10` | Interrupt input |
| Beat LED | `GPIO 2` | LED anode through a 330 ohm resistor; cathode to GND |
| Buzzer | `GPIO 3` | Positive through a 150-220 ohm resistor; negative to GND |
| BOOT button | `GPIO 9` | Uses the internal pull-up; button connects to GND |
| Sensor/display power | `3V3` | MAX30102 VIN, OLED VCC, capacitor positive terminal |
| Ground | `GND` | MAX30102 GND/PGND, OLED GND, capacitor negative terminal |

> [!NOTE]
> Use 3.3 V-compatible breakout boards. Pin labels and voltage requirements vary between MAX30102 and OLED modules, so verify the documentation for your specific hardware before powering it.

## 🎮 Control Reference

The onboard BOOT button distinguishes short and long presses:

| Input | Screen | Action |
| :--- | :--- | :--- |
| Short press, 40-599 ms | Any screen | Cycle `LIVE` -> `HISTORY` -> `SETTINGS` |
| Long press, 600 ms or longer | Live | Save the current reading when BPM is at least 40 and SpO2 is at least 85% |
| Long press, 600 ms or longer | History | Clear saved records |
| Long press, 600 ms or longer | Settings | Cycle `PIEZO` -> `ACTIVE` -> `OFF` |

## 🖥️ Display Modes

### 1. Live

The default screen shows:

- Current heart rate (`HR`) and SpO2 percentage.
- A scrolling infrared pulse waveform.
- `LEAD OFF` when the signal is below the configured threshold.
- The most recent saved reading.
- A heart icon that flashes with each detected beat.

### 2. History

The History screen shows up to the two most recent records on the compact display and calculates an average BPM across all stored records. Hold the BOOT button to wipe the in-memory history.

### 3. Settings

The Settings screen shows the current buzzer mode and perfusion index. Hold the BOOT button to cycle the buzzer mode. The selected mode is restored after reboot.

## 📊 Measurement Behavior

| Behavior | Firmware value |
| :--- | :--- |
| Sensor interface | I2C, fast mode |
| Sensor sampling | 100 Hz, red + infrared LEDs |
| Beat range accepted | 35-220 BPM for instantaneous beat samples |
| Displayed BPM validity | At least 40 BPM |
| Displayed SpO2 validity | At least 85% |
| Lead-off threshold | Infrared reading below `45000` |
| Automatic log interval | 10 seconds |
| Maximum history size | 8 records |
| Serial monitor speed | 115200 baud |

The history records and timestamps are held in RAM and are cleared when the board restarts. Only the buzzer mode is persisted in flash.

## 🚀 Quick Start and Installation

### Dependencies

Install these libraries through **Sketch > Include Library > Manage Libraries** in Arduino IDE:

1. **SparkFun MAX3010x Pulse and Proximity Sensor Library** by SparkFun
2. **Adafruit SSD1306** by Adafruit
3. **Adafruit GFX Library** by Adafruit

`Wire` and `Preferences` are included with the ESP32 Arduino core.

### Flashing via Arduino IDE

1. Install Arduino IDE 2.x.
2. Install the **ESP32** board package by Espressif through Boards Manager.
3. Install the libraries listed above.
4. Open `Heartrate-monitor.ino`.
5. Select **ESP32C3 Dev Module**, or the board profile matching your hardware.
6. Enable **USB CDC On Boot** when using the board's native USB-C connection.
7. Select an upload speed of `115200` or `921600`.
8. Connect the circuit using the wiring table.
9. Compile and upload the sketch.
10. Open the Serial Monitor at `115200` baud.

The current firmware is local-only. It does not provide Wi-Fi, JSON, MQTT, or network streaming.

## 🧰 Troubleshooting

| Symptom | Checks |
| :--- | :--- |
| `SENSOR ERR` | Check MAX30102 power, I2C wiring, module address, and library installation. |
| `LEAD OFF` | Place a still finger over the sensor and confirm stable 3.3 V power. |
| Blank OLED | Confirm address `0x3C` and that the display is a 72x40 SSD1306 module. |
| No buzzer output | Confirm the buzzer type, GPIO 3 wiring, resistor, and selected buzzer mode. |
| Upload fails | Try `115200`, verify the COM port, and hold BOOT during upload if required by the board. |

## 📂 Project Architecture

This repository is intentionally small:

```text
Heartrate-monitor/
├── Heartrate-monitor.ino   # Sensor processing, UI, button handling, and logging
├── README.md               # Project documentation
└── README.txt              # Original quick-reference wiring notes
```

## ⚠️ Limitations and Safety

- This project is not a certified pulse oximeter or patient monitor.
- SpO2 and BPM values can be affected by motion, poor sensor contact, ambient light, skin properties, temperature, and module quality.
- Do not use these readings to make medical decisions or delay professional care.
- The firmware only measures while a usable optical signal is present.
- The onboard history is temporary and is not a permanent measurement record.
- The project has not been clinically validated or tested for regulatory compliance.

## 🤝 Contributing

Issues and pull requests are welcome. Useful contributions include:

- Support for additional SSD1306 display sizes.
- Improved signal-quality and motion-artifact handling.
- Hardware compatibility notes and wiring diagrams.
- Automated tests for signal processing and record handling.
- Better persistence or export options for measurement history.

## 📄 License

No license file is currently included. Add a license before publishing or accepting external contributions so GitHub users know how the code may be reused.

