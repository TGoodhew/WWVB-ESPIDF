# GitHub Copilot Instructions for WWVB-ESPIDF

## Project Overview

This is a WWVB (60 kHz time signal) emulator running on an Adafruit Huzzah32 Featherboard (ESP32). The device:
- Gets current time via NTP
- Generates an emulated WWVB signal on a GPIO pin
- Uses WiFi provisioning via BLE for network configuration
- Targets atomic clocks that need WWVB signal synchronization

## Framework and Build System

- **Framework**: ESP-IDF (Espressif IoT Development Framework) v4.1.0 or higher
- **Build System**: CMake-based ESP-IDF build system
- **Target Hardware**: ESP32 (specifically Adafruit Huzzah32 Featherboard)
- **Language**: C

## Code Organization

- All main code is in `main/main.c` as a single-file implementation for portability
- Component dependencies are managed via `main/idf_component.yml`
- CMake configuration in root `CMakeLists.txt` and `main/CMakeLists.txt`

## Coding Conventions

### Style Guidelines

1. **Naming Conventions**:
   - Functions: PascalCase (e.g., `SetupWiFi()`, `SetupSNTP()`, `TimerSecond_ISR()`)
   - Variables: camelCase (e.g., `isProvisioned`, `ntpServer`, `ledc_channel`)
   - Constants: camelCase for simple constants, UPPER_CASE for defines
   - ISR functions: Suffix with `_ISR` (e.g., `TimerSecond_ISR()`)

2. **Formatting**:
   - Use 4 spaces for indentation (not tabs)
   - Opening braces on the same line for control structures
   - Space after control structure keywords (`if (`, `while (`, etc.)

3. **ESP-IDF Patterns**:
   - Always use `ESP_ERROR_CHECK()` for error handling of ESP-IDF functions
   - Use `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()` for logging with appropriate tags
   - Follow ESP-IDF component initialization patterns
   - Use FreeRTOS types and functions (e.g., `vTaskDelay()`, `portTICK_PERIOD_MS`)

4. **Comments**:
   - Use C-style `/* */` for multi-line comments
   - Use `//` for single-line comments
   - Include function prototypes at the top of the file
   - Document complex algorithms and hardware-specific configurations

### Hardware-Specific Considerations

1. **GPIO Configuration**:
   - GPIO 13 is used for signal output
   - Always reset pins before configuring: `gpio_reset_pin(GPIO_NUM_XX)`
   - Set direction explicitly: `gpio_set_direction()`

2. **60 kHz Signal Generation**:
   - Uses ESP32 LEDC (LED Control) peripheral for PWM generation
   - Target frequency: 60,000 Hz (60 kHz)
   - Signal modulation follows WWVB protocol (0.2s, 0.5s, 0.8s pulse widths)

3. **Timing Requirements**:
   - Uses ESP32 high-resolution timers (`esp_timer`)
   - Critical timing for WWVB signal generation
   - One-second synchronization for time slot updates

## Required Components

Always include these ESP-IDF components when needed:
- `esp_netif` - Network interface
- `esp_timer` - High-resolution timers
- `esp_driver_ledc` - LED Control PWM
- `esp_driver_gpio` - GPIO control
- `nvs_flash` - Non-volatile storage
- `esp_wifi` - WiFi functionality
- `lwip` - Lightweight IP stack
- `wifi_provisioning` - BLE provisioning

## Build and Development

### Building the Project

```bash
idf.py build
```

### Flashing to Device

```bash
idf.py -p <PORT> flash
```

### Monitoring Output

```bash
idf.py -p <PORT> monitor
```

### Configuration

```bash
idf.py menuconfig
```

## Testing and Debugging

1. **Debug Output**: 
   - The `WWVBDEBUG` macro is defined for debug builds
   - Use ESP logging functions with appropriate log levels

2. **Serial Output**: 
   - `setvbuf(stdout, NULL, _IONBF, 0)` is used to disable buffering for immediate console output

3. **WiFi Provisioning**:
   - BLE provisioning with security level 1
   - Default PoP (Proof of Possession): "abcd1234"
   - Service name generated from device MAC address

## WWVB Protocol Implementation

When working with WWVB signal encoding:
- 60-second frame structure (array of 60 bits)
- Bit encoding: 0 = 0.2s low, 1 = 0.5s low, marker = 0.8s low
- Markers at positions 0, 9, 19, 29, 39, 49, 59
- Time encoding: BCD format for year, day, hour, minute
- Include DUT1, leap year, leap second, and DST indicators

## Best Practices

1. **Memory Management**:
   - Be mindful of stack sizes in FreeRTOS tasks
   - Use static allocation where possible for ISR-related data
   - Avoid dynamic allocation in ISRs

2. **WiFi and Network**:
   - Handle WiFi disconnections and reconnections gracefully
   - Implement retry logic for NTP synchronization
   - Use provisioning manager for first-time setup

3. **Time Synchronization**:
   - Ensure SNTP is initialized after WiFi connection
   - Use SNTP callbacks for time sync notifications
   - Convert to UTC for WWVB encoding

4. **Error Handling**:
   - Never ignore ESP-IDF function return values
   - Use `ESP_ERROR_CHECK()` for critical operations
   - Log errors with context using `ESP_LOGE()`

## File Management

- Build artifacts go in `build/` directory (gitignored)
- Configuration in `sdkconfig` (gitignored)
- Keep the single-file design for easy portability
- Images and documentation in separate directories

## Additional Notes

- This is an embedded system with real-time constraints
- Signal accuracy is critical for proper atomic clock synchronization
- The code targets a specific hardware platform (ESP32)
- WiFi provisioning allows setup without hardcoded credentials
