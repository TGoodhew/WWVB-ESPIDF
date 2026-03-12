This is an emulator for the WWVB signal running on an Adafruit Huzzah32 Featherboard (ESP32).

The goal is to create a small device that gets UTC time via SNTP/NTP and then uses a GPIO pin to generate an emulated WWVB signal.

## Requirements

- ESP-IDF v5.5.3
- ESP32 target (this project is currently configured for classic ESP32)
- Python environment installed by ESP-IDF tools

## Build and Flash

### Windows (PowerShell)

```powershell
& "C:\esp\v5.5.3\esp-idf\export.ps1"
idf.py build
idf.py flash monitor
```

### macOS/Linux

```bash
source $HOME/esp/v5.5.3/esp-idf/export.sh
idf.py build
idf.py flash monitor
```

## Factory Reset (Clear Provisioning)

To erase WiFi provisioning credentials and all NVS data:

```powershell
idf.py erase_flash
```

## WiFi Provisioning

- Provisioning transport: BLE
- Security: `WIFI_PROV_SECURITY_1`
- Proof-of-Possession is configured via `CONFIG_WWVB_WIFI_POP` (default `abcd1234`)

You can change the PoP with:

```powershell
idf.py menuconfig
```

Then navigate to WWVB Emulator Configuration.

## Signal Validation

The signal currently looks like this:
![Scope image showing signal](https://github.com/tgoodhew/WWVB-ESPIDF/blob/main/images/ScopeOutput.png?raw=true)

It also appears as a strong 60 kHz spike on a spectrum analyzer (direct connection via a 20 dB attenuator):
![Spectrum Analyzer image showing a peak at 60KHz](https://github.com/tgoodhew/WWVB-ESPIDF/blob/main/images/SAOutput.png?raw=true)
