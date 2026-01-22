# Unit Test Verification Guide

This guide helps verify that the unit tests build and run correctly on ESP32 hardware.

## Prerequisites

Before running tests, ensure:
- ESP-IDF v4.1.0 or higher is installed
- ESP-IDF environment is activated (`get_idf` or `. $IDF_PATH/export.sh`)
- ESP32 development board is connected
- Serial port permissions are configured (Linux: add user to `dialout` group)

## Quick Start

### Option 1: Using Helper Script (Easiest)

From the **root directory** of the project:

```bash
# Build, flash, and run all tests
./run_tests.sh

# Or specify a different serial port
./run_tests.sh all /dev/ttyUSB1
```

### Option 2: Manual Build

From the **root directory**:

```bash
# Navigate to test directory
cd test

# Build and run tests
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

**Important:** The command `idf.py test` does not exist. The test project is a standalone ESP-IDF project in the `test/` subdirectory.

**Expected Output:**
- Build should complete without errors
- You should see compilation of:
  - `main.c`
  - `test_wwvb_encoder.c`
  - `test_dst_calc.c`
  - `wwvb_encoder.c`
  - `dst_calc.c`

## Build Verification

### Using Helper Script

From the **root directory**:

```bash
./run_tests.sh build
```

**Expected Output:**
- Build should complete without errors
- You should see compilation of test files

### Using idf.py Directly

From the **test directory**:

```bash
cd test
idf.py build
```

**Expected Output:**
- Build should complete without errors
- You should see compilation of:
  - `main.c`
  - `test_wwvb_encoder.c`
  - `test_dst_calc.c`
  - `wwvb_encoder.c`
  - `dst_calc.c`

**Common Build Issues:**
- **"idf.py: command not found"** - ESP-IDF environment not activated
- **"IDF_PATH not set"** - Run `. $IDF_PATH/export.sh` or `get_idf`
- **CMake errors** - Ensure ESP-IDF version is 4.1.0 or higher
- **"ninja: error: unknown target 'test'"** - You're trying to run `idf.py test` which doesn't exist. Use `idf.py build` from the `test/` directory or use `./run_tests.sh` from the root directory.

## Running Tests on ESP32

### Using Helper Script

From the **root directory**:

```bash
# Build, flash, and monitor
./run_tests.sh

# Or specify port
./run_tests.sh all /dev/ttyUSB0
```

### Using idf.py Directly

From the **test directory**:

### Using idf.py Directly

From the **test directory**:

```bash
cd test
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your serial port (e.g., `COM3` on Windows, `/dev/cu.usbserial-*` on macOS).

**Expected Output:**
```
Starting WWVB Unit Tests
====================================
Unity test run 1 of 1

[test_wwvb_encoder.c]
test_BitsEncoder_zero:PASS
test_BitsEncoder_single_digit:PASS
test_BitsEncoder_two_digits:PASS
test_BitsEncoder_three_digits:PASS
test_EncodeYear_2024:PASS
test_EncodeYear_2000:PASS
test_EncodeDayOfYear_001:PASS
test_EncodeDayOfYear_365:PASS
test_EncodeHour_00:PASS
test_EncodeHour_13:PASS
test_EncodeMinute_00:PASS
test_EncodeMinute_42:PASS
test_SetMarkersAndIndicators:PASS
test_SetLeapYear_2024:PASS
test_SetLeapYear_2023:PASS
test_SetDST_false:PASS
test_SetDST_true:PASS

[test_dst_calc.c]
test_IsLeapYear_regular_leap_year:PASS
test_IsLeapYear_non_leap_year:PASS
test_IsLeapYear_century_non_leap:PASS
test_IsLeapYear_century_leap:PASS
test_CalculateDSTDays_2024:PASS
test_CalculateDSTDays_2023:PASS
test_CalculateDSTDays_2020:PASS
test_IsDaylightSavingTime_2024_before_DST:PASS
test_IsDaylightSavingTime_2024_during_DST:PASS
test_IsDaylightSavingTime_2024_after_DST:PASS
test_IsDaylightSavingTime_boundaries:PASS

-----------------------
28 Tests 0 Failures 0 Ignored 
OK
====================================
WWVB Unit Tests Complete
```

### Step 4: Verify Results

**Success Criteria:**
- All 28 tests should PASS
- 0 Failures
- 0 Ignored
- Final status: **OK**

**If Tests Fail:**
1. Note which specific test(s) failed
2. Check the assertion message for details
3. Verify the expected vs. actual values
4. Review the corresponding source function implementation

## Test Details

### WWVB Encoder Tests (17 tests)

| Test Name | Description | Validates |
|-----------|-------------|-----------|
| `test_BitsEncoder_zero` | Zero value encoding | BCD: 0 → 0x0000 |
| `test_BitsEncoder_single_digit` | Single digit encoding | BCD: 5 → 0x0005, 9 → 0x0009 |
| `test_BitsEncoder_two_digits` | Two digit encoding | BCD: 24 → 0x0024, 59 → 0x0059 |
| `test_BitsEncoder_three_digits` | Three digit encoding | BCD: 365 → 0x0365 |
| `test_EncodeYear_2024` | Year 2024 encoding | WWVB positions 45-48, 50-53 |
| `test_EncodeYear_2000` | Year 2000 encoding | Boundary case: Y2K |
| `test_EncodeDayOfYear_001` | Day 1 encoding | First day of year |
| `test_EncodeDayOfYear_365` | Day 365 encoding | Last day of non-leap year |
| `test_EncodeHour_00` | Hour 00 encoding | Midnight |
| `test_EncodeHour_13` | Hour 13 encoding | 1 PM in 24-hour format |
| `test_EncodeMinute_00` | Minute 00 encoding | Start of hour |
| `test_EncodeMinute_42` | Minute 42 encoding | Mid-hour value |
| `test_SetMarkersAndIndicators` | Marker positioning | Positions 0,9,19,29,39,49,59 + zeros |
| `test_SetLeapYear_2024` | 2024 leap year | Bit 55 = 1 |
| `test_SetLeapYear_2023` | 2023 non-leap year | Bit 55 = 0 |
| `test_SetDST_false` | Standard time | Bits 57,58 = 0,0 |
| `test_SetDST_true` | DST active | Bits 57,58 = 1,1 |

### DST Calculation Tests (11 tests)

| Test Name | Description | Validates |
|-----------|-------------|-----------|
| `test_IsLeapYear_regular_leap_year` | Regular leap years | 2024, 2020, 2016 |
| `test_IsLeapYear_non_leap_year` | Non-leap years | 2023, 2022, 2021 |
| `test_IsLeapYear_century_non_leap` | Century non-leap | 1900, 2100 |
| `test_IsLeapYear_century_leap` | Century leap | 2000, 2400 |
| `test_CalculateDSTDays_2024` | 2024 DST dates | Mar 10 (day 70), Nov 3 (day 308) |
| `test_CalculateDSTDays_2023` | 2023 DST dates | Mar 12 (day 71), Nov 5 (day 309) |
| `test_CalculateDSTDays_2020` | 2020 DST dates | Mar 8 (day 68), Nov 1 (day 306) |
| `test_IsDaylightSavingTime_2024_before_DST` | Before DST period | Feb, early March |
| `test_IsDaylightSavingTime_2024_during_DST` | During DST period | March 10 - Nov 2 |
| `test_IsDaylightSavingTime_2024_after_DST` | After DST period | Nov 3 onwards |
| `test_IsDaylightSavingTime_boundaries` | Edge cases | Jan 1, Dec 31 |

## Troubleshooting

### Build Fails

**Symptom:** Compilation errors
**Solution:**
1. Check ESP-IDF version: `idf.py --version`
2. Ensure IDF_PATH is set: `echo $IDF_PATH`
3. Clean build: `idf.py fullclean` then `idf.py build`

### Flash Fails

**Symptom:** Cannot connect to ESP32 or "port is busy"

**Solution:**
1. **If port is busy/locked:**
   - Close VSCode Serial Monitor or any other serial terminal
   - Kill monitor processes: `pkill -f "idf.py monitor"`
   - Check what's using the port: `lsof /dev/ttyUSB0` (Linux/macOS)
   - Build without flashing first: `./run_tests.sh build`
   - Then flash separately after closing monitors: `./run_tests.sh flash /dev/ttyUSB0`

2. **If port doesn't exist:**
   - Check USB connection
   - Verify serial port exists: `ls /dev/tty* | grep -E 'USB|ACM'` (Linux/macOS) or Device Manager (Windows)
   - Try different port: `./run_tests.sh all /dev/ttyACM0`
   - Check permissions: On Linux, user should be in `dialout` group
   - Try different USB cable or USB port

3. **General troubleshooting:**
   - Ensure ESP32 is powered
   - Try pressing RESET button on ESP32
   - Check USB drivers are installed for your board

### Tests Run But Fail

**Symptom:** Some tests show "FAIL"
**Solution:**
1. Read the failure message carefully
2. Note the expected vs. actual values
3. Check if source code was modified
4. Verify WWVB protocol specifications

### Serial Monitor Issues

**Symptom:** No output or garbled text
**Solution:**
1. Check baud rate: should be 115200
2. Press RESET button on ESP32
3. Use `Ctrl+]` to exit monitor, then restart

## Continuous Integration

For automated testing:
1. Add GitHub Actions workflow to build tests
2. Use ESP-IDF Docker container
3. Run tests in QEMU (for host-side testing)
4. Set up ESP32 CI runner for hardware tests

Example GitHub Actions snippet:
```yaml
- name: Build Unit Tests
  run: |
    cd test
    idf.py build
```

## Next Steps

After verifying tests pass:
1. Run tests after any code changes to encoder or DST modules
2. Add new tests when adding new features
3. Update test cases if WWVB protocol changes
4. Consider adding integration tests for hardware components

## Support

If you encounter issues:
1. Check `test/README.md` for detailed testing documentation
2. Review ESP-IDF Unity framework documentation
3. Consult ESP-IDF build system guide
4. Check GitHub issues for similar problems
