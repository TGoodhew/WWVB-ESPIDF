# WWVB Unit Tests

This directory contains unit tests for the WWVB-ESPIDF project using the ESP-IDF Unity test framework.

## Test Coverage

### WWVB Encoder Tests (`test_wwvb_encoder.c`)
Tests for the WWVB signal encoding functions:
- `BitsEncoder()` - Binary-Coded Decimal (BCD) conversion
- `EncodeYear()` - Year encoding into WWVB signal format
- `EncodeDayOfYear()` - Julian day encoding
- `EncodeHour()` - Hour encoding (24-hour format)
- `EncodeMinute()` - Minute encoding
- `SetMarkersAndIndicators()` - Position markers and reserved bits
- `SetLeapYear()` - Leap year indicator
- `SetDST()` - Daylight Saving Time indicators

### DST Calculation Tests (`test_dst_calc.c`)
Tests for US Daylight Saving Time calculation functions:
- `IsLeapYear()` - Leap year detection (Gregorian calendar rules)
- `CalculateDSTDays()` - Calculate DST start/end days for a given year
- `IsDaylightSavingTime()` - Check if a specific day is during DST

## Building and Running Tests

### Prerequisites
- ESP-IDF v4.1.0 or higher installed and configured
- ESP32 development board (or QEMU for host-side testing)

### Build Tests
```bash
cd test
idf.py build
```

### Flash and Run Tests on ESP32
```bash
cd test
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your serial port.

### Expected Output
When tests run successfully, you should see output like:
```
Starting WWVB Unit Tests
====================================
Unity test run 1 of 1
test_BitsEncoder_zero...OK
test_BitsEncoder_single_digit...OK
test_BitsEncoder_two_digits...OK
...
-----------------------
28 Tests 0 Failures 0 Ignored 
OK
====================================
WWVB Unit Tests Complete
```

## Test Structure

```
test/
├── CMakeLists.txt          # Test project configuration
├── main/
│   ├── CMakeLists.txt      # Main component configuration
│   ├── main.c              # Test runner entry point
│   ├── test_wwvb_encoder.c # WWVB encoder tests
│   └── test_dst_calc.c     # DST calculation tests
└── README.md               # This file
```

## Adding New Tests

To add new tests:

1. Create a new test file in `test/main/` (e.g., `test_new_module.c`)
2. Implement test functions following the Unity framework conventions
3. Create a `run_new_module_tests()` function that calls `RUN_TEST()` for each test
4. Add the extern declaration and function call in `main.c`
5. Add the source file to `SRCS` in `test/main/CMakeLists.txt`

Example test function:
```c
static void test_my_function(void)
{
    TEST_ASSERT_EQUAL_INT(expected, my_function(input));
}
```

## Test Guidelines

- Use descriptive test function names (e.g., `test_function_name_scenario`)
- Test edge cases (boundary values, null pointers, etc.)
- Keep tests focused on a single behavior
- Use appropriate Unity assertions (see Unity documentation)
- Include comments explaining complex test scenarios

## Unity Assertions

Common Unity assertions used in these tests:
- `TEST_ASSERT_EQUAL_UINT8(expected, actual)` - Compare 8-bit unsigned integers
- `TEST_ASSERT_EQUAL_UINT16(expected, actual)` - Compare 16-bit unsigned integers
- `TEST_ASSERT_EQUAL_INT(expected, actual)` - Compare signed integers
- `TEST_ASSERT_TRUE(condition)` - Assert condition is true
- `TEST_ASSERT_FALSE(condition)` - Assert condition is false

For more Unity assertions, see: https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityAssertionsReference.md

## Troubleshooting

### Build Errors
- Ensure ESP-IDF environment is properly set up (`get_idf` or `. $IDF_PATH/export.sh`)
- Check that IDF_PATH environment variable is set
- Verify ESP-IDF version is 4.1.0 or higher

### Test Failures
- Check test assertions to understand what is failing
- Verify expected values match the WWVB protocol specification
- Review source code changes that may have affected test behavior

### Serial Monitor Issues
- Ensure correct serial port is specified
- Check baud rate (default: 115200)
- Verify USB drivers are installed for your ESP32 board
