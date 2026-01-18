# WWVB Emulator for ESP32

This is an emulator for the WWVB time signal running on an Adafruit Huzzah32 Featherboard (ESP32).

## Overview

The goal is to create a small device that gets the current time via NTP and generates an emulated WWVB signal on a GPIO pin. This allows atomic clocks to synchronize even when the real WWVB signal from Fort Collins, Colorado is weak or unavailable.

**WWVB** is a time signal radio station near Fort Collins, Colorado, operated by NIST (National Institute of Standards and Technology). It broadcasts on 60 kHz and is used by atomic clocks throughout North America for time synchronization.

## Features

- **NTP Time Synchronization**: Automatically fetches accurate time from NTP servers
- **60 kHz WWVB Signal Generation**: Produces amplitude-modulated signal compatible with atomic clocks
- **WiFi Provisioning via BLE**: Easy wireless setup without hardcoded credentials
- **Configurable Parameters**: GPIO pins, NTP server, and retry settings via menuconfig
- **Double-Buffered Signal Generation**: Ensures glitch-free signal output
- **US Daylight Saving Time Support**: Automatically handles DST transitions

## Configuration

The following parameters can be configured via `idf.py menuconfig` under "WWVB Configuration":

- **WWVB Output GPIO Pin**: GPIO pin for 60kHz signal output (default: 26, A0 on Huzzah32)
- **Debug LED GPIO Pin**: GPIO pin for debug LED (default: 13)
- **NTP Server**: NTP server hostname or IP address (default: "pool.ntp.org")
- **WiFi Max Retry**: Maximum WiFi connection retry attempts (default: 10)

To configure these parameters:
```bash
idf.py menuconfig
```
Navigate to "WWVB Configuration" and adjust the settings as needed.

## Hardware Requirements

- **ESP32 Development Board**: Adafruit Huzzah32 Featherboard (or compatible ESP32 board)
- **Antenna** (optional): For over-the-air transmission to atomic clocks
- **Development Environment**: ESP-IDF v4.1.0 or higher

## Project Architecture

The codebase is organized into modular components:

```
main/
├── main.c              - Main application logic, ISR coordination, double-buffering
├── wwvb_encoder.c/h    - WWVB signal encoding (BCD, time data)
├── signal_output.c/h   - 60 kHz PWM generation, timer management
├── wifi_manager.c/h    - WiFi provisioning and connection management
├── time_sync.c/h       - SNTP time synchronization
├── dst_calc.c/h        - US Daylight Saving Time calculations
└── wwvb_config.h       - Shared configuration and debug macros
```

### Module Responsibilities

- **main.c**: Application entry point, coordinates all modules, manages double-buffered WWVB signal arrays, and handles the per-second ISR that drives signal transmission
- **wwvb_encoder**: Converts time data (year, day, hour, minute) into WWVB frame format using BCD encoding
- **signal_output**: Generates the 60 kHz carrier using ESP32 LEDC PWM and manages signal modulation timers
- **wifi_manager**: Handles WiFi provisioning via BLE with cryptographically secure PoP generation
- **time_sync**: Manages SNTP synchronization with configurable NTP server and retry logic
- **dst_calc**: Calculates US DST boundaries (2nd Sunday in March to 1st Sunday in November)

## WWVB Signal Format

### Signal Characteristics

- **Carrier Frequency**: 60,000 Hz (60 kHz)
- **Modulation**: Amplitude modulation (AM)
- **Frame Duration**: 60 seconds (one frame per minute)
- **Power Levels**: Full power and reduced power (simulated by PWM duty cycle)

### Signal Timing - Pulse Width Encoding

WWVB encodes data using **pulse width modulation** where the duration of reduced power determines the bit value:

```
Bit Value '0':     |████|____| 0.2s low, 0.8s high (200ms reduced power)
Bit Value '1':     |█████|___| 0.5s low, 0.5s high (500ms reduced power)
Position Marker:   |███████|_| 0.8s low, 0.2s high (800ms reduced power)

█ = Full carrier power (50% PWM duty cycle at 60 kHz)
_ = Reduced carrier power (0% PWM duty cycle)
```

**Why these specific timings?**
- **200ms (0.2s)**: Represents binary '0', short enough to be distinct from '1'
- **500ms (0.5s)**: Represents binary '1', exactly halfway through the second
- **800ms (0.8s)**: Position markers, longest duration for easy frame synchronization

Each second of the 60-second frame transmits one bit. The atomic clock receiver:
1. Measures the duration of reduced power each second
2. Decodes the bit value (0, 1, or marker)
3. After receiving all 60 bits, decodes the time information

### WWVB Frame Structure (60 bits, 0-59)

The WWVB signal encodes time information in a 60-second frame using Binary-Coded Decimal (BCD):

```
Position  Type        Data                Weight    Description
--------  ----------  ------------------  --------  ------------------------------------
0         Marker      Frame Reference     -         Start of minute marker
1-8       Data        Minutes             40-1      Current minute (00-59) in BCD
9         Marker      Position Reference  -         Every 10 seconds
10-11     Reserved    Always 0            -         Reserved bits
12-18     Data        Hours               20-1      Current hour (00-23) in BCD
19        Marker      Position Reference  -         Every 10 seconds
20-21     Reserved    Always 0            -         Reserved bits
22-33     Data        Day of Year         200-1     Julian day (001-366) in BCD
34-35     Reserved    Always 0            -         Reserved bits
36-43     Data        DUT1 (obsolete)     -         Set to 0 (deprecated)
44        Reserved    Always 0            -         Reserved bit
45-53     Data        Year                80-1      2-digit year (00-99) in BCD
54        Reserved    Always 0            -         Reserved bit
55        Data        Leap Year           -         1 if leap year, 0 otherwise
56        Data        Leap Second         -         1 if leap second at end of month
57-58     Data        DST Status          -         Both bits: 11=DST, 00=Standard
59        Marker      End of Frame        -         End of minute marker

Marker positions: 0, 9, 19, 29, 39, 49, 59 (every 10 seconds, plus start/end)
```

**BCD (Binary-Coded Decimal) Encoding:**

BCD represents each decimal digit as a 4-bit binary number. For example:
- Decimal 24 = 0010 0100 in BCD (2 in upper nibble, 4 in lower nibble)
- Year 2024 → 24 → bits represent: 0010 0100
- Hour 13 → bits represent: 0001 0011

The BitsEncoder function converts decimal values to BCD by:
1. Extracting hundreds digit: `n / 100`
2. Extracting tens digit: `(n / 10) % 10`
3. Extracting ones digit: `n % 10`
4. Packing into a 16-bit value with each digit in a 4-bit nibble

### Example: Encoding Minute = 42

```
Decimal: 42
BCD breakdown:
  - Tens digit: 4 → 0100 binary
  - Ones digit: 2 → 0010 binary
  - Combined: 0100 0010 (0x42 in BCD)

WWVB Frame Positions (LSB to MSB):
  Position 1: bit 0 of ones (2) → 0
  Position 2: bit 1 of ones     → 1
  Position 3: bit 2 of ones     → 0
  Position 4: bit 3 of ones     → 0
  Position 5: bit 0 of tens (4) → 0
  Position 6: bit 1 of tens     → 0
  Position 7: bit 2 of tens     → 1
  Position 8: bit 3 of tens     → 0
```

## System Architecture

### Double-Buffer State Machine

The emulator uses **double-buffering with pointer swapping** to ensure glitch-free signal transmission:

```
┌─────────────┐           ┌─────────────┐
│   Buffer 0  │◄──Active──│     ISR     │ Reads and transmits
│  (60 bytes) │           │   (1 Hz)    │ one bit per second
└─────────────┘           └─────────────┘
       ▲                         │
       │                         │ At slot 0, swap if pending
       │ Pointer swap            ▼
       │ (atomic operation)  swap_pending=true
       │                         │
┌─────────────┐           ┌─────────────┐
│   Buffer 1  │◄─Staging──│  Main Task  │ Encodes next minute
│  (60 bytes) │           │             │ when signaled by ISR
└─────────────┘           └─────────────┘
```

**Why Double-Buffering?**
- **ISR reads from active buffer**: Never interrupted, ensures consistent frame
- **Main task writes to staging buffer**: Can take time without affecting transmission
- **Pointer swap at minute boundary**: Atomic operation (<1µs), no data copying needed
- **Prevents glitches**: No partially-updated frames are ever transmitted

### State Flow

```
1. Startup → Initialize buffers, set active/staging pointers
2. NVS Init → Prepare non-volatile storage for WiFi credentials
3. WiFi Setup → BLE provisioning or connect to saved network
4. SNTP Sync → Fetch time from NTP server, start 1-second timer
5. Main Loop:
   ┌─────────────────────────────────────────┐
   │ Main Task:                              │
   │ - Wait for update_wwvb_array flag       │
   │ - Encode current time to staging buffer │
   │ - Set swap_pending flag                 │
   └─────────────────────────────────────────┘
           ▲
           │ Signal flag
           │
   ┌─────────────────────────────────────────┐
   │ ISR (1 Hz):                             │
   │ - Advance slot (0-59)                   │
   │ - If slot==0 && swap_pending:           │
   │     Swap active↔staging pointers        │
   │ - Read active[slot]                     │
   │ - Start appropriate timer (0.2s/0.5s/0.8s) │
   │ - At slot==30 or 60: Set update flag   │
   └─────────────────────────────────────────┘
           │
           ▼
   ┌─────────────────────────────────────────┐
   │ Bit/Marker Timer ISR:                   │
   │ - Restore carrier to full power (50%)   │
   └─────────────────────────────────────────┘
```

### Timer Coordination

The system uses multiple coordinated ESP32 hardware timers:

1. **Second Timer (1 Hz)**: Main ISR that advances through the 60-second frame
   - Triggers every 1,000,000 microseconds
   - Reads current bit from active buffer
   - Starts appropriate bit timer based on value (0, 1, or marker)

2. **Bit Timers (One-shot)**:
   - **Bit0 Timer**: 200,000µs (0.2s) - After bit '0' reduced power period
   - **Bit1 Timer**: 500,000µs (0.5s) - After bit '1' reduced power period  
   - **Marker Timer**: 800,000µs (0.8s) - After marker reduced power period
   - All restore carrier to full power when they expire

**Timing Diagram for One Second:**
```
Second Timer ISR fires at t=0
    ↓
    Reduce carrier power (duty cycle → 0%)
    Start Bit Timer (200ms, 500ms, or 800ms)
    ↓
    ... carrier at reduced power ...
    ↓
    Bit Timer ISR fires
    Restore carrier power (duty cycle → 50%)
    ↓
    ... carrier at full power until next second ...
    ↓
Next Second Timer ISR fires at t=1000ms
```

### WiFi Provisioning Flow

```
┌─────────────────┐
│   Device Boot   │
└────────┬────────┘
         │
         ▼
   ┌─────────────┐
   │ Check NVS   │◄──── Non-Volatile Storage
   │ Provisioned?│
   └──────┬──────┘
          │
    ┌─────┴─────┐
    │           │
   YES         NO
    │           │
    │           ▼
    │    ┌──────────────────┐
    │    │ Generate Service │ Based on MAC address
    │    │ Name: PROV_XXXXXX│ (last 3 bytes in hex)
    │    └────────┬─────────┘
    │             │
    │             ▼
    │    ┌──────────────────┐
    │    │  Generate PoP    │ SHA-256 hash of MAC
    │    │ (12 hex chars)   │ First 6 bytes of hash
    │    └────────┬─────────┘
    │             │
    │             ▼
    │    ┌──────────────────┐
    │    │  Start BLE Adv   │ User sees PROV_XXXXXX
    │    │ Wait for Mobile  │ in ESP BLE Prov app
    │    └────────┬─────────┘
    │             │
    │             ▼
    │    ┌──────────────────┐
    │    │  User Enters:    │
    │    │  - PoP (shown in │ Logged to console
    │    │    serial output)│ for user to enter
    │    │  - WiFi SSID     │
    │    │  - WiFi Password │
    │    └────────┬─────────┘
    │             │
    │             ▼
    │    ┌──────────────────┐
    │    │ Save to NVS      │
    │    │ Deinit BLE       │
    │    └────────┬─────────┘
    │             │
    └─────────────┘
                  │
                  ▼
         ┌────────────────┐
         │  Start WiFi    │
         │  Connect to AP │
         └───────┬────────┘
                 │
                 ▼
         ┌────────────────┐
         │   Got IP via   │
         │     DHCP       │
         └───────┬────────┘
                 │
                 ▼
         ┌────────────────┐
         │  Ready for     │
         │  SNTP Sync     │
         └────────────────┘
```

**Security Note**: The PoP (Proof of Possession) is generated using SHA-256 hash of the MAC address. This prevents attackers from deriving the PoP by observing the advertised service name or scanning for MAC addresses. The PoP must be obtained from the device's serial console output.

## Configuration

The following parameters can be configured via `idf.py menuconfig` under "WWVB Configuration":

- **WWVB Output GPIO Pin**: GPIO pin for 60kHz signal output (default: 26, A0 on Huzzah32)
- **Debug LED GPIO Pin**: GPIO pin for debug LED (default: 13)
- **NTP Server**: NTP server hostname or IP address (default: "pool.ntp.org")
- **WiFi Max Retry**: Maximum WiFi connection retry attempts (default: 10)

To configure these parameters:
```bash
idf.py menuconfig
```
Navigate to "WWVB Configuration" and adjust the settings as needed.

## Building and Flashing

### Prerequisites

- ESP-IDF v4.1.0 or higher installed and configured
- USB cable to connect ESP32 board to computer
- Serial port access (driver dependent on your OS)

### Build

```bash
cd /path/to/WWVB-ESPIDF
idf.py build
```

### Flash to Device

```bash
idf.py -p /dev/ttyUSB0 flash
```
Replace `/dev/ttyUSB0` with your serial port (e.g., `COM3` on Windows, `/dev/cu.usbserial-*` on macOS).

### Monitor Serial Output

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Press `Ctrl+]` to exit the monitor.

### Combined Flash and Monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Usage

1. **First Boot**: Device will start BLE provisioning
   - Service name appears as `PROV_XXXXXX` (where XXXXXX is from MAC address)
   - Check serial console for the PoP (Proof of Possession) - a 12-character hex code
   - Use ESP BLE Provisioning app (Android/iOS) to provision WiFi credentials
   
2. **WiFi Connection**: Device connects to configured WiFi network
   
3. **Time Sync**: Device fetches time from NTP server
   
4. **Signal Generation**: 60 kHz WWVB signal starts on configured GPIO pin
   
5. **Atomic Clock Sync**: Place your atomic clock near the ESP32 antenna
   - Most clocks sync automatically when they detect signal loss
   - Some clocks have a manual sync button

## Signal Output

The signal currently looks like this:
![Scope image showing signal](images/ScopeOutput.png)

And is showing up as a nice spike on my spectrum analyzer (direct connection via a 20dB attenuator) - The noise is 70dB down from the peak but I don't have antennas yet so I can yet test OTA values.
![Spectrum Analyzer image showing a peak at 60KHz](images/SAOutput.png)

## Troubleshooting

### Time Not Synchronizing
- Check WiFi connection: `idf.py monitor` should show "got ip"
- Verify NTP server is reachable
- Check firewall/network allows NTP (UDP port 123)

### Atomic Clock Not Syncing
- Ensure GPIO 26 (A0) is connected to antenna or clock input
- Move clock closer to ESP32
- Try manual sync on atomic clock
- Check signal on oscilloscope to verify output

### BLE Provisioning Not Working
- Device may already be provisioned - erase flash: `idf.py erase-flash`
- Check PoP is entered correctly (case-sensitive hex)
- Ensure phone Bluetooth is enabled

## Technical References

- [WWVB Time Code Format (NIST)](https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb/wwvb-time-code-format)
- [WWVB Wikipedia Article](https://en.wikipedia.org/wiki/WWVB)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [ESP32 LEDC (PWM) Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html)

## License

This project is provided as-is for educational and personal use.
