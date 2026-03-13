# WWVB Emulator for ESP32

This ESP-IDF project generates a local WWVB-compatible 60 kHz signal on an ESP32 so nearby radio-controlled clocks can synchronize from a local source instead of relying on the over-the-air broadcast.

The firmware provisions Wi-Fi over BLE, synchronizes UTC from SNTP, encodes the WWVB time frame, and drives a 60 kHz carrier with WWVB-style pulse-width modulation.

## Repository Layout

- `main/main.c` contains system startup, BLE provisioning, Wi-Fi handling, SNTP synchronization, timer setup, frame buffering, and carrier modulation.
- `main/wwvb_codec.c` and `main/wwvb_codec.h` contain the WWVB frame encoding helpers.
- `main/Kconfig` defines the project-specific `menuconfig` options.
- `test/` is a separate ESP-IDF Unity test project for codec logic, frame generation, ISR timing, DST logic, and selected Wi-Fi/provisioning behavior.
- `images/` contains scope and spectrum screenshots referenced below.

## What The Firmware Does

- Uses ESP-IDF Wi-Fi provisioning over BLE with `WIFI_PROV_SECURITY_1`.
- Advertises with a provisioning service name in the form `PROV_XXXXXX`, derived from STA MAC bytes 3 through 5.
- Waits for Wi-Fi before starting SNTP.
- Uses `CONFIG_WWVB_NTP_SERVER` as the SNTP host.
- Builds a full 60-symbol WWVB frame for the next UTC minute, not the current minute.
- Uses two 60-byte buffers and swaps them only at slot `0` to avoid mid-frame corruption.
- Starts transmission on the next UTC minute boundary after the first SNTP synchronization callback.
- Drives the WWVB carrier with ESP-IDF LEDC PWM on `CONFIG_WWVB_OUTPUT_GPIO`.
- Reduces carrier duty to `0` for symbol timing, then restores duty to `127`.
- Emits `HEALTH` log lines once per minute with frame, timing, Wi-Fi, and SNTP counters.
- Prints `0`, `1`, and `M` once per second because `WWVBDEBUG` is enabled in `main/main.c` as checked in.

## WWVB Encoding Implemented Here

The encoder writes a 60-slot frame with these fields:

- Marker slots: `0, 9, 19, 29, 39, 49, 59`
- Minute slots: `1, 2, 3, 5, 6, 7, 8`
- Hour slots: `12, 13, 15, 16, 17, 18`
- Day-of-year slots: `22, 23, 25, 26, 27, 28, 30, 31, 32, 33`
- Year slots: `45, 46, 47, 48, 50, 51, 52, 53`
- Leap-year slot: `55`
- Leap-second slot: `56`
- DST slots: `57, 58`

Behavior currently implemented in code:

- Symbol `0`: carrier suppressed for `200 ms`
- Symbol `1`: carrier suppressed for `500 ms`
- Marker `M`: carrier suppressed for `800 ms`
- DUT1 slots `36, 37, 38, 40, 41, 42, 43` are always encoded as `0`
- Leap second is always encoded as `false`
- DST bits are driven from `calculateDSTDays()` and `isDaylightSavingTime()` in `main/main.c`

## Configuration

Project-specific configuration is exposed through `idf.py menuconfig` using symbols defined in `main/Kconfig`:

- `CONFIG_WWVB_WIFI_POP`
- `CONFIG_WWVB_NTP_SERVER`
- `CONFIG_WWVB_OUTPUT_GPIO`
- `CONFIG_WWVB_STATUS_LED_GPIO`
- `CONFIG_WWVB_STATUS_LED_BLINK_HZ`
- `CONFIG_WWVB_STATUS_HEARTBEAT_ENABLE`
- `CONFIG_WWVB_STATUS_HEARTBEAT_HZ`
- `CONFIG_WWVB_CARRIER_FREQ_HZ`
- `CONFIG_WWVB_WIFI_RETRY_LIMIT`

Default values from `main/Kconfig`:

- PoP: `abcd1234`
- NTP server: `pool.ntp.org`
- WWVB output GPIO: `26`
- Status LED GPIO: `13`
- Status LED wait-for-sync blink rate: `8 Hz`
- Status LED heartbeat enabled: `yes`
- Status LED heartbeat rate: `1 Hz`
- Carrier frequency: `60000 Hz`
- Wi-Fi retry limit: `10`

The checked-in top-level `sdkconfig` currently sets:

- `CONFIG_WWVB_STATUS_LED_BLINK_HZ=25`
- `CONFIG_WWVB_STATUS_HEARTBEAT_HZ=10`
- `CONFIG_WWVB_WIFI_RETRY_LIMIT=20`

Other configuration details that are hardcoded in source:

- SNTP sync wait timeout is `10 seconds`.
- The LEDC duty resolution is `8-bit`.
- The provisioning BLE service UUID is set in `main/main.c`.

Configure the application with:

```bash
idf.py menuconfig
```

For production use, change `CONFIG_WWVB_WIFI_POP` from the default value. The source includes a compile-time warning for default PoP usage in non-debug builds.

## Build

This repository is an ESP-IDF project with `idf >= 5.0` declared in `main/idf_component.yml`.

Build the firmware from the repository root:

```bash
idf.py build
```

In VS Code with the ESP-IDF extension, the equivalent workflow is: select the serial port, build, flash, and open the monitor.

## Deployment

For this project, deployment means flashing the firmware to an ESP32 board.

Flash and monitor:

```bash
idf.py -p COM3 flash monitor
```

Replace `COM3` with the correct serial port for your system.

First boot behavior:

- If the device is not provisioned, it starts BLE provisioning.
- After credentials are received and provisioning ends, it connects as a Wi-Fi station.
- After Wi-Fi connects, it initializes SNTP.
- After the first SNTP synchronization, it prepares both WWVB buffers and starts transmission on the next UTC minute boundary.

To clear saved provisioning data and start over:

```bash
idf.py -p COM3 erase-flash
idf.py -p COM3 flash monitor
```

## Wi-Fi Provisioning

When unprovisioned, the device starts BLE provisioning with:

- Security mode: `WIFI_PROV_SECURITY_1`
- PoP: `CONFIG_WWVB_WIFI_POP`
- Service name format: `PROV_XXXXXX`

Provisioning flow:

1. Boot the device with erased or uninitialized storage.
2. Open Espressif's BLE provisioning app.
3. Connect to the `PROV_XXXXXX` device.
4. Enter the configured PoP.
5. Provide Wi-Fi credentials.

If provisioning succeeds, the device deinitializes the provisioning manager and attempts to connect to the configured access point.

## Testing

Tests live under `test/` as a separate ESP-IDF Unity project.

What exists in the codebase today:

- `test/main/test_main.c` defines codec-focused Unity tests.
- `test/main/main_under_test.c` includes `main/main.c` directly with macro-based test doubles for timers, GPIO, LEDC, task notify, Wi-Fi, and event groups.
- There are `27` `TEST_CASE(...)` definitions across those two files.
- The test app starts `unity_run_menu()`, so the runner is interactive over the serial console.

Build and run the test project:

```bash
cd test
idf.py build
idf.py -p COM3 flash monitor
```

At the Unity menu, enter `*` to run all tests.

Coverage areas in the current tests:

- WWVB BCD encoding helpers
- Minute, hour, day-of-year, and year bit placement
- Marker and reserved slot handling
- Leap-year, leap-second, and DST control bits
- DST boundary calculations across multiple years
- `SetupWWVBArray()` rollover behavior
- `TimerSecond_ISR()` symbol timing and buffer swap behavior
- Provisioning service name formatting
- Selected Wi-Fi event handler state transitions and retry behavior

## Troubleshooting

### BLE provisioning does not start or does not complete

- Check that the device is not already provisioned. If it is, erase flash and boot again.
- Confirm Bluetooth is enabled on the provisioning device.
- Confirm you are using the same PoP value configured in `CONFIG_WWVB_WIFI_POP`.
- If logs show `Provisioning failed!`, the code currently reports either authentication failure or access point not found.

### Wi-Fi never connects

- Watch for `retry to connect to the AP` messages in the serial log.
- Check SSID and password entered during provisioning.
- Review `CONFIG_WWVB_WIFI_RETRY_LIMIT` if you want more or fewer reconnect attempts.
- If retries are exhausted, the code sets `WIFI_FAIL_BIT`; the current `app_main()` path still waits on `WIFI_CONNECTED_BIT` before continuing.

### Time never synchronizes

- Check for `got ip:` in the serial log before expecting SNTP to succeed.
- Verify outbound access to the host configured by `CONFIG_WWVB_NTP_SERVER`.
- The code waits up to `10` seconds for the first SNTP synchronization attempt.
- Transmission does not start until the SNTP callback has prepared both frame buffers.

### No WWVB output on the GPIO pin

- Verify `CONFIG_WWVB_OUTPUT_GPIO` matches your wiring.
- Verify `CONFIG_WWVB_CARRIER_FREQ_HZ` is set to `60000` if you want nominal WWVB carrier frequency.
- Remember the output duty is intentionally held low until SNTP synchronization completes.

### The clock does not lock to the local signal

- Confirm the target clock actually receives WWVB at `60 kHz`.
- Move the clock physically close to the ESP32 and coupling hardware.
- Use an oscilloscope or frequency counter to verify the carrier and the `200 ms`, `500 ms`, and `800 ms` pulse timings.
- Check that the minute boundary swap is happening cleanly by looking at the serial debug output and `HEALTH` logs.

### Serial monitor output is missing or unreadable

- Use the correct serial port.
- Use `115200` baud.
- Close any other program that may already be attached to the port.

## Signal Output Examples

![Scope image showing signal](images/ScopeOutput.png)

![Spectrum analyzer image showing a peak at 60 kHz](images/SAOutput.png)

## More Information

- [NIST WWVB station overview](https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb)
- [NIST WWVB time code format](https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb/wwvb-time-code-format)
- [NIST radio-controlled clocks and watches overview](https://www.nist.gov/pml/time-and-frequency-division/time-services/radio-controlled-clocks-and-watches)
- [Wikipedia: WWVB](https://en.wikipedia.org/wiki/WWVB)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
