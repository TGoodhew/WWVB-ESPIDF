/*
 * WWVB Signal Encoding Module
 * 
 * Functions for encoding time data into WWVB signal format.
 * - Minutes and Hours use weighted binary encoding (e.g., 40+20+10+8+4+2+1 for minutes)
 * - Year and Day of Year use Binary-Coded Decimal (BCD) format
 * 
 * Reference: https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
 */

#ifndef WWVB_ENCODER_H
#define WWVB_ENCODER_H

#include <stdint.h>
#include <stdbool.h>

// WWVB Signal Array Size
#define WWVB_SIGNAL_ARRAY_SIZE 60

// WWVB Year encoding constants
#define WWVB_MIN_YEAR 2000
#define WWVB_MAX_YEAR 2099

// WWVB Day of year constants
#define WWVB_MIN_DAY_OF_YEAR 1
#define WWVB_MAX_DAY_OF_YEAR 366

// WWVB Hour constants
#define WWVB_MAX_HOUR 23

// WWVB Minute constants
#define WWVB_MAX_MINUTE 59

// WWVB Signal values
#define WWVB_BIT_ZERO 0
#define WWVB_BIT_ONE 1
#define WWVB_BIT_MARKER 2

// Time structure constants
#define YEAR_OFFSET_1900 1900    // tm_year offset from 1900 (tm_year = year - 1900)

/*
 * Encode a value in BCD format for WWVB
 * 
 * @param n The value to encode
 * @return The BCD-encoded value
 */
uint16_t BitsEncoder(uint16_t n);

/*
 * Encode year into WWVB signal array (8 bit BCD)
 * Encodes the year modulo 100 (e.g., 2024 -> 24) into WWVB signal positions.
 * 
 * @param year The year to encode (WWVB_MIN_YEAR to WWVB_MAX_YEAR)
 * @param signal The WWVB_SIGNAL_ARRAY_SIZE-element signal array
 */
void EncodeYear(uint16_t year, volatile uint8_t *signal);

/*
 * Encode day of year into WWVB signal array (10 bit BCD)
 * Encodes the day of year (Julian day) into WWVB signal positions.
 * 
 * @param day_of_year The day of year (WWVB_MIN_DAY_OF_YEAR to WWVB_MAX_DAY_OF_YEAR)
 * @param signal The WWVB_SIGNAL_ARRAY_SIZE-element signal array
 */
void EncodeDayOfYear(uint16_t day_of_year, volatile uint8_t *signal);

/*
 * Encode hour into WWVB signal array (6 bits, weighted binary)
 * Encodes the hour in 24-hour format into WWVB signal positions.
 * Uses weighted binary encoding: 20, 10, 8, 4, 2, 1
 * 
 * @param hour The hour (0 to WWVB_MAX_HOUR)
 * @param signal The WWVB_SIGNAL_ARRAY_SIZE-element signal array
 */
void EncodeHour(uint8_t hour, volatile uint8_t *signal);

/*
 * Encode minute into WWVB signal array (7 bits, weighted binary)
 * Encodes the minute value into WWVB signal positions.
 * Uses weighted binary encoding: 40, 20, 10, 8, 4, 2, 1
 * 
 * @param minute The minute (0 to WWVB_MAX_MINUTE)
 * @param signal The WWVB_SIGNAL_ARRAY_SIZE-element signal array
 */
void EncodeMinute(uint8_t minute, volatile uint8_t *signal);

/*
 * Set WWVB markers and always-zero indicators
 * Sets position markers (at seconds 0, 9, 19, 29, 39, 49, 59) and
 * fixed-zero bit positions as defined by WWVB protocol.
 * 
 * @param signal The WWVB_SIGNAL_ARRAY_SIZE-element signal array
 */
void SetMarkersAndIndicators(volatile uint8_t *signal);

/*
 * Set DUT1 bits (obsolete, set to 0)
 * DUT1 (UT1-UTC difference) is obsolete and no longer used for celestial navigation.
 * All DUT1 bits are set to zero.
 * 
 * @param signal The WWVB_SIGNAL_ARRAY_SIZE-element signal array
 */
void SetDUT1(volatile uint8_t *signal);

/*
 * Set leap year indicator
 * Sets the leap year bit based on whether the year is a leap year.
 * Uses mktime() to determine if February has 29 days.
 * 
 * @param year The year to check (WWVB_MIN_YEAR to WWVB_MAX_YEAR)
 * @param signal The WWVB_SIGNAL_ARRAY_SIZE-element signal array
 */
void SetLeapYear(uint16_t year, volatile uint8_t *signal);

/*
 * Set leap second indicator
 * Sets the leap second warning bit. A leap second is occasionally added
 * to UTC to account for Earth's irregular rotation.
 * 
 * @param is_leap True if leap second will occur at end of current month
 * @param signal The WWVB_SIGNAL_ARRAY_SIZE-element signal array
 */
void SetLeapSecond(bool is_leap, volatile uint8_t *signal);

/*
 * Set DST (Daylight Saving Time) indicator bits
 * Sets both DST bits (positions 57 and 58) to indicate whether DST is currently in effect.
 * Both bits are set to 1 when DST is active, 0 when standard time is in effect.
 * 
 * @param is_dst True if DST is currently in effect
 * @param signal The WWVB_SIGNAL_ARRAY_SIZE-element signal array
 */
void SetDST(bool is_dst, volatile uint8_t *signal);

#endif // WWVB_ENCODER_H
