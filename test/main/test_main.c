#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_task_wdt.h"
#include "unity.h"

void app_main(void)
{
    // Test app uses an interactive Unity menu; disable task WDT to avoid timeout
    // while waiting for console input.
    esp_task_wdt_deinit();
    unity_run_menu();
}

uint16_t BitsEncoder(uint16_t n);
void encodeYear(uint16_t year, uint8_t *signal);
void encodeDayOfYear(uint16_t dayOfYear, uint8_t *signal);
void encodeHour(uint8_t hour, uint8_t *signal);
void encodeMinute(uint8_t minute, uint8_t *signal);
void setMarkersAndIndicators(uint8_t *signal);
void setDUT1(uint8_t *signal);
void setLeapYear(uint16_t year, uint8_t *signal);
void setLeapSecond(bool IsLeap, uint8_t *signal);
void setDST(bool IsDST, uint8_t *signal);
void calculateDSTDays(int year, int *startDay, int *endDay);
bool isDaylightSavingTime(int year, int daysPassed);

TEST_CASE("BitsEncoder encodes BCD values", "[wwvb]")
{
    TEST_ASSERT_EQUAL_HEX16(0x000, BitsEncoder(0));
    TEST_ASSERT_EQUAL_HEX16(0x059, BitsEncoder(59));
    TEST_ASSERT_EQUAL_HEX16(0x365, BitsEncoder(365));
}

TEST_CASE("encodeYear maps 2025 into expected bit positions", "[wwvb]")
{
    uint8_t signal[60] = {0};
    encodeYear(2025, signal);

    TEST_ASSERT_EQUAL_UINT8(0, signal[45]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[46]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[47]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[48]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[50]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[51]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[52]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[53]);
}

TEST_CASE("encodeDayOfYear maps 365 into expected bit positions", "[wwvb]")
{
    uint8_t signal[60] = {0};
    encodeDayOfYear(365, signal);

    TEST_ASSERT_EQUAL_UINT8(1, signal[22]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[23]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[25]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[26]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[27]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[28]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[30]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[31]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[32]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[33]);
}

TEST_CASE("encodeHour and encodeMinute set expected bits", "[wwvb]")
{
    uint8_t signal[60] = {0};
    encodeHour(23, signal);
    encodeMinute(59, signal);

    TEST_ASSERT_EQUAL_UINT8(1, signal[17]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[18]);

    TEST_ASSERT_EQUAL_UINT8(1, signal[1]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[2]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[3]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[5]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[6]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[7]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[8]);
}

TEST_CASE("setMarkersAndIndicators sets markers and fixed zero bits", "[wwvb]")
{
    uint8_t signal[60];
    memset(signal, 0xFF, sizeof(signal));

    setMarkersAndIndicators(signal);

    TEST_ASSERT_EQUAL_UINT8(2, signal[0]);
    TEST_ASSERT_EQUAL_UINT8(2, signal[9]);
    TEST_ASSERT_EQUAL_UINT8(2, signal[19]);
    TEST_ASSERT_EQUAL_UINT8(2, signal[29]);
    TEST_ASSERT_EQUAL_UINT8(2, signal[39]);
    TEST_ASSERT_EQUAL_UINT8(2, signal[49]);
    TEST_ASSERT_EQUAL_UINT8(2, signal[59]);

    TEST_ASSERT_EQUAL_UINT8(0, signal[4]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[10]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[11]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[14]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[20]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[21]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[24]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[34]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[35]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[44]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[54]);
}

TEST_CASE("setDUT1 clears DUT1 slots", "[wwvb]")
{
    uint8_t signal[60];
    memset(signal, 0xFF, sizeof(signal));

    setDUT1(signal);

    TEST_ASSERT_EQUAL_UINT8(0, signal[36]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[37]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[38]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[40]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[41]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[42]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[43]);
}

TEST_CASE("setLeapYear sets leap-year flag", "[wwvb]")
{
    uint8_t signal[60] = {0};
    setLeapYear(2024, signal);
    TEST_ASSERT_EQUAL_UINT8(1, signal[55]);

    setLeapYear(2023, signal);
    TEST_ASSERT_EQUAL_UINT8(0, signal[55]);
}

TEST_CASE("setLeapSecond and setDST set control bits", "[wwvb]")
{
    uint8_t signal[60] = {0};

    setLeapSecond(true, signal);
    TEST_ASSERT_EQUAL_UINT8(1, signal[56]);
    setLeapSecond(false, signal);
    TEST_ASSERT_EQUAL_UINT8(0, signal[56]);

    setDST(true, signal);
    TEST_ASSERT_EQUAL_UINT8(1, signal[57]);
    TEST_ASSERT_EQUAL_UINT8(1, signal[58]);

    setDST(false, signal);
    TEST_ASSERT_EQUAL_UINT8(0, signal[57]);
    TEST_ASSERT_EQUAL_UINT8(0, signal[58]);
}

TEST_CASE("isDaylightSavingTime returns expected seasonal values", "[wwvb]")
{
    TEST_ASSERT_FALSE(isDaylightSavingTime(2025, 15));
    TEST_ASSERT_TRUE(isDaylightSavingTime(2025, 196));
    TEST_ASSERT_FALSE(isDaylightSavingTime(2025, 349));
}

TEST_CASE("isDaylightSavingTime honors 2025 transition boundaries", "[wwvb]")
{
    int startDay = 0;
    int endDay = 0;

    calculateDSTDays(2025, &startDay, &endDay);
    TEST_ASSERT_EQUAL_INT(68, startDay);   // 2025-03-09
    TEST_ASSERT_EQUAL_INT(306, endDay);    // 2025-11-02

    TEST_ASSERT_FALSE(isDaylightSavingTime(2025, 67));
    TEST_ASSERT_TRUE(isDaylightSavingTime(2025, 68));
    TEST_ASSERT_TRUE(isDaylightSavingTime(2025, 305));
    TEST_ASSERT_FALSE(isDaylightSavingTime(2025, 306));
}
