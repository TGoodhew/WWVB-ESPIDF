/*
 * DST (Daylight Saving Time) Calculation Module
 * 
 * These functions implement US DST rules as mandated since 2007:
 * - DST starts: Second Sunday in March at 2:00 AM
 * - DST ends: First Sunday in November at 2:00 AM
 * 
 * NOTE: These functions ONLY support US DST rules. If you need support for other
 * time zones or DST rules, you should use ESP-IDF's timezone support instead.
 */

#ifndef DST_CALC_H
#define DST_CALC_H

#include <stdbool.h>

// Days in month constants
#define DAYS_IN_JANUARY 31
#define DAYS_IN_FEBRUARY_NORMAL 28
#define DAYS_IN_FEBRUARY_LEAP 29
#define DAYS_IN_MARCH 31
#define DAYS_IN_APRIL 30
#define DAYS_IN_MAY 31
#define DAYS_IN_JUNE 30
#define DAYS_IN_JULY 31
#define DAYS_IN_AUGUST 31
#define DAYS_IN_SEPTEMBER 30
#define DAYS_IN_OCTOBER 31

// Day of week constants
#define DOW_SUNDAY 0
#define DOW_SATURDAY 6
#define DAYS_PER_WEEK 7

// Zeller's congruence constants
#define ZELLER_JANUARY_AS_MONTH_13 13
#define ZELLER_OFFSET_TO_STANDARD 6

/*
 * Determine if a year is a leap year
 * 
 * @param year The year to check
 * @return true if the year is a leap year, false otherwise
 */
bool IsLeapYear(int year);

/*
 * Calculate the start and end days (as day-of-year) for DST in a given year
 * 
 * @param year The year to calculate DST days for (e.g., 2024)
 * @param start_day Pointer to store the day-of-year when DST starts (1-366)
 * @param end_day Pointer to store the day-of-year when DST ends (1-366)
 */
void CalculateDSTDays(int year, int *start_day, int *end_day);

/*
 * Check if a given day is within the DST period for a given year
 * 
 * @param year The year (e.g., 2024)
 * @param days_passed Day of year (1-366, where 1 = January 1)
 * @return true if the day is during DST (on or after DST start, before DST end), false otherwise
 */
bool IsDaylightSavingTime(int year, int days_passed);

#endif // DST_CALC_H
