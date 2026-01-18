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
#define LEAP_YEAR_MARCH_MONTH 2  // March (0-based: January is 0)
#define LEAP_YEAR_TEST_DAY 0     // Zero day of March rolls back to last day of February
#define LEAP_YEAR_FEB_29 29      // Day value for February 29
#define YEAR_OFFSET_1900 1900    // tm_year offset from 1900

uint16_t BitsEncoder(uint16_t n)
{
    uint16_t result = 0;

    const uint8_t div1 = n / BCD_DIVISOR_100;
    const uint8_t div2 = (n % BCD_DIVISOR_100) / BCD_DIVISOR_10;
    const uint8_t mod = n % BCD_DIVISOR_10;

    result = (div1 & BCD_MASK_NIBBLE) << 8;
    result |= (div2 & BCD_MASK_NIBBLE) << 4;
    result |= (mod & BCD_MASK_NIBBLE);

    return result;
}

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

    const int yearBCD = year % BCD_DIVISOR_100;
    const uint16_t bitsResult = BitsEncoder(yearBCD);

    // Encode year into WWVB signal positions (8 bits)
    signal[WWVB_YEAR_BIT_45] = (bitsResult & 0x80) >> 7;
    signal[WWVB_YEAR_BIT_46] = (bitsResult & 0x40) >> 6;
    signal[WWVB_YEAR_BIT_47] = (bitsResult & 0x20) >> 5;
    signal[WWVB_YEAR_BIT_48] = (bitsResult & 0x10) >> 4;
    signal[WWVB_YEAR_BIT_50] = (bitsResult & 0x08) >> 3;
    signal[WWVB_YEAR_BIT_51] = (bitsResult & 0x04) >> 2;
    signal[WWVB_YEAR_BIT_52] = (bitsResult & 0x02) >> 1;
    signal[WWVB_YEAR_BIT_53] = (bitsResult & 0x01);
}

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

    const uint16_t bitsResult = BitsEncoder(dayOfYear);

    // Encode day of year into WWVB signal positions (10 bits)
    signal[WWVB_DAY_BIT_22] = (bitsResult & 0x0200) >> 9;
    signal[WWVB_DAY_BIT_23] = (bitsResult & 0x0100) >> 8;
    signal[WWVB_DAY_BIT_25] = (bitsResult & 0x0080) >> 7;
    signal[WWVB_DAY_BIT_26] = (bitsResult & 0x0040) >> 6;
    signal[WWVB_DAY_BIT_27] = (bitsResult & 0x0020) >> 5;
    signal[WWVB_DAY_BIT_28] = (bitsResult & 0x0010) >> 4;
    signal[WWVB_DAY_BIT_30] = (bitsResult & 0x0008) >> 3;
    signal[WWVB_DAY_BIT_31] = (bitsResult & 0x0004) >> 2;
    signal[WWVB_DAY_BIT_32] = (bitsResult & 0x0002) >> 1;
    signal[WWVB_DAY_BIT_33] = (bitsResult & 0x0001);
}

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

    const uint16_t bitsResult = BitsEncoder(hour);

    // Encode hour into WWVB signal positions (6 bits)
    signal[WWVB_HOUR_BIT_12] = (bitsResult & 0x20) >> 5;
    signal[WWVB_HOUR_BIT_13] = (bitsResult & 0x10) >> 4;
    signal[WWVB_HOUR_BIT_15] = (bitsResult & 0x08) >> 3;
    signal[WWVB_HOUR_BIT_16] = (bitsResult & 0x04) >> 2;
    signal[WWVB_HOUR_BIT_17] = (bitsResult & 0x02) >> 1;
    signal[WWVB_HOUR_BIT_18] = (bitsResult & 0x01);
}

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

    const uint16_t bitsResult = BitsEncoder(minute);

    // Encode minute into WWVB signal positions (7 bits)
    signal[WWVB_MINUTE_BIT_1] = (bitsResult & 0x40) >> 6;
    signal[WWVB_MINUTE_BIT_2] = (bitsResult & 0x20) >> 5;
    signal[WWVB_MINUTE_BIT_3] = (bitsResult & 0x10) >> 4;
    signal[WWVB_MINUTE_BIT_5] = (bitsResult & 0x08) >> 3;
    signal[WWVB_MINUTE_BIT_6] = (bitsResult & 0x04) >> 2;
    signal[WWVB_MINUTE_BIT_7] = (bitsResult & 0x02) >> 1;
    signal[WWVB_MINUTE_BIT_8] = (bitsResult & 0x01);
}

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

    // Use mktime to determine if year is a leap year by checking if Feb has 29 days
    struct tm time_in = {0};
    time_in.tm_year = year - YEAR_OFFSET_1900;
    time_in.tm_mon = LEAP_YEAR_MARCH_MONTH;     // March (0-based: January is 0)
    time_in.tm_mday = LEAP_YEAR_TEST_DAY;       // Zero day of March rolls back to last day of Feb

    mktime(&time_in);

    // If mktime leaves the day as 29 then February has 29 days (leap year)
    signal[WWVB_LEAP_YEAR_BIT] = (time_in.tm_mday == LEAP_YEAR_FEB_29) ? WWVB_BIT_ONE : WWVB_BIT_ZERO;
}

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
