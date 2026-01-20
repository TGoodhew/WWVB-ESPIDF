/*
 * Unit Tests for DST Calculation Module
 * 
 * Tests the US Daylight Saving Time calculation functions.
 */

#include "unity.h"
#include "dst_calc.h"

static void test_IsLeapYear_regular_leap_year(void)
{
    TEST_ASSERT_TRUE(IsLeapYear(2024));
    TEST_ASSERT_TRUE(IsLeapYear(2020));
    TEST_ASSERT_TRUE(IsLeapYear(2016));
}

static void test_IsLeapYear_non_leap_year(void)
{
    TEST_ASSERT_FALSE(IsLeapYear(2023));
    TEST_ASSERT_FALSE(IsLeapYear(2022));
    TEST_ASSERT_FALSE(IsLeapYear(2021));
}

static void test_IsLeapYear_century_non_leap(void)
{
    TEST_ASSERT_FALSE(IsLeapYear(1900));
    TEST_ASSERT_FALSE(IsLeapYear(2100));
}

static void test_IsLeapYear_century_leap(void)
{
    TEST_ASSERT_TRUE(IsLeapYear(2000));
    TEST_ASSERT_TRUE(IsLeapYear(2400));
}

static void test_CalculateDSTDays_2024(void)
{
    int start_day, end_day;
    CalculateDSTDays(2024, &start_day, &end_day);
    
    // 2024 DST: March 10 (day 70) to November 3 (day 308)
    TEST_ASSERT_EQUAL_INT(70, start_day);
    TEST_ASSERT_EQUAL_INT(308, end_day);
}

static void test_CalculateDSTDays_2023(void)
{
    int start_day, end_day;
    CalculateDSTDays(2023, &start_day, &end_day);
    
    // 2023 DST: March 12 (day 71) to November 5 (day 309)
    TEST_ASSERT_EQUAL_INT(71, start_day);
    TEST_ASSERT_EQUAL_INT(309, end_day);
}

static void test_CalculateDSTDays_2020(void)
{
    int start_day, end_day;
    CalculateDSTDays(2020, &start_day, &end_day);
    
    // 2020 DST: March 8 (day 68) to November 1 (day 306)
    TEST_ASSERT_EQUAL_INT(68, start_day);
    TEST_ASSERT_EQUAL_INT(306, end_day);
}

static void test_IsDaylightSavingTime_2024_before_DST(void)
{
    TEST_ASSERT_FALSE(IsDaylightSavingTime(2024, 50));  // Feb 19
    TEST_ASSERT_FALSE(IsDaylightSavingTime(2024, 69));  // Mar 9
}

static void test_IsDaylightSavingTime_2024_during_DST(void)
{
    TEST_ASSERT_TRUE(IsDaylightSavingTime(2024, 70));   // Mar 10 - DST starts
    TEST_ASSERT_TRUE(IsDaylightSavingTime(2024, 150));  // May 29
    TEST_ASSERT_TRUE(IsDaylightSavingTime(2024, 307));  // Nov 2
}

static void test_IsDaylightSavingTime_2024_after_DST(void)
{
    TEST_ASSERT_FALSE(IsDaylightSavingTime(2024, 308));  // Nov 3 - DST ends
    TEST_ASSERT_FALSE(IsDaylightSavingTime(2024, 350));  // Dec 15
}

static void test_IsDaylightSavingTime_boundaries(void)
{
    // Test January 1 (always standard time)
    TEST_ASSERT_FALSE(IsDaylightSavingTime(2024, 1));
    
    // Test December 31 (always standard time)
    TEST_ASSERT_FALSE(IsDaylightSavingTime(2024, 366));
}

void run_dst_calc_tests(void)
{
    RUN_TEST(test_IsLeapYear_regular_leap_year);
    RUN_TEST(test_IsLeapYear_non_leap_year);
    RUN_TEST(test_IsLeapYear_century_non_leap);
    RUN_TEST(test_IsLeapYear_century_leap);
    RUN_TEST(test_CalculateDSTDays_2024);
    RUN_TEST(test_CalculateDSTDays_2023);
    RUN_TEST(test_CalculateDSTDays_2020);
    RUN_TEST(test_IsDaylightSavingTime_2024_before_DST);
    RUN_TEST(test_IsDaylightSavingTime_2024_during_DST);
    RUN_TEST(test_IsDaylightSavingTime_2024_after_DST);
    RUN_TEST(test_IsDaylightSavingTime_boundaries);
}
