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

/*
 * Determine if a year is a leap year
 * 
 * @param year The year to check
 * @return true if the year is a leap year, false otherwise
 */
bool isLeapYear(int year);

/*
 * Calculate the start and end days (as day-of-year) for DST in a given year
 * 
 * @param year The year to calculate DST days for (e.g., 2024)
 * @param startDay Pointer to store the day-of-year when DST starts (1-366)
 * @param endDay Pointer to store the day-of-year when DST ends (1-366)
 */
void calculateDSTDays(int year, int *startDay, int *endDay);

/*
 * Check if a given day is within the DST period for a given year
 * 
 * @param year The year (e.g., 2024)
 * @param daysPassed Day of year (1-366, where 1 = January 1)
 * @return true if the day is during DST (on or after DST start, before DST end), false otherwise
 */
bool isDaylightSavingTime(int year, int daysPassed);

#endif // DST_CALC_H
