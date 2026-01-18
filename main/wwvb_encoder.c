/*
 * WWVB Signal Encoding Module
 */

#include "wwvb_encoder.h"
#include <time.h>
#include <esp_log.h>

// BCD encoding constants
#define BCD_DIVISOR_100 100
#define BCD_DIVISOR_10 10
#define BCD_MASK_NIBBLE 0xF

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
 * @brief Encode a decimal value into Binary-Coded Decimal (BCD) format
 * 
 * BCD represents each decimal digit as a 4-bit binary number (nibble).
 * This function converts a decimal number (0-999) into a 16-bit BCD value
 * where each nibble represents one decimal digit.
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
 * @param n Decimal value to encode (0-999)
 * @return BCD-encoded 16-bit value with each decimal digit in a 4-bit nibble
 */
uint16_t BitsEncoder(uint16_t n)
{
    uint16_t result = 0;

    // Extract individual decimal digits
    const uint8_t div1 = n / BCD_DIVISOR_100;                              // Hundreds digit (0-9)
    const uint8_t div2 = (n / BCD_DIVISOR_10) % BCD_DIVISOR_10;           // Tens digit (0-9)
    const uint8_t mod = n % BCD_DIVISOR_10;                               // Ones digit (0-9)

    // Pack digits into BCD format: [bits 11-8: hundreds][bits 7-4: tens][bits 3-0: ones]
    // Mask with 0xF ensures only lower 4 bits of each digit are used
    result = (div1 & BCD_MASK_NIBBLE) << 8;     // Hundreds in bits 11-8
    result |= (div2 & BCD_MASK_NIBBLE) << 4;    // Tens in bits 7-4
    result |= (mod & BCD_MASK_NIBBLE);          // Ones in bits 3-0

    return result;
}

/**
 * @brief Encode year value into WWVB signal format
 * 
 * WWVB encodes the 2-digit year (00-99) in 8 bit positions using BCD format.
 * The year is split across two groups with position 49 (a marker) in between:
 * - Positions 45-48: Lower nibble (ones digit) of year in BCD
 * - Position 49: Marker bit (not used for year)
 * - Positions 50-53: Upper nibble (tens digit) of year in BCD
 * 
 * Algorithm:
 * 1. Convert 4-digit year (e.g., 2024) to 2-digit year (24) using modulo 100
 * 2. Use BitsEncoder to get BCD representation (e.g., 24 → 0x24 = 0010 0100)
 * 3. Extract individual bits from BCD result
 * 4. Write bits to appropriate WWVB frame positions (LSB to MSB order)
 * 
 * Example: year=2024 → yearBCD=24 → bitsResult=0x0024 (binary: 0000 0000 0010 0100)
 *   Position 45: bit 7 (MSB of upper nibble) = 0
 *   Position 46: bit 6                        = 0
 *   Position 47: bit 5                        = 1
 *   Position 48: bit 4 (LSB of upper nibble)  = 0
 *   [Position 49 is a marker]
 *   Position 50: bit 3 (MSB of lower nibble)  = 0
 *   Position 51: bit 2                        = 1
 *   Position 52: bit 1                        = 0
 *   Position 53: bit 0 (LSB)                  = 0
 * 
 * @param year Full 4-digit year (WWVB_MIN_YEAR to WWVB_MAX_YEAR, e.g., 2024)
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void encodeYear(uint16_t year, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeYear: signal pointer is NULL");
        return;
    }
    
    if (year < WWVB_MIN_YEAR || year > WWVB_MAX_YEAR)
    {
        ESP_LOGE("WWVB", "encodeYear: year %d is out of valid range (%d-%d)", 
                 year, WWVB_MIN_YEAR, WWVB_MAX_YEAR);
        return;
    }

    const int yearBCD = year % BCD_DIVISOR_100;  // Convert to 2-digit year (e.g., 2024 → 24)
    const uint16_t bitsResult = BitsEncoder(yearBCD);  // Convert to BCD (e.g., 24 → 0x0024)

    // Encode year into WWVB signal positions 45-48 and 50-53 (8 bits total)
    // Extract and write individual bits from BCD result to frame positions
    // Bits are written in MSB-to-LSB order within each nibble
    signal[WWVB_YEAR_BIT_45] = (bitsResult & 0x80) >> 7;  // Upper nibble bit 3 (MSB)
    signal[WWVB_YEAR_BIT_46] = (bitsResult & 0x40) >> 6;  // Upper nibble bit 2
    signal[WWVB_YEAR_BIT_47] = (bitsResult & 0x20) >> 5;  // Upper nibble bit 1
    signal[WWVB_YEAR_BIT_48] = (bitsResult & 0x10) >> 4;  // Upper nibble bit 0 (LSB)
    // Position 49 is a marker bit
    signal[WWVB_YEAR_BIT_50] = (bitsResult & 0x08) >> 3;  // Lower nibble bit 3 (MSB)
    signal[WWVB_YEAR_BIT_51] = (bitsResult & 0x04) >> 2;  // Lower nibble bit 2
    signal[WWVB_YEAR_BIT_52] = (bitsResult & 0x02) >> 1;  // Lower nibble bit 1
    signal[WWVB_YEAR_BIT_53] = (bitsResult & 0x01);       // Lower nibble bit 0 (LSB)
}

/**
 * @brief Encode day of year into WWVB signal format
 * 
 * WWVB encodes the day of year (Julian day, 001-366) in 10 bit positions using BCD format.
 * The day is split across three groups with markers at positions 19, 24, and 29:
 * - Positions 22-23: Hundreds digit (0-3) - 2 bits
 * - Positions 25-28: Tens digit (0-9) - 4 bits
 * - Positions 30-33: Ones digit (0-9) - 4 bits
 * 
 * Example: dayOfYear=365 → bitsResult=0x0365 (binary: 0011 0110 0101)
 *   Position 22: bit 9 (hundreds bit 1)       = 1  (represents 2^9 = 512... but max is 366)
 *   Position 23: bit 8 (hundreds bit 0)       = 1  (represents 2^8 = 256)
 *   Position 25: bit 7 (tens bit 3, MSB)      = 0
 *   Position 26: bit 6 (tens bit 2)           = 1
 *   Position 27: bit 5 (tens bit 1)           = 1
 *   Position 28: bit 4 (tens bit 0, LSB)      = 0
 *   Position 30: bit 3 (ones bit 3, MSB)      = 0
 *   Position 31: bit 2 (ones bit 2)           = 1
 *   Position 32: bit 1 (ones bit 1)           = 0
 *   Position 33: bit 0 (ones bit 0, LSB)      = 1
 * 
 * @param dayOfYear Day of year (WWVB_MIN_DAY_OF_YEAR to WWVB_MAX_DAY_OF_YEAR, 1-366)
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void encodeDayOfYear(uint16_t dayOfYear, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeDayOfYear: signal pointer is NULL");
        return;
    }
    
    if (dayOfYear < WWVB_MIN_DAY_OF_YEAR || dayOfYear > WWVB_MAX_DAY_OF_YEAR)
    {
        ESP_LOGE("WWVB", "encodeDayOfYear: dayOfYear %d is out of valid range (%d-%d)", 
                 dayOfYear, WWVB_MIN_DAY_OF_YEAR, WWVB_MAX_DAY_OF_YEAR);
        return;
    }

    const uint16_t bitsResult = BitsEncoder(dayOfYear);  // Convert to BCD (e.g., 365 → 0x0365)

    // Encode day of year into WWVB signal positions 22-23, 25-28, 30-33 (10 bits total)
    // Markers at positions 19, 24, and 29 break up the encoding
    signal[WWVB_DAY_BIT_22] = (bitsResult & 0x0200) >> 9;  // Hundreds digit bit 1
    signal[WWVB_DAY_BIT_23] = (bitsResult & 0x0100) >> 8;  // Hundreds digit bit 0
    // Position 24 is reserved (always 0)
    signal[WWVB_DAY_BIT_25] = (bitsResult & 0x0080) >> 7;  // Tens digit bit 3 (MSB)
    signal[WWVB_DAY_BIT_26] = (bitsResult & 0x0040) >> 6;  // Tens digit bit 2
    signal[WWVB_DAY_BIT_27] = (bitsResult & 0x0020) >> 5;  // Tens digit bit 1
    signal[WWVB_DAY_BIT_28] = (bitsResult & 0x0010) >> 4;  // Tens digit bit 0 (LSB)
    // Position 29 is a marker bit
    signal[WWVB_DAY_BIT_30] = (bitsResult & 0x0008) >> 3;  // Ones digit bit 3 (MSB)
    signal[WWVB_DAY_BIT_31] = (bitsResult & 0x0004) >> 2;  // Ones digit bit 2
    signal[WWVB_DAY_BIT_32] = (bitsResult & 0x0002) >> 1;  // Ones digit bit 1
    signal[WWVB_DAY_BIT_33] = (bitsResult & 0x0001);       // Ones digit bit 0 (LSB)
}

/**
 * @brief Encode hour value into WWVB signal format
 * 
 * WWVB encodes the hour (00-23) in 6 bit positions using BCD format.
 * The hour is split across two groups with markers/reserved bits between them:
 * - Positions 12-13: Tens digit (0-2) - 2 bits (only need 0-2 for 00-23)
 * - Position 14: Reserved (always 0)
 * - Positions 15-18: Ones digit (0-9) - 4 bits
 * 
 * Example: hour=13 → bitsResult=0x0013 (binary: 0001 0011)
 *   Position 12: bit 5 (tens bit 1)         = 1  (represents 10)
 *   Position 13: bit 4 (tens bit 0)         = 0
 *   Position 15: bit 3 (ones bit 3, MSB)    = 0
 *   Position 16: bit 2 (ones bit 2)         = 0
 *   Position 17: bit 1 (ones bit 1)         = 1
 *   Position 18: bit 0 (ones bit 0, LSB)    = 1
 * 
 * @param hour Hour in 24-hour format (0 to WWVB_MAX_HOUR, 0-23)
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void encodeHour(uint8_t hour, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeHour: signal pointer is NULL");
        return;
    }
    
    if (hour > WWVB_MAX_HOUR)
    {
        ESP_LOGE("WWVB", "encodeHour: hour %d is out of valid range (0-%d)", hour, WWVB_MAX_HOUR);
        return;
    }

    const uint16_t bitsResult = BitsEncoder(hour);  // Convert to BCD (e.g., 13 → 0x0013)

    // Encode hour into WWVB signal positions 12-13 and 15-18 (6 bits total)
    // Position 14 is reserved (always 0)
    signal[WWVB_HOUR_BIT_12] = (bitsResult & 0x20) >> 5;  // Tens digit bit 1
    signal[WWVB_HOUR_BIT_13] = (bitsResult & 0x10) >> 4;  // Tens digit bit 0
    // Position 14 is reserved
    signal[WWVB_HOUR_BIT_15] = (bitsResult & 0x08) >> 3;  // Ones digit bit 3 (MSB)
    signal[WWVB_HOUR_BIT_16] = (bitsResult & 0x04) >> 2;  // Ones digit bit 2
    signal[WWVB_HOUR_BIT_17] = (bitsResult & 0x02) >> 1;  // Ones digit bit 1
    signal[WWVB_HOUR_BIT_18] = (bitsResult & 0x01);       // Ones digit bit 0 (LSB)
}

/**
 * @brief Encode minute value into WWVB signal format
 * 
 * WWVB encodes the minute (00-59) in 7 bit positions using BCD format.
 * The minute is split across two groups with marker/reserved bits between them:
 * - Positions 1-3: Ones digit (0-9) - lower 3 bits only (bit 3 unused)
 * - Position 4: Reserved (always 0)
 * - Positions 5-8: Tens digit (0-5) - 4 bits
 * 
 * Note: The ones digit only uses 3 bits (positions 1-3), so bit 3 is never set.
 * This is sufficient since it encodes values 0-9 which only need 4 bits maximum,
 * but WWVB protocol only uses the lower 3 bits for ones digit of minutes.
 * 
 * Example: minute=42 → bitsResult=0x0042 (binary: 0100 0010)
 *   Position 1: bit 6 (ones bit 2... wait, let me recalculate)
 *   Actually: 42 in BCD = 0x42 = 0100 0010
 *   Tens digit = 4 = 0100, Ones digit = 2 = 0010
 *   Position 1: bit 6 (ones bit 2, MSB used)  = 0
 *   Position 2: bit 5 (ones bit 1)            = 1
 *   Position 3: bit 4 (ones bit 0, LSB)       = 0
 *   Position 5: bit 3 (tens bit 3, MSB)       = 0
 *   Position 6: bit 2 (tens bit 2)            = 1
 *   Position 7: bit 1 (tens bit 1)            = 0
 *   Position 8: bit 0 (tens bit 0, LSB)       = 0
 * 
 * @param minute Minute value (0 to WWVB_MAX_MINUTE, 0-59)
 * @param signal Pointer to 60-byte WWVB signal array to update
 */
void encodeMinute(uint8_t minute, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeMinute: signal pointer is NULL");
        return;
    }
    
    if (minute > WWVB_MAX_MINUTE)
    {
        ESP_LOGE("WWVB", "encodeMinute: minute %d is out of valid range (0-%d)", minute, WWVB_MAX_MINUTE);
        return;
    }

    const uint16_t bitsResult = BitsEncoder(minute);  // Convert to BCD (e.g., 42 → 0x0042)

    // Encode minute into WWVB signal positions 1-3 and 5-8 (7 bits total)
    // Position 0 is a marker, Position 4 is reserved (always 0)
    signal[WWVB_MINUTE_BIT_1] = (bitsResult & 0x40) >> 6;  // Ones digit bit 2
    signal[WWVB_MINUTE_BIT_2] = (bitsResult & 0x20) >> 5;  // Ones digit bit 1
    signal[WWVB_MINUTE_BIT_3] = (bitsResult & 0x10) >> 4;  // Ones digit bit 0
    // Position 4 is reserved
    signal[WWVB_MINUTE_BIT_5] = (bitsResult & 0x08) >> 3;  // Tens digit bit 3 (MSB)
    signal[WWVB_MINUTE_BIT_6] = (bitsResult & 0x04) >> 2;  // Tens digit bit 2
    signal[WWVB_MINUTE_BIT_7] = (bitsResult & 0x02) >> 1;  // Tens digit bit 1
    signal[WWVB_MINUTE_BIT_8] = (bitsResult & 0x01);       // Tens digit bit 0 (LSB)
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
void setMarkersAndIndicators(volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setMarkersAndIndicators: signal pointer is NULL");
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
void setDUT1(volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setDUT1: signal pointer is NULL");
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
void setLeapYear(uint16_t year, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setLeapYear: signal pointer is NULL");
        return;
    }
    
    if (year < WWVB_MIN_YEAR || year > WWVB_MAX_YEAR)
    {
        ESP_LOGE("WWVB", "setLeapYear: year %d is out of valid range (%d-%d)", 
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
void setLeapSecond(bool IsLeap, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setLeapSecond: signal pointer is NULL");
        return;
    }

    signal[WWVB_LEAP_SECOND_BIT] = IsLeap ? WWVB_BIT_ONE : WWVB_BIT_ZERO;
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
void setDST(bool IsDST, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setDST: signal pointer is NULL");
        return;
    }

    // Both DST bits must be set to the same value
    // 1,1 = DST in effect; 0,0 = Standard time in effect
    const uint8_t dstValue = IsDST ? WWVB_BIT_ONE : WWVB_BIT_ZERO;
    signal[WWVB_DST_BIT_57] = dstValue;
    signal[WWVB_DST_BIT_58] = dstValue;
}
