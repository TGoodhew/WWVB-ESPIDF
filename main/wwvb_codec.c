#include "wwvb_codec.h"

#include <time.h>

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

void encodeYear(uint16_t year, uint8_t *signal)
{
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

void encodeDayOfYear(uint16_t dayOfYear, uint8_t *signal)
{
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

void encodeHour(uint8_t hour, uint8_t *signal)
{
    uint16_t bitsResult = BitsEncoder(hour);

    signal[12] = (bitsResult & 0x20) >> 5;
    signal[13] = (bitsResult & 0x10) >> 4;
    signal[15] = (bitsResult & 0x08) >> 3;
    signal[16] = (bitsResult & 0x04) >> 2;
    signal[17] = (bitsResult & 0x02) >> 1;
    signal[18] = (bitsResult & 0x01);
}

void encodeMinute(uint8_t minute, uint8_t *signal)
{
    uint16_t bitsResult = BitsEncoder(minute);

    signal[1] = (bitsResult & 0x40) >> 6;
    signal[2] = (bitsResult & 0x20) >> 5;
    signal[3] = (bitsResult & 0x10) >> 4;
    signal[5] = (bitsResult & 0x08) >> 3;
    signal[6] = (bitsResult & 0x04) >> 2;
    signal[7] = (bitsResult & 0x02) >> 1;
    signal[8] = (bitsResult & 0x01);
}

void setMarkersAndIndicators(uint8_t *signal)
{
    signal[0] = 2;
    signal[9] = 2;
    signal[19] = 2;
    signal[29] = 2;
    signal[39] = 2;
    signal[49] = 2;
    signal[59] = 2;

    signal[4] = 0;
    signal[10] = 0;
    signal[11] = 0;
    signal[14] = 0;
    signal[20] = 0;
    signal[21] = 0;
    signal[24] = 0;
    signal[34] = 0;
    signal[35] = 0;
    signal[44] = 0;
    signal[54] = 0;
}

void setDUT1(uint8_t *signal)
{
    signal[36] = 0;
    signal[37] = 0;
    signal[38] = 0;
    signal[40] = 0;
    signal[41] = 0;
    signal[42] = 0;
    signal[43] = 0;
}

void setLeapYear(uint16_t year, uint8_t *signal)
{
    struct tm time_in = {0};
    time_in.tm_year = year - 1900;
    time_in.tm_mon = 2;
    time_in.tm_mday = 0;

    mktime(&time_in);
    signal[55] = (time_in.tm_mday == 29) ? 1 : 0;
}

void setLeapSecond(bool IsLeap, uint8_t *signal)
{
    signal[56] = IsLeap ? 1 : 0;
}

void setDST(bool IsDST, uint8_t *signal)
{
    signal[57] = IsDST ? 1 : 0;
    signal[58] = IsDST ? 1 : 0;
}
