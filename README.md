# WWVB Emulator for ESP32

This is an emulator for the WWVB time signal running on an Adafruit Huzzah32 Featherboard (ESP32).

## Overview

The goal is to create a small device that gets the current time via NTP and generates an emulated WWVB signal on a GPIO pin. This allows atomic clocks to synchronize even when the real WWVB signal from Fort Collins, Colorado is weak or unavailable.

**WWVB** is a time signal radio station near Fort Collins, Colorado, operated by NIST (National Institute of Standards and Technology). It broadcasts on 60 kHz and is used by atomic clocks throughout North America for time synchronization.

The signal currently looks like this:

![Scope image showing signal](https://github.com/tgoodhew/WWVB-ESPIDF/blob/main/images/ScopeOutput.png?raw=true)

And is showing up as a nice spike on a spectrum analyzer (direct connection via a 20 dB attenuator) — the noise is 70 dB down from the peak.

![Spectrum Analyzer image showing a peak at 60KHz](https://github.com/tgoodhew/WWVB-ESPIDF/blob/main/images/SAOutput.png?raw=true)

## Hardware Requirements

- **ESP32 Development Board**: Adafruit Huzzah32 Featherboard (or compatible ESP32 board)
- **Antenna** (optional): For over-the-air transmission to atomic clocks
- **Development Environment**: ESP-IDF v5.x or higher

## Building

### Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) v5.x installed
- VS Code with the [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-tools) (recommended)

### Using VS Code

1. Open this repository folder in VS Code.
2. The ESP-IDF extension reads `.vscode/settings.json` and picks up the configured IDF path automatically.
3. Click **Build** in the status bar (or run **ESP-IDF: Build your project** from the Command Palette).
4. Select your serial port via **ESP-IDF: Select port to use** in the Command Palette (e.g. `COM3` on Windows).
5. Click **Flash** in the status bar to write the firmware to the device.
6. Click **Monitor** to open the serial console and watch the output.

### Using the command line

```bash
idf.py build
idf.py -p COM3 flash monitor   # Windows — substitute your actual COM port
idf.py -p /dev/ttyUSB0 flash monitor  # Linux / WSL / macOS
```

## Configuration

WiFi credentials are provisioned over Bluetooth Low Energy (BLE) on first boot using the **ESP BLE Provisioning** mobile app (available on iOS and Android).

1. Flash the firmware and open the serial monitor.
2. Note the `Provisioning PoP:` line printed to the console — this is your proof-of-possession PIN.
3. Open the **ESP BLE Provisioning** app and scan for the device (it appears as `PROV_XXXXXX`).
4. Enter the PoP when prompted, then provide your Wi-Fi SSID and password.
5. Credentials are stored in NVS flash and are used automatically on every subsequent boot.

The NTP server (`pool.ntp.org` by default) and GPIO pin assignments can be changed in `main/main.c`.

## Testing

The project includes a unit test suite in the `test/` subdirectory. The tests use the **Unity** test framework that ships with ESP-IDF and run on real ESP32 hardware.

> **Note:** The `test/` folder is a **separate ESP-IDF project**. You must treat it as its own project when building and flashing — do not confuse it with the root project.

### Prerequisites

- VS Code with the [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-tools) installed and configured.
- A physical ESP32 board connected via USB (tests run on device, not in a simulator).

### Option A — VS Code (recommended for Windows)

#### 1. Open the test project

The ESP-IDF extension builds whichever `CMakeLists.txt` is at the workspace root. To switch it to the test sub-project:

1. Open the Command Palette (`Ctrl+Shift+P`).
2. Run **ESP-IDF: Open ESP-IDF Project** and select the `test/` folder.  
   _Alternatively_, open a new VS Code window with `File → Open Folder` pointing to `test/`.

#### 2. Select the serial port

1. Open the Command Palette and run **ESP-IDF: Select port to use**.
2. Choose the COM port your ESP32 is connected to (e.g. `COM3` on Windows).

#### 3. Build the test firmware

Click **Build** in the ESP-IDF status bar, or run:

**Command Palette:** `ESP-IDF: Build your project`

#### 4. Flash the test firmware

Click **Flash** in the ESP-IDF status bar, or run:

**Command Palette:** `ESP-IDF: Flash your project`

#### 5. View test results

Click **Monitor** in the ESP-IDF status bar, or run:

**Command Palette:** `ESP-IDF: Monitor your device`

The Unity test runner prints results to the serial port. You will see output similar to:

```
Running test_BitsEncoder_zero...
PASS
Running test_EncodeMinute_42...
PASS
...
-----------------------
17 Tests 0 Failures 0 Ignored
OK
```

A line ending in `OK` means all tests passed. Any `FAIL` lines include the file, line number, and failure message.

### Option B — Command line (Git Bash / WSL / PowerShell)

```bash
# Build only
cd test
idf.py build

# Flash and monitor in one step
idf.py -p COM3 flash monitor          # Windows COM port
idf.py -p /dev/ttyUSB0 flash monitor  # Linux / WSL / macOS
```

> **Windows note:** Native Windows COM ports are specified as `COM3`, `COM4`, etc.  
> Inside WSL they map to `/dev/ttyS2`, `/dev/ttyS3`, etc. (COM port number minus 1).

### Interpreting results

| Output | Meaning |
|--------|---------|
| `PASS` | The test assertion succeeded. |
| `FAIL` | An assertion failed; the file and line number are shown. |
| `IGNORE` | The test was skipped with `TEST_IGNORE()`. |
| `OK` at the end | All non-ignored tests passed. |
| `FAIL` at the end | One or more tests failed — check the individual `FAIL` lines above. |

If the monitor shows garbage or nothing, check that no other program (another monitor window, a serial terminal) is holding the COM port open.
