# WWVB Emulator for ESP32

This project runs on an ESP32 (tested on Adafruit Huzzah32 Feather) and generates a local WWVB-compatible 60 kHz signal so nearby atomic clocks can synchronize from a local source.

## Overview

The firmware:

- Provisions Wi-Fi over BLE using ESP-IDF provisioning manager.
- Synchronizes UTC time from SNTP (`pool.ntp.org` by default).
- Encodes WWVB time fields (year/day/hour/minute plus control bits) into a 60-second frame.
- Outputs a 60 kHz carrier on GPIO 26 and modulates it per WWVB pulse-width timing.
- Uses double buffering and minute-boundary pointer swap to avoid frame glitches.
- Emits runtime health telemetry once per minute.

WWVB is the 60 kHz NIST time broadcast from Fort Collins, Colorado.

## Current Implementation Details

This section reflects what is implemented in this branch.

- Main logic is in `main/main.c`.
- WWVB encoding helpers are in `main/wwvb_codec.c` and `main/wwvb_codec.h`.
- BLE provisioning uses Security 1 with PoP from `CONFIG_WWVB_WIFI_POP`.
- Default PoP is `abcd1234` (configured in `main/Kconfig`).
- Wi-Fi reconnect attempts are fixed at 10 retries (`s_retry_num < 10`).
- SNTP sync wait timeout is fixed at 10 seconds.
- WWVB carrier output is fixed in code to GPIO 26 at 60 kHz.
- Debug/status LED is fixed in code to GPIO 13.

## Configuration

Only one application parameter is currently exposed via `menuconfig`:

- `WWVB_WIFI_POP` in `main/Kconfig`

Configure it with:

```bash
idf.py menuconfig
```

Notes:

- NTP server, GPIO assignments, carrier frequency, and retry limits are currently hardcoded in `main/main.c`.
- In non-debug builds, code emits a compile-time warning if default PoP is still used.

## WWVB Signal Behavior

- Carrier frequency: 60 kHz (LEDC PWM)
- Modulation method: duty cycle reduced to 0% for part of each second
- Per-second symbol timing:
  - `0`: 200 ms reduced power, then restore carrier
  - `1`: 500 ms reduced power, then restore carrier
  - `M` (marker): 800 ms reduced power, then restore carrier
- Marker positions: 0, 9, 19, 29, 39, 49, 59

The serial debug stream prints `0`, `1`, and `M` each second when `WWVBDEBUG` is enabled (currently enabled in source).

## WWVB Frame Map (Implemented)

The encoder writes these fields in a 60-slot frame (`0..59`):

- Markers: `0, 9, 19, 29, 39, 49, 59` (value `2`)
- Minute bits: `1, 2, 3, 5, 6, 7, 8`
- Hour bits: `12, 13, 15, 16, 17, 18`
- Day-of-year bits: `22, 23, 25, 26, 27, 28, 30, 31, 32, 33`
- Year bits (2-digit year): `45, 46, 47, 48, 50, 51, 52, 53`
- Control/status bits:
  - Leap year: `55`
  - Leap second (currently always encoded as false): `56`
  - DST pair: `57`, `58`
- Forced-zero slots:
  - Reserved: `4, 10, 11, 14, 20, 21, 24, 34, 35, 44, 54`
  - DUT1 area (encoded as zero in this project): `36, 37, 38, 40, 41, 42, 43`

## Frame Generation and Timing

- Two 60-byte buffers are used (`activeWWVBBuffer` and `stagingWWVBBuffer`).
- A dedicated task builds the next frame into the staging buffer.
- At slot 0 (minute boundary), ISR swaps pointers if a staging frame is ready.
- First frame start is aligned to the next UTC minute boundary using SNTP callback timestamp.
- `HEALTH` log prints once per frame with uptime, frame counters, jitter/drift, Wi-Fi stats, and SNTP sync count.

## Hardware Requirements

- ESP32 development board (tested with Adafruit Huzzah32 Feather)
- Optional antenna/coupling method for nearby clock reception
- ESP-IDF v5.x environment

## Build and Flash

### VS Code (ESP-IDF extension)

1. Open the repo in VS Code.
2. Select serial port (for example `COM3` on Windows).
3. Run build.
4. Flash.
5. Start monitor.

### Command line

```bash
idf.py build
idf.py -p COM3 flash monitor
```

Use your actual port (`COMx` on Windows, `/dev/ttyUSBx` on Linux/macOS/WSL).

## Wi-Fi Provisioning (BLE)

On first boot (or after erasing NVS/flash):

1. Device advertises as `PROV_XXXXXX`.
2. Use Espressif's ESP BLE Provisioning app.
3. Enter PoP set by `CONFIG_WWVB_WIFI_POP`.
4. Provide Wi-Fi SSID/password.
5. Credentials are stored and reused on subsequent boots.

To re-provision from scratch:

```bash
idf.py -p COM3 erase-flash
idf.py -p COM3 flash monitor
```

## Testing

Unit tests live in `test/`, which is a separate ESP-IDF project.

Current test app structure:

- `test/main/test_main.c`: Unity entry point and test cases
- `test/main/main_under_test.c`: includes production `main/main.c` with renamed `app_main`
- `main/wwvb_codec.c`: compiled into test app

The test runner uses `unity_run_menu()` (interactive Unity menu over serial).

### Run tests

```bash
cd test
idf.py build
idf.py -p COM3 flash monitor
```

There are currently 10 `TEST_CASE` definitions in `test/main/test_main.c`.

## Troubleshooting

### BLE provisioning issues

- Ensure Bluetooth is enabled on your phone.
- If device does not appear, erase flash and retry provisioning.
- Verify you are entering the configured PoP exactly.

### No time sync

- Confirm Wi-Fi connected (`got ip` in logs).
- Check network allows NTP (UDP 123).
- Default server is `pool.ntp.org`.

### Clock not syncing

- Verify 60 kHz signal on GPIO 26 with scope if available.
- Move clock close to transmitter for testing.
- Confirm the clock expects WWVB (60 kHz).

### Serial monitor problems

- Verify correct COM port and 115200 baud.
- Close other tools that may hold the serial port.

## Signal Output Examples

![Scope image showing signal](images/ScopeOutput.png)

![Spectrum Analyzer image showing a peak at 60KHz](images/SAOutput.png)

## References

- [WWVB Time Code Format (NIST)](https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb/wwvb-time-code-format)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
