This is an emualtor for the WWVB signal running on an Adafruit Huzzah32 Featherboard.

The goal here is to create a small device that will get the current time via NTP and then use a GPIO pin to generate an emulated WWVB signal.

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

## Signal Output

The signal currently looks like this:
![Scope image showing signal](https://github.com/tgoodhew/WWVB-ESPIDF/blob/main/images/ScopeOutput.png?raw=true)

And is showing up as a nice spike on my spectrum analyzer (direct connection via a 20dB attenuator) - The noise is 70dB down from the peak but I don't have antennas yet so I can yet test OTA values.
![Spectrum Analyzer image showing a peak at 60KHz](https://github.com/tgoodhew/WWVB-ESPIDF/blob/main/images/SAOutput.png?raw=true)
