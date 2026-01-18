/*
 * WWVB Signal Encoding Module
 */

#include "wwvb_encoder.h"
#include <time.h>
#include <esp_log.h>

uint16_t BitsEncoder(uint16_t n)
{
    uint16_t result = 0;

    const uint8_t div1 = n / 100;
    const uint8_t div2 = (n - (div1 * 100)) / 10;
    const uint8_t mod = n % 10;

    result = (div1 & 0xF) << 8;
    result |= (div2 & 0xF) << 4;
    result |= (mod & 0xF);

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
    
    if (year < 2000 || year > 2099)
    {
        ESP_LOGE("WWVB", "encodeYear: year %d is out of valid range (2000-2099)", year);
        return;
    }

    int yearBCD = year % 100;
    uint16_t bitsResult = BitsEncoder(yearBCD);

    signal[45] = (bitsResult & 0x80) >> 7;
    signal[46] = (bitsResult & 0x40) >> 6;
    signal[47] = (bitsResult & 0x20) >> 5;
    signal[48] = (bitsResult & 0x10) >> 4;
    signal[50] = (bitsResult & 0x08) >> 3;
    signal[51] = (bitsResult & 0x04) >> 2;
    signal[52] = (bitsResult & 0x02) >> 1;
    signal[53] = (bitsResult & 0x01);
}

void encodeDayOfYear(uint16_t dayOfYear, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeDayOfYear: signal pointer is NULL");
        return;
    }
    
    if (dayOfYear < 1 || dayOfYear > 366)
    {
        ESP_LOGE("WWVB", "encodeDayOfYear: dayOfYear %d is out of valid range (1-366)", dayOfYear);
        return;
    }

    uint16_t bitsResult = BitsEncoder(dayOfYear);

    signal[22] = (bitsResult & 0x0200) >> 9;
    signal[23] = (bitsResult & 0x0100) >> 8;
    signal[25] = (bitsResult & 0x0080) >> 7;
    signal[26] = (bitsResult & 0x0040) >> 6;
    signal[27] = (bitsResult & 0x0020) >> 5;
    signal[28] = (bitsResult & 0x0010) >> 4;
    signal[30] = (bitsResult & 0x0008) >> 3;
    signal[31] = (bitsResult & 0x0004) >> 2;
    signal[32] = (bitsResult & 0x0002) >> 1;
    signal[33] = (bitsResult & 0x0001);
}

void encodeHour(uint8_t hour, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeHour: signal pointer is NULL");
        return;
    }
    
    if (hour > 23)
    {
        ESP_LOGE("WWVB", "encodeHour: hour %d is out of valid range (0-23)", hour);
        return;
    }

    uint16_t bitsResult = BitsEncoder(hour);

    signal[12] = (bitsResult & 0x20) >> 5;
    signal[13] = (bitsResult & 0x10) >> 4;
    signal[15] = (bitsResult & 0x08) >> 3;
    signal[16] = (bitsResult & 0x04) >> 2;
    signal[17] = (bitsResult & 0x02) >> 1;
    signal[18] = (bitsResult & 0x01);
}

void encodeMinute(uint8_t minute, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeMinute: signal pointer is NULL");
        return;
    }
    
    if (minute > 59)
    {
        ESP_LOGE("WWVB", "encodeMinute: minute %d is out of valid range (0-59)", minute);
        return;
    }

    uint16_t bitsResult = BitsEncoder(minute);

    signal[1] = (bitsResult & 0x40) >> 6;
    signal[2] = (bitsResult & 0x20) >> 5;
    signal[3] = (bitsResult & 0x10) >> 4;
    signal[5] = (bitsResult & 0x08) >> 3;
    signal[6] = (bitsResult & 0x04) >> 2;
    signal[7] = (bitsResult & 0x02) >> 1;
    signal[8] = (bitsResult & 0x01);
}

void setMarkersAndIndicators(volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setMarkersAndIndicators: signal pointer is NULL");
        return;
    }

    signal[0] = 2;  // Position marker
    signal[9] = 2;  // Position marker
    signal[19] = 2; // Position marker
    signal[29] = 2; // Position marker
    signal[39] = 2; // Position marker
    signal[49] = 2; // Position marker
    signal[59] = 2; // Position marker

    signal[4] = 0;  // Always 0
    signal[10] = 0; // Always 0
    signal[11] = 0; // Always 0
    signal[14] = 0; // Always 0
    signal[20] = 0; // Always 0
    signal[21] = 0; // Always 0
    signal[24] = 0; // Always 0
    signal[34] = 0; // Always 0
    signal[35] = 0; // Always 0
    signal[44] = 0; // Always 0
    signal[54] = 0; // Always 0
}

void setDUT1(volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setDUT1: signal pointer is NULL");
        return;
    }

    // DUT1 is obsolete, it was used for celestial navigation
    signal[36] = 0;
    signal[37] = 0;
    signal[38] = 0;
    signal[40] = 0;
    signal[41] = 0;
    signal[42] = 0;
    signal[43] = 0;
}

void setLeapYear(uint16_t year, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setLeapYear: signal pointer is NULL");
        return;
    }
    
    if (year < 2000 || year > 2099)
    {
        ESP_LOGE("WWVB", "setLeapYear: year %d is out of valid range (2000-2099)", year);
        return;
    }

    struct tm time_in = {0};
    time_in.tm_year = year - 1900;
    time_in.tm_mon = 2;  // March (0-based: January is 0)
    time_in.tm_mday = 0; // Zero day of March will roll back to the last day of February

    mktime(&time_in);

    // If mktime leaves the day as 29 then it is a leap year
    if (time_in.tm_mday == 29)
        signal[55] = 1;
    else
        signal[55] = 0;
}

void setLeapSecond(bool IsLeap, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setLeapSecond: signal pointer is NULL");
        return;
    }

    if (IsLeap)
        signal[56] = 1;
    else
        signal[56] = 0;
}

void setDST(bool IsDST, volatile uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setDST: signal pointer is NULL");
        return;
    }

    if (IsDST)
    {
        signal[57] = 1;
        signal[58] = 1;
    }
    else
    {
        signal[57] = 0;
        signal[58] = 0;
    }
}
