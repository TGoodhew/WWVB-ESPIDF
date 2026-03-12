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

Unit tests live in `test/`, which is a separate ESP-IDF project. There are 27 test cases verified passing on hardware.

### Test infrastructure

- `test/main/test_main.c` — Unity entry point and codec-level test cases (tags: `[wwvb]`).
- `test/main/main_under_test.c` — compiles `main/main.c` directly into the test app and intercepts hardware calls (tags: `[main][...]`).
- `main/wwvb_codec.c` — compiled into both projects.

The test runner uses `unity_run_menu()` (interactive Unity menu over serial). Enter `*` to run all tests.

### How the hardware interception works

`main_under_test.c` uses `#define` macro substitution to redirect every hardware or OS call to a deterministic test double before including `main.c`. The calls intercepted are:

- `time()` → returns a pre-set `fake_now` timestamp
- `esp_timer_get_time()` → returns a pre-set `fake_now_us`
- `esp_timer_start_once()` / `esp_timer_start_periodic()` → records handle and period; returns `ESP_OK`
- `esp_timer_is_active()` → returns true only for a designated handle
- `gpio_set_level()` → records GPIO number and level
- `ledc_set_duty()` / `ledc_update_duty()` → records LEDC duty value
- `xTaskNotifyGive()` → records which task was notified
- `esp_wifi_connect()` → counts calls; returns `ESP_OK`
- `xEventGroupSetBits()` → accumulates bits into a test-visible OR mask
- `esp_wifi_get_mac()` → returns a pre-set fake MAC address

All captured values are held in a `test_fakes_t` struct and reset to a known state before each test.

### Run tests

```bash
cd test
idf.py build
idf.py -p COM3 flash monitor
```

### Test cases

#### Codec unit tests (`[wwvb]`) — `test_main.c`

These tests call codec functions in isolation with no hardware dependency.

| # | Test name | What it tests | Why it matters |
|---|-----------|---------------|----------------|
| 1 | `BitsEncoder encodes BCD values` | `BitsEncoder()` converts integers 0, 59, and 365 to their BCD bit-field representation | Every encoder function relies on `BitsEncoder`; a wrong mapping corrupts the entire frame |
| 2 | `encodeYear maps 2025 into expected bit positions` | The 2-digit year 25 appears correctly across slots 45–53 | Wrong year slots cause clocks to set the wrong year |
| 3 | `encodeDayOfYear maps 365 into expected bit positions` | Day 365 is written across slots 22–33 | Day-of-year errors affect time displays that show date |
| 4 | `encodeHour and encodeMinute set expected bits` | Hour 23 sets slots 12–18; minute 59 sets slots 1–8 | Incorrect hour/minute mapping produces wrong displayed time |
| 5 | `setMarkersAndIndicators sets markers and fixed zero bits` | Marker slots (0,9,19,29,39,49,59) are set to 2; reserved slots are forced to 0 | Receiving clocks use markers for frame synchronisation; wrong values prevent lock |
| 6 | `setDUT1 clears DUT1 slots` | Slots 36–43 are zeroed | DUT1 area is unused in this implementation; stale data would encode invalid correction values |
| 7 | `setLeapYear sets leap-year flag` | Slot 55 is 1 for 2024 (leap), 0 for 2023 (non-leap) | Some clocks display or use the leap-year flag; an incorrect flag sends bad metadata |
| 8 | `setLeapSecond and setDST set control bits` | `setLeapSecond` writes slot 56; `setDST` writes slots 57–58 | DST bits inform the clock whether to display DST; wrong bits cause clocks to show wrong local time |
| 9 | `isDaylightSavingTime returns expected seasonal values` | Returns false in January, true in mid-summer, false in late December for 2025 | Basic sanity that DST detection is not inverted |
| 10 | `isDaylightSavingTime honors 2025 transition boundaries` | `calculateDSTDays` returns day 68 (March 9) and day 306 (November 2) for 2025; transition boundary is exact (day 67 = off, day 68 = on, day 305 = on, day 306 = off) | The US DST algorithm is boundary-sensitive; an off-by-one means clocks show the wrong local time for a full year |
| 11 | `encodeMinute only updates minute bit slots` | Buffer pre-filled with `0xAA`; after `encodeMinute(42)` every slot outside 1,2,3,5,6,7,8 still holds `0xAA` | Proves the function has no side-effects on slots it does not own; encoder bugs that bleed into adjacent fields would corrupt year, day, or marker slots |
| 12 | `setMarkersAndIndicators leaves non-marker fields untouched` | Buffer pre-filled with `0xAA`; after the call every slot that is not a marker or reserved slot still holds `0xAA` | Same side-effect guard for the marker function; protects the time fields that the codec writes into the same buffer |

#### Frame-builder integration tests (`[main][frame]`) — `main_under_test.c`

These tests call `SetupWWVBArray()` directly with a controlled `time()` return value, then decode the resulting frame back to integers to verify the complete encode → decode round trip.

| # | Test name | What it tests | Why it matters |
|---|-----------|---------------|----------------|
| 13 | `SetupWWVBArray encodes next minute across year rollover` | `time()` returns 2025-12-31 23:59:30 UTC; frame must encode 00:00 January 1, 2026 | `SetupWWVBArray` adds 60 s before encoding; a year or day-of-year rollover bug would emit 23:59 day 365 year 25 instead |
| 14 | `SetupWWVBArray encodes leap-day rollover correctly` | `time()` returns 2024-02-28 23:59:30 UTC; frame must encode 00:00 day 60 year 24, with leap-year bit set | Feb 29 only exists in leap years; wrong day arithmetic would skip or double-count the leap day |
| 15 | `SetupWWVBArray encodes non-leap Feb rollover correctly` | `time()` returns 2025-02-28 23:59:30 UTC; frame must encode 00:00 day 60 year 25, with leap-year bit clear | Confirms the same rollover path reaches day 60 in a non-leap year and correctly clears the leap-year flag |

#### ISR timing tests (`[main][isr]`) — `main_under_test.c`

These tests call `TimerSecond_ISR()` directly with the slot and buffer pre-set, then inspect what the fake timer layer recorded.

| # | Test name | What it tests | Why it matters |
|---|-----------|---------------|----------------|
| 16 | `TimerSecond_ISR schedules 200ms timer for symbol 0` | When the active buffer slot holds `0`, the ISR starts `TimerBit0` with a 200 000 µs timeout | A wrong timer handle or a wrong period changes the pulse width that the receiving clock measures, causing it to misread a `0` bit |
| 17 | `TimerSecond_ISR schedules 500ms timer for symbol 1` | Same for buffer value `1` → `TimerBit1` at 500 000 µs | Same: `1` bits misread as `0` or marker |
| 18 | `TimerSecond_ISR schedules 800ms timer for marker` | Same for buffer value `2` → `TimerBitMarker` at 800 000 µs | Same: markers are how the receiver finds the frame boundary |
| 19 | `TimerSecond_ISR swaps buffers only at minute boundary` | With `slot = 0` and `stagingFrameReady = true`, the ISR exchanges `activeWWVBBuffer` and `stagingWWVBBuffer` pointers, clears `stagingFrameReady`, and increments `frames_swapped` | If the swap happened on any slot other than 0, the transmitter would switch to a partially-built frame mid-minute, corrupting the broadcast |
| 20 | `TimerSecond_ISR does not swap buffers away from slot zero` | With `slot = 1`, the same ready staging frame must not be swapped and the pointers must be unchanged | Complementary to test 19; prevents a spurious pointer swap from injecting `stagingWWVBBuffer` content mid-frame |
| 21 | `TimerSecond_ISR wraps slot and triggers health path at end of frame` | With `slot = 59`, after the ISR runs `slot` must wrap to 0 and `runtime_metrics.uptime_sec` must be set from `esp_timer_get_time()` | Slot 59 is the last symbol; failure to wrap would stop the transmitter, and failure to call `HealthCheck_ISR` would stall health telemetry |

#### DST multi-year tests (`[main][dst]`) — `main_under_test.c`

| # | Test name | What it tests | Why it matters |
|---|-----------|---------------|----------------|
| 22 | `calculateDSTDays supports multiple years` | DST start/end day-of-year for 2023 (71/309), 2024 (70/308), and 2026 (67/305) | The algorithm computes the 2nd Sunday in March and 1st Sunday in November via day-of-week arithmetic; multiple years with different weekday offsets confirm it does not hard-code a single year's answer |
| 23 | `isDaylightSavingTime matches start and end boundaries` | Exact boundary days for 2024 and 2026, both the last day off and first day on at each transition | Protects against off-by-one errors in the inclusive/exclusive boundary logic |

#### Wi-Fi and provisioning tests (`[main][wifi]`) — `main_under_test.c`

| # | Test name | What it tests | Why it matters |
|---|-----------|---------------|----------------|
| 24 | `get_device_service_name formats PROV suffix from MAC` | With fake MAC bytes `[3]=0xA1 [4]=0xB2 [5]=0xC3`, the function writes `PROV_A1B2C3` | The BLE advertisement name must derive from MAC bytes 3–5 so each device is uniquely discoverable; reading the wrong bytes or wrong formatting leaves all devices with the same suffix |
| 25 | `wifi_event_handler connects on STA_START only when not provisioning` | When `provisioning_in_progress = false`, `WIFI_EVENT_STA_START` calls `esp_wifi_connect` once and sets `wifi_state = WIFI_STATE_CONNECTING`; when the flag is true, no connect call is made | Calling `esp_wifi_connect` during active BLE provisioning interferes with the provisioning exchange; the guard is safety-critical |
| 26 | `wifi_event_handler retries and then sets fail bit` | Starting with `s_retry_num = 9`, the first `WIFI_EVENT_STA_DISCONNECTED` event triggers a retry and increments the counter to 10; the second event (retry limit reached) does not retry but sets `WIFI_FAIL_BIT` in the event group instead | The connect/fail event group is what unblocks the waiting task in `app_main`; if the fail path is broken the firmware hangs waiting for an event that never arrives |
| 27 | `wifi_event_handler GOT_IP updates state and counters` | On the first `IP_EVENT_STA_GOT_IP`, `wifi_state` becomes `WIFI_STATE_CONNECTED`, `s_retry_num` resets to 0, `wifi_reconnects` increments, and `WIFI_CONNECTED_BIT` is set; a second event does not double-count reconnects | The connection bit unblocks SNTP; wrong state or a double-counted reconnect would misrepresent health telemetry |

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
