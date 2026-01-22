/*
 * Unit Tests for WWVB Encoder Module
 * 
 * Tests the BCD encoding and WWVB signal array encoding functions.
 */

#include <string.h>
#include "unity.h"
#include "wwvb_encoder.h"

static uint8_t test_signal[WWVB_SIGNAL_ARRAY_SIZE];

static void reset_test_signal(void)
{
    memset(test_signal, 0xFF, WWVB_SIGNAL_ARRAY_SIZE);
}

static void test_BitsEncoder_zero(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x0000, BitsEncoder(0));
}

static void test_BitsEncoder_single_digit(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x0009, BitsEncoder(9));
    TEST_ASSERT_EQUAL_UINT16(0x0005, BitsEncoder(5));
}

static void test_BitsEncoder_two_digits(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x0024, BitsEncoder(24));
    TEST_ASSERT_EQUAL_UINT16(0x0059, BitsEncoder(59));
    TEST_ASSERT_EQUAL_UINT16(0x0042, BitsEncoder(42));
}

static void test_BitsEncoder_three_digits(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x0365, BitsEncoder(365));
    TEST_ASSERT_EQUAL_UINT16(0x0142, BitsEncoder(142));
}

static void test_EncodeYear_2024(void)
{
    reset_test_signal();
    EncodeYear(2024, test_signal);
    
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[45]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[46]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[47]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[48]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[50]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[51]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[52]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[53]);
}

static void test_EncodeYear_2000(void)
{
    reset_test_signal();
    EncodeYear(2000, test_signal);
    
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[45]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[46]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[47]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[48]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[50]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[51]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[52]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[53]);
}

static void test_EncodeDayOfYear_001(void)
{
    reset_test_signal();
    EncodeDayOfYear(1, test_signal);
    
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[22]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[23]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[25]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[26]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[27]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[28]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[30]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[31]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[32]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[33]);
}

static void test_EncodeDayOfYear_365(void)
{
    reset_test_signal();
    EncodeDayOfYear(365, test_signal);
    
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[22]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[23]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[25]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[26]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[27]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[28]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[30]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[31]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[32]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[33]);
}

static void test_EncodeHour_00(void)
{
    reset_test_signal();
    EncodeHour(0, test_signal);
    
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[12]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[13]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[15]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[16]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[17]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[18]);
}

static void test_EncodeHour_13(void)
{
    reset_test_signal();
    EncodeHour(13, test_signal);
    
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[12]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[13]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[15]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[16]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[17]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[18]);
}

static void test_EncodeMinute_00(void)
{
    reset_test_signal();
    EncodeMinute(0, test_signal);
    
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[1]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[2]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[3]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[5]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[6]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[7]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[8]);
}

static void test_EncodeMinute_42(void)
{
    reset_test_signal();
    EncodeMinute(42, test_signal);
    
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[1]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[2]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[3]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[5]);
    TEST_ASSERT_EQUAL_UINT8(1, test_signal[6]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[7]);
    TEST_ASSERT_EQUAL_UINT8(0, test_signal[8]);
}

static void test_SetMarkersAndIndicators(void)
{
    reset_test_signal();
    SetMarkersAndIndicators(test_signal);
    
    // Check markers
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_MARKER, test_signal[0]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_MARKER, test_signal[9]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_MARKER, test_signal[19]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_MARKER, test_signal[29]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_MARKER, test_signal[39]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_MARKER, test_signal[49]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_MARKER, test_signal[59]);
    
    // Check always-zero bits
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ZERO, test_signal[4]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ZERO, test_signal[10]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ZERO, test_signal[11]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ZERO, test_signal[14]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ZERO, test_signal[20]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ZERO, test_signal[21]);
}

static void test_SetLeapYear_2024(void)
{
    reset_test_signal();
    SetLeapYear(2024, test_signal);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ONE, test_signal[55]);
}

static void test_SetLeapYear_2023(void)
{
    reset_test_signal();
    SetLeapYear(2023, test_signal);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ZERO, test_signal[55]);
}

static void test_SetDST_false(void)
{
    reset_test_signal();
    SetDST(false, test_signal);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ZERO, test_signal[57]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ZERO, test_signal[58]);
}

static void test_SetDST_true(void)
{
    reset_test_signal();
    SetDST(true, test_signal);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ONE, test_signal[57]);
    TEST_ASSERT_EQUAL_UINT8(WWVB_BIT_ONE, test_signal[58]);
}

void run_wwvb_encoder_tests(void)
{
    RUN_TEST(test_BitsEncoder_zero);
    RUN_TEST(test_BitsEncoder_single_digit);
    RUN_TEST(test_BitsEncoder_two_digits);
    RUN_TEST(test_BitsEncoder_three_digits);
    RUN_TEST(test_EncodeYear_2024);
    RUN_TEST(test_EncodeYear_2000);
    RUN_TEST(test_EncodeDayOfYear_001);
    RUN_TEST(test_EncodeDayOfYear_365);
    RUN_TEST(test_EncodeHour_00);
    RUN_TEST(test_EncodeHour_13);
    RUN_TEST(test_EncodeMinute_00);
    RUN_TEST(test_EncodeMinute_42);
    RUN_TEST(test_SetMarkersAndIndicators);
    RUN_TEST(test_SetLeapYear_2024);
    RUN_TEST(test_SetLeapYear_2023);
    RUN_TEST(test_SetDST_false);
    RUN_TEST(test_SetDST_true);
}
