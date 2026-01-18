/*
 * WWVB Signal Encoding Module
 * 
 * Functions for encoding time data into WWVB signal format.
 * WWVB uses Binary-Coded Decimal (BCD) format with specific bit positions
 * for year, day of year, hour, minute, and various indicators.
 * 
 * Reference: https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
 */

#ifndef WWVB_ENCODER_H
#define WWVB_ENCODER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Encode a value in BCD format for WWVB
 * 
 * @param n The value to encode
 * @return The BCD-encoded value
 */
uint16_t BitsEncoder(uint16_t n);

/*
 * Encode year into WWVB signal array (8 bit BCD)
 * 
 * @param year The year to encode (2000-2099)
 * @param signal The 60-element signal array
 */
void encodeYear(uint16_t year, volatile uint8_t *signal);

/*
 * Encode day of year into WWVB signal array (10 bit BCD)
 * 
 * @param dayOfYear The day of year (1-366)
 * @param signal The 60-element signal array
 */
void encodeDayOfYear(uint16_t dayOfYear, volatile uint8_t *signal);

/*
 * Encode hour into WWVB signal array (6 bit BCD)
 * 
 * @param hour The hour (0-23)
 * @param signal The 60-element signal array
 */
void encodeHour(uint8_t hour, volatile uint8_t *signal);

/*
 * Encode minute into WWVB signal array (7 bit BCD)
 * 
 * @param minute The minute (0-59)
 * @param signal The 60-element signal array
 */
void encodeMinute(uint8_t minute, volatile uint8_t *signal);

/*
 * Set WWVB markers and always-zero indicators
 * 
 * @param signal The 60-element signal array
 */
void setMarkersAndIndicators(volatile uint8_t *signal);

/*
 * Set DUT1 bits (obsolete, set to 0)
 * 
 * @param signal The 60-element signal array
 */
void setDUT1(volatile uint8_t *signal);

/*
 * Set leap year indicator
 * 
 * @param year The year to check
 * @param signal The 60-element signal array
 */
void setLeapYear(uint16_t year, volatile uint8_t *signal);

/*
 * Set leap second indicator
 * 
 * @param IsLeap True if leap second is present
 * @param signal The 60-element signal array
 */
void setLeapSecond(bool IsLeap, volatile uint8_t *signal);

/*
 * Set DST indicator bits
 * 
 * @param IsDST True if DST is in effect
 * @param signal The 60-element signal array
 */
void setDST(bool IsDST, volatile uint8_t *signal);

#endif // WWVB_ENCODER_H
