/*
 * WWVB Signal Encoding Module
 * 
 * Unit tests: See test/main/test_wwvb_encoder.c
 */

#include "wwvb_encoder.h"
#include <time.h>
#include <esp_log.h>

// Digit extraction constants
#define DIGIT_DIVISOR_100 100
#define DIGIT_DIVISOR_10 10
#define NIBBLE_MASK 0xF

// WWVB bit position constants for year encoding
#define WWVB_YEAR_BIT_45 45
#define WWVB_YEAR_BIT_46 46
#define WWVB_YEAR_BIT_47 47
#define WWVB_YEAR_BIT_48 48
#define WWVB_YEAR_BIT_50 50
#define WWVB_YEAR_BIT_51 51
#define WWVB_YEAR_BIT_52 52
#define WWVB_YEAR_BIT_53 53

// WWVB bit position constants for day of year encoding
#define WWVB_DAY_BIT_22 22
#define WWVB_DAY_BIT_23 23
#define WWVB_DAY_BIT_25 25
#define WWVB_DAY_BIT_26 26
#define WWVB_DAY_BIT_27 27
#define WWVB_DAY_BIT_28 28
#define WWVB_DAY_BIT_30 30
#define WWVB_DAY_BIT_31 31
#define WWVB_DAY_BIT_32 32
#define WWVB_DAY_BIT_33 33

// WWVB bit position constants for hour encoding
#define WWVB_HOUR_BIT_12 12
#define WWVB_HOUR_BIT_13 13
#define WWVB_HOUR_BIT_15 15
#define WWVB_HOUR_BIT_16 16
#define WWVB_HOUR_BIT_17 17
#define WWVB_HOUR_BIT_18 18

// WWVB bit position constants for minute encoding
#define WWVB_MINUTE_BIT_1 1
#define WWVB_MINUTE_BIT_2 2
#define WWVB_MINUTE_BIT_3 3
#define WWVB_MINUTE_BIT_5 5
#define WWVB_MINUTE_BIT_6 6
#define WWVB_MINUTE_BIT_7 7
#define WWVB_MINUTE_BIT_8 8

// WWVB position marker bit positions
#define WWVB_MARKER_BIT_0 0
#define WWVB_MARKER_BIT_9 9
#define WWVB_MARKER_BIT_19 19
#define WWVB_MARKER_BIT_29 29
#define WWVB_MARKER_BIT_39 39
#define WWVB_MARKER_BIT_49 49
#define WWVB_MARKER_BIT_59 59

// WWVB always-zero bit positions
#define WWVB_ZERO_BIT_4 4
#define WWVB_ZERO_BIT_10 10
#define WWVB_ZERO_BIT_11 11
#define WWVB_ZERO_BIT_14 14
#define WWVB_ZERO_BIT_20 20
#define WWVB_ZERO_BIT_21 21
#define WWVB_ZERO_BIT_24 24
#define WWVB_ZERO_BIT_34 34
#define WWVB_ZERO_BIT_35 35
#define WWVB_ZERO_BIT_44 44
#define WWVB_ZERO_BIT_54 54

// WWVB DUT1 bit positions (obsolete)
#define WWVB_DUT1_BIT_36 36
#define WWVB_DUT1_BIT_37 37
#define WWVB_DUT1_BIT_38 38
#define WWVB_DUT1_BIT_40 40
#define WWVB_DUT1_BIT_41 41
#define WWVB_DUT1_BIT_42 42
#define WWVB_DUT1_BIT_43 43

// WWVB indicator bit positions
#define WWVB_LEAP_YEAR_BIT 55
#define WWVB_LEAP_SECOND_BIT 56
#define WWVB_DST_BIT_57 57
#define WWVB_DST_BIT_58 58

// Time constants for leap year calculation
#define LEAP_YEAR_MARCH_MONTH 2  // March in struct tm (0-based: January=0, February=1, March=2)
#define LEAP_YEAR_TEST_DAY 0     // Zero day of March rolls back to last day of February
#define LEAP_YEAR_FEB_29 29      // Day value for February 29

/**
 * @brief Helper function to organize decimal digits into nibbles for bit extraction
 * 
 * Converts a decimal value (0-999) into a format where each decimal digit
 * occupies a 4-bit nibble. This organization simplifies extracting individual bits
 * for WWVB's weighted binary encoding.
 * 
 * Algorithm:
 * 1. Extract hundreds digit: n / 100
 * 2. Extract tens digit: (n / 10) % 10
 * 3. Extract ones digit: n % 10
 * 4. Pack into 16-bit result: [hundreds nibble][tens nibble][ones nibble]
 * 
 * Example: n=142
 *   - Hundreds: 1 → 0x0001
 *   - Tens:     4 → 0x0004
 *   - Ones:     2 → 0x0002
 *   - Result: 0x0142 (bit pattern: 0000 0001 0100 0010)
 * 
 * Example: n=59 (minute or second value)
 *   - Hundreds: 0 → 0x0000
 *   - Tens:     5 → 0x0005
 *   - Ones:     9 → 0x0009
 *   - Result: 0x0059 (bit pattern: 0000 0000 0101 1001)
 * 
 * @param n Decimal value to organize (0-999)
 * @return Value with each decimal digit in its own 4-bit nibble
 */
uint16_t BitsEncoder(uint16_t n)
{
    uint16_t result = 0;

    // Extract individual decimal digits
    const uint8_t div1 = n / DIGIT_DIVISOR_100;                              // Hundreds digit (0-9)
    const uint8_t div2 = (n / DIGIT_DIVISOR_10) % DIGIT_DIVISOR_10;           // Tens digit (0-9)
    const uint8_t mod = n % DIGIT_DIVISOR_10;                               // Ones digit (0-9)

    // Pack digits into nibbles: [bits 11-8: hundreds][bits 7-4: tens][bits 3-0: ones]
    // Mask with 0xF ensures only lower 4 bits of each digit are used
    result = (div1 & NIBBLE_MASK) << 8;     // Hundreds in bits 11-8
    result |= (div2 & NIBBLE_MASK) << 4;    // Tens in bits 7-4
    result |= (mod & NIBBLE_MASK);          // Ones in bits 3-0

    return result;
}

/**
 * @brief Encode year value into WWVB signal format
 * 
 * WWVB encodes the 2-digit year (00-99) in 8 bit positions using weighted binary.
 * The year is split across two groups with position 49 (a marker) in between:
 * - Positions 45-48: Four bits from ones digit (weights 80,40,20,10)
 * - Position 49: Marker bit (not used for year)
 * - Positions 50-53: Four bits from tens digit (weights 8,4,2,1)
 * 
 * Algorithm:
 * 1. Convert 4-digit year (e.g., 2024) to 2-digit year (24) using modulo 100
 * 2. Use BitsEncoder to organize digits into nibbles (e.g., 24 → 0x0024)
 * 3. Extract individual bits to produce weighted binary encoding
 * 4. Write bits to appropriate WWVB frame positions
 * 
 * Example: year=2024 → year_2digit=24 → bits_result=0x0024 (binary: 0000 0000 0010 0100)
 *   Position 45: bit 7 = 0
 *   Position 46: bit 6 = 0
 *   Position 47: bit 5 = 1
 *   Position 48: bit 4 = 0
 *   [Position 49 is a marker]
 *   Position 50: bit 3 = 0
 *   Position 51: bit 2 = 1
 *   Position 52: bit 1 = 0
 *   Position 53: bit 0 = 0
 * 
 * @param year Full 4-digit year (WWVB_MIN_YEAR to WWVB_MAX_YEAR, e.g., 2024)
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void EncodeYear(uint16_t year, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "EncodeYear: signal pointer is NULL");
        return;
    }
    
    if (year < WWVB_MIN_YEAR || year > WWVB_MAX_YEAR)
    {
        ESP_LOGE("WWVB", "EncodeYear: year %d is out of valid range (%d-%d)", 
                 year, WWVB_MIN_YEAR, WWVB_MAX_YEAR);
        return;
    }

    const int year_2digit = year % DIGIT_DIVISOR_100;  // Convert to 2-digit year (e.g., 2024 → 24)
    const uint16_t bits_result = BitsEncoder(year_2digit);  // Organize digits (e.g., 24 → 0x0024)

    // Encode year into WWVB signal positions 45-48 and 50-53 (8 bits total)
    // Extract and write individual bits to produce weighted binary encoding
    signal[WWVB_YEAR_BIT_45] = (bits_result & 0x80) >> 7;  // Bit 7
    signal[WWVB_YEAR_BIT_46] = (bits_result & 0x40) >> 6;  // Bit 6
    signal[WWVB_YEAR_BIT_47] = (bits_result & 0x20) >> 5;  // Bit 5
    signal[WWVB_YEAR_BIT_48] = (bits_result & 0x10) >> 4;  // Bit 4
    // Position 49 is a marker bit
    signal[WWVB_YEAR_BIT_50] = (bits_result & 0x08) >> 3;  // Bit 3
    signal[WWVB_YEAR_BIT_51] = (bits_result & 0x04) >> 2;  // Bit 2
    signal[WWVB_YEAR_BIT_52] = (bits_result & 0x02) >> 1;  // Bit 1
    signal[WWVB_YEAR_BIT_53] = (bits_result & 0x01);       // Bit 0
}

/**
 * @brief Encode day of year into WWVB signal format
 * 
 * WWVB encodes the day of year (Julian day, 001-366) in 10 bit positions using weighted binary.
 * The day is split across three groups with markers/reserved at positions 19, 24, and 29:
 * - Positions 22-23: Hundreds digit bits (weights 200, 100)
 * - Positions 25-28: Tens digit bits (weights 80, 40, 20, 10)
 * - Positions 30-33: Ones digit bits (weights 8, 4, 2, 1)
 * 
 * Example: day_of_year=365 → bits_result=0x0365 (binary: 0011 0110 0101)
 *   Position 22: bit 9 = 1  (weight 200)
 *   Position 23: bit 8 = 1  (weight 100)
 *   Position 25: bit 7 = 0  (weight 80)
 *   Position 26: bit 6 = 1  (weight 40)
 *   Position 27: bit 5 = 1  (weight 20)
 *   Position 28: bit 4 = 0  (weight 10)
 *   Position 30: bit 3 = 0  (weight 8)
 *   Position 31: bit 2 = 1  (weight 4)
 *   Position 32: bit 1 = 0  (weight 2)
 *   Position 33: bit 0 = 1  (weight 1)
 * 
 * @param dayOfYear Day of year (WWVB_MIN_DAY_OF_YEAR to WWVB_MAX_DAY_OF_YEAR, 1-366)
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void EncodeDayOfYear(uint16_t day_of_year, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "EncodeDayOfYear: signal pointer is NULL");
        return;
    }
    
    if (day_of_year < WWVB_MIN_DAY_OF_YEAR || day_of_year > WWVB_MAX_DAY_OF_YEAR)
    {
        ESP_LOGE("WWVB", "EncodeDayOfYear: day_of_year %d is out of valid range (%d-%d)", 
                 day_of_year, WWVB_MIN_DAY_OF_YEAR, WWVB_MAX_DAY_OF_YEAR);
        return;
    }

    const uint16_t bits_result = BitsEncoder(day_of_year);  // Organize digits (e.g., 365 → 0x0365)

    // Encode day of year into WWVB signal positions 22-23, 25-28, 30-33 (10 bits total)
    // Markers/reserved at positions 19, 24, and 29 break up the encoding
    signal[WWVB_DAY_BIT_22] = (bits_result & 0x0200) >> 9;  // Bit 9
    signal[WWVB_DAY_BIT_23] = (bits_result & 0x0100) >> 8;  // Bit 8
    // Position 24 is reserved (always 0)
    signal[WWVB_DAY_BIT_25] = (bits_result & 0x0080) >> 7;  // Bit 7
    signal[WWVB_DAY_BIT_26] = (bits_result & 0x0040) >> 6;  // Bit 6
    signal[WWVB_DAY_BIT_27] = (bits_result & 0x0020) >> 5;  // Bit 5
    signal[WWVB_DAY_BIT_28] = (bits_result & 0x0010) >> 4;  // Bit 4
    // Position 29 is a marker bit
    signal[WWVB_DAY_BIT_30] = (bits_result & 0x0008) >> 3;  // Bit 3
    signal[WWVB_DAY_BIT_31] = (bits_result & 0x0004) >> 2;  // Bit 2
    signal[WWVB_DAY_BIT_32] = (bits_result & 0x0002) >> 1;  // Bit 1
    signal[WWVB_DAY_BIT_33] = (bits_result & 0x0001);       // Bit 0
}

/**
 * @brief Encode hour value into WWVB signal format
 * 
 * WWVB encodes the hour (00-23) in 6 bit positions using weighted binary format.
 * Each bit position has a specific weight that contributes to the final hour value.
 * 
 * Bit weights:
 * - Positions 12-13: weights 20, 10 (tens place)
 * - Position 14: Reserved (always 0)
 * - Positions 15-18: weights 8, 4, 2, 1 (ones place)
 * 
 * Implementation:
 * The BitsEncoder() helper organizes the decimal digits into nibbles, allowing
 * us to extract bits from the ones digit (bits 3-0) and tens digit (bits 7-4)
 * separately, producing the weighted binary encoding WWVB requires.
 * 
 * Example: hour=13
 *   - BitsEncoder(13) returns 0x0013 (tens=1 in bits 7-4, ones=3 in bits 3-0)
 *   - Tens digit (1 = 0001): extract bits [1,0] → positions [12,13] → [0,1]
 *   - Ones digit (3 = 0011): extract bits [3,2,1,0] → positions [15,16,17,18] → [0,0,1,1]
 *   - Result represents: 0*20 + 1*10 + 0*8 + 0*4 + 1*2 + 1*1 = 13 ✓
 * 
 * @param hour Hour in 24-hour format (0 to WWVB_MAX_HOUR, 0-23)
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void EncodeHour(uint8_t hour, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "EncodeHour: signal pointer is NULL");
        return;
    }
    
    if (hour > WWVB_MAX_HOUR)
    {
        ESP_LOGE("WWVB", "EncodeHour: hour %d is out of valid range (0-%d)", hour, WWVB_MAX_HOUR);
        return;
    }

    const uint16_t bits_result = BitsEncoder(hour);  // Organize digits: 13 → 0x0013

    // Extract ones digit (bits 3-0) to get weights 8,4,2,1
    const uint8_t ones = bits_result & 0x0F;
    signal[WWVB_HOUR_BIT_15] = (ones >> 3) & 1;  // Bit 3 (weight 8)
    signal[WWVB_HOUR_BIT_16] = (ones >> 2) & 1;  // Bit 2 (weight 4)
    signal[WWVB_HOUR_BIT_17] = (ones >> 1) & 1;  // Bit 1 (weight 2)
    signal[WWVB_HOUR_BIT_18] = ones & 1;         // Bit 0 (weight 1)
    
    // Extract tens digit (bits 7-4) to get weights 20,10
    // For hours 0-23, tens digit is 0-2, so only bits 1-0 are used
    const uint8_t tens = (bits_result >> 4) & 0x0F;
    signal[WWVB_HOUR_BIT_12] = (tens >> 1) & 1;  // Bit 1 (weight 2 → 20 hours)
    signal[WWVB_HOUR_BIT_13] = tens & 1;         // Bit 0 (weight 1 → 10 hours)
    
    // Position 14 is reserved
}

/**
 * @brief Encode minute value into WWVB signal format
 * 
 * WWVB encodes the minute (00-59) in 7 bit positions using weighted binary format.
 * Each bit position has a specific weight that contributes to the final minute value.
 * 
 * Bit weights:
 * - Positions 1-3: weights 4, 2, 1 (ones place)
 * - Position 4: Reserved (always 0)
 * - Positions 5-8: weights 80, 40, 20, 10 (tens place)
 * 
 * Implementation:
 * The BitsEncoder() helper organizes the decimal digits into nibbles, allowing
 * us to extract bits from the ones digit (bits 3-0) and tens digit (bits 7-4)
 * separately, producing the weighted binary encoding WWVB requires.
 * 
 * Example: minute=42
 *   - BitsEncoder(42) returns 0x0042 (tens=4 in bits 7-4, ones=2 in bits 3-0)
 *   - Ones digit (2 = 0010): extract bits [2,1,0] → positions [1,2,3] → [0,1,0]
 *   - Tens digit (4 = 0100): extract bits [3,2,1,0] → positions [5,6,7,8] → [0,1,0,0]
 *   - Result represents: 0*4 + 1*2 + 0*1 + 0*80 + 1*40 + 0*20 + 0*10 = 42 ✓
 * 
 * Note: For minutes (0-59), tens digit is 0-5, so bit 3 (weight 80) is always 0.
 * 
 * @param minute Minute value (0 to WWVB_MAX_MINUTE, 0-59)
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void EncodeMinute(uint8_t minute, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "EncodeMinute: signal pointer is NULL");
        return;
    }
    
    if (minute > WWVB_MAX_MINUTE)
    {
        ESP_LOGE("WWVB", "EncodeMinute: minute %d is out of valid range (0-%d)", minute, WWVB_MAX_MINUTE);
        return;
    }

    const uint16_t bits_result = BitsEncoder(minute);  // Organize digits: 42 → 0x0042

    // Extract ones digit (bits 3-0) to get weights 4,2,1
    const uint8_t ones = bits_result & 0x0F;
    signal[WWVB_MINUTE_BIT_1] = (ones >> 2) & 1;  // Bit 2 (weight 4)
    signal[WWVB_MINUTE_BIT_2] = (ones >> 1) & 1;  // Bit 1 (weight 2)
    signal[WWVB_MINUTE_BIT_3] = ones & 1;         // Bit 0 (weight 1)
    
    // Position 4 is reserved
    
    // Extract tens digit (bits 7-4) to get weights 80,40,20,10
    // For minutes 0-59, tens digit is 0-5, so bit 3 (weight 80) is always 0
    const uint8_t tens = (bits_result >> 4) & 0x0F;
    signal[WWVB_MINUTE_BIT_5] = (tens >> 3) & 1;  // Bit 3 (weight 8 → 80 minutes, always 0)
    signal[WWVB_MINUTE_BIT_6] = (tens >> 2) & 1;  // Bit 2 (weight 4 → 40 minutes)
    signal[WWVB_MINUTE_BIT_7] = (tens >> 1) & 1;  // Bit 1 (weight 2 → 20 minutes)
    signal[WWVB_MINUTE_BIT_8] = tens & 1;         // Bit 0 (weight 1 → 10 minutes)
}

/**
 * @brief Set WWVB position markers and always-zero indicator bits
 * 
 * WWVB uses special marker bits for frame synchronization and reserved bits that
 * must always be zero. This function sets all such fixed-value bits in the frame.
 * 
 * Position Markers (0.8 second reduced power):
 *   - Every 10 seconds: positions 0, 9, 19, 29, 39, 49, 59
 *   - These help receivers synchronize to the frame structure
 *   - Easily distinguished from data bits by their longer (800ms) duration
 * 
 * Always-Zero Bits (reserved positions):
 *   - Positions: 4, 10, 11, 14, 20, 21, 24, 34, 35, 44, 54
 *   - These are reserved by the WWVB protocol specification
 *   - Receivers can use these for error detection (if not 0, frame is corrupted)
 * 
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void SetMarkersAndIndicators(volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "SetMarkersAndIndicators: signal pointer is NULL");
        return;
    }

    // Set position markers (0.8 second reduced power markers)
    signal[WWVB_MARKER_BIT_0] = WWVB_BIT_MARKER;
    signal[WWVB_MARKER_BIT_9] = WWVB_BIT_MARKER;
    signal[WWVB_MARKER_BIT_19] = WWVB_BIT_MARKER;
    signal[WWVB_MARKER_BIT_29] = WWVB_BIT_MARKER;
    signal[WWVB_MARKER_BIT_39] = WWVB_BIT_MARKER;
    signal[WWVB_MARKER_BIT_49] = WWVB_BIT_MARKER;
    signal[WWVB_MARKER_BIT_59] = WWVB_BIT_MARKER;

    // Set always-zero bits as per WWVB protocol
    signal[WWVB_ZERO_BIT_4] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_10] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_11] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_14] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_20] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_21] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_24] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_34] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_35] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_44] = WWVB_BIT_ZERO;
    signal[WWVB_ZERO_BIT_54] = WWVB_BIT_ZERO;
}

/**
 * @brief Set DUT1 (UT1-UTC difference) indicator bits to zero
 * 
 * DUT1 represents the difference between UT1 (astronomical time based on Earth's rotation)
 * and UTC (atomic time). This was historically used for celestial navigation.
 * 
 * As of 2013, NIST deprecated DUT1 encoding in WWVB because:
 * 1. GPS provides more accurate timing for navigation
 * 2. The encoding was complex and rarely used
 * 3. Modern applications don't require UT1-UTC difference
 * 
 * All DUT1 bits (positions 36-38 and 40-43) are now set to zero per current protocol.
 * Position 39 is a marker bit, not part of DUT1 encoding.
 * 
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void SetDUT1(volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "SetDUT1: signal pointer is NULL");
        return;
    }

    // DUT1 (UT1-UTC difference) is obsolete and was used for celestial navigation
    // All DUT1 bits are set to zero as per current WWVB protocol
    signal[WWVB_DUT1_BIT_36] = WWVB_BIT_ZERO;
    signal[WWVB_DUT1_BIT_37] = WWVB_BIT_ZERO;
    signal[WWVB_DUT1_BIT_38] = WWVB_BIT_ZERO;
    signal[WWVB_DUT1_BIT_40] = WWVB_BIT_ZERO;
    signal[WWVB_DUT1_BIT_41] = WWVB_BIT_ZERO;
    signal[WWVB_DUT1_BIT_42] = WWVB_BIT_ZERO;
    signal[WWVB_DUT1_BIT_43] = WWVB_BIT_ZERO;
}

/**
 * @brief Set leap year indicator bit
 * 
 * Sets position 55 to indicate whether the current year is a leap year.
 * WWVB receivers use this to correctly calculate dates from day-of-year values.
 * 
 * Algorithm uses mktime() trick to detect leap year:
 * 1. Create a tm structure for March 0 (day before March 1) of the target year
 * 2. Call mktime() which normalizes the date
 * 3. If the normalized day is 29, February has 29 days (leap year)
 * 4. If the normalized day is 28, February has 28 days (not leap year)
 * 
 * This approach is more reliable than manual leap year calculation because
 * it uses the C library's date normalization, which handles all edge cases.
 * 
 * Leap Year Rules (for reference):
 * - Divisible by 4: leap year
 * - EXCEPT divisible by 100: not a leap year
 * - EXCEPT divisible by 400: leap year
 * Examples: 2000 (leap), 1900 (not leap), 2024 (leap), 2100 (not leap)
 * 
 * @param year Full 4-digit year (WWVB_MIN_YEAR to WWVB_MAX_YEAR)
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void SetLeapYear(uint16_t year, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "SetLeapYear: signal pointer is NULL");
        return;
    }
    
    if (year < WWVB_MIN_YEAR || year > WWVB_MAX_YEAR)
    {
        ESP_LOGE("WWVB", "SetLeapYear: year %d is out of valid range (%d-%d)", 
                 year, WWVB_MIN_YEAR, WWVB_MAX_YEAR);
        return;
    }

    // Use mktime() to determine if year is a leap year
    // Set March 0 (which normalizes to last day of February)
    struct tm time_in = {0};
    time_in.tm_year = year - YEAR_OFFSET_1900;       // tm_year is years since 1900
    time_in.tm_mon = LEAP_YEAR_MARCH_MONTH;          // March (0-based: Jan=0, Feb=1, Mar=2)
    time_in.tm_mday = LEAP_YEAR_TEST_DAY;            // Day 0 of March

    mktime(&time_in);  // Normalize the date (March 0 → last day of February)

    // After normalization, if day is 29, then Feb has 29 days (leap year)
    // If day is 28, then Feb has 28 days (not a leap year)
    signal[WWVB_LEAP_YEAR_BIT] = (time_in.tm_mday == LEAP_YEAR_FEB_29) ? WWVB_BIT_ONE : WWVB_BIT_ZERO;
}

/**
 * @brief Set leap second warning indicator bit
 * 
 * Position 56 indicates whether a leap second will be inserted at the end of the
 * current month. When set to 1, a leap second (23:59:60) will occur at month's end.
 * 
 * Leap seconds are occasionally added to UTC to keep it synchronized with Earth's
 * rotation (UT1), which is gradually slowing down. The decision to add a leap second
 * is made by the International Earth Rotation and Reference Systems Service (IERS)
 * typically with several months notice.
 * 
 * This implementation sets the bit to 0 (no leap second) as:
 * 1. Leap seconds are rare (last one was December 31, 2016)
 * 2. Advance knowledge from IERS would be required
 * 3. Most consumer atomic clocks handle this automatically via WWVB signal
 * 
 * For production use requiring leap second support, you would need to:
 * - Monitor IERS Bulletin C announcements
 * - Update firmware/configuration when leap second is announced
 * - Set this bit to 1 for the entire month before the leap second
 * 
 * @param IsLeap true if leap second will occur at end of current month, false otherwise
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void SetLeapSecond(bool is_leap, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "SetLeapSecond: signal pointer is NULL");
        return;
    }

    signal[WWVB_LEAP_SECOND_BIT] = is_leap ? WWVB_BIT_ONE : WWVB_BIT_ZERO;
}

/**
 * @brief Set DST (Daylight Saving Time) indicator bits
 * 
 * Positions 57 and 58 encode the current DST status. Both bits must have the same value:
 * - Both bits = 1: DST is currently in effect
 * - Both bits = 0: Standard Time is currently in effect
 * - Mismatch (01 or 10): Invalid/error condition
 * 
 * The redundancy (two identical bits) provides error detection. If a receiver
 * sees mismatched values, it knows the frame is corrupted.
 * 
 * DST transitions (US rules since 2007):
 * - DST begins: 2:00 AM on the second Sunday in March (clocks spring forward to 3:00 AM)
 * - DST ends: 2:00 AM on the first Sunday in November (clocks fall back to 1:00 AM)
 * 
 * During the transition hour, WWVB continues to broadcast the time before the change,
 * then updates both the time and DST bits at the moment of transition.
 * 
 * @param IsDST true if DST is currently in effect, false for Standard Time
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void SetDST(bool is_dst, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "SetDST: signal pointer is NULL");
        return;
    }

    // Both DST bits must be set to the same value
    // 1,1 = DST in effect; 0,0 = Standard time in effect
    const uint8_t dst_value = is_dst ? WWVB_BIT_ONE : WWVB_BIT_ZERO;
    signal[WWVB_DST_BIT_57] = dst_value;
    signal[WWVB_DST_BIT_58] = dst_value;
}
