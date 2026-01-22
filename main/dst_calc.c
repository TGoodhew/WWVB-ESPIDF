/*
 * DST (Daylight Saving Time) Calculation Module
 * 
 * This module implements US DST rules as mandated since 2007:
 * - DST starts: 2:00 AM on the Second Sunday in March
 * - DST ends: 2:00 AM on the First Sunday in November
 * 
 * The calculations use Zeller's Congruence algorithm to determine the day of week
 * for any date, which allows us to find the specific Sundays when DST transitions occur.
 * 
 * IMPORTANT NOTES:
 * - These functions ONLY support US DST rules
 * - For other time zones or DST rules, use ESP-IDF's timezone support
 * - The WWVB signal encodes UTC time, but DST indicators tell receivers whether
 *   the local US time zone is observing DST
 * 
 * Unit tests: See test/main/test_dst_calc.c
 */

#include "dst_calc.h"
#include <esp_log.h>

// Leap year divisibility constants
#define LEAP_YEAR_DIVISOR_4 4
#define LEAP_YEAR_DIVISOR_100 100
#define LEAP_YEAR_DIVISOR_400 400

// DST calculation constants
#define DST_SECOND_SUNDAY_OFFSET 7   // Days from first Sunday to second Sunday
#define FIRST_DAY_OF_MONTH 1         // First day of any month

// Zeller's congruence calculation constants
#define ZELLER_MONTH_MULTIPLIER 13
#define ZELLER_CENTURY_MULTIPLIER 5

/**
 * @brief Determine if a year is a leap year
 * 
 * Implements the Gregorian calendar leap year rules:
 * - A year is a leap year if divisible by 4
 * - EXCEPT if divisible by 100, then it's NOT a leap year
 * - EXCEPT if divisible by 400, then it IS a leap year
 * 
 * Examples:
 * - 2024: divisible by 4, not by 100 → leap year ✓
 * - 2000: divisible by 4, by 100, AND by 400 → leap year ✓
 * - 1900: divisible by 4 and 100, but NOT by 400 → not a leap year ✗
 * - 2100: divisible by 4 and 100, but NOT by 400 → not a leap year ✗
 * 
 * This function is used internally to correctly calculate DST transition dates,
 * as leap years affect the day-of-week for dates later in the year.
 * 
 * @param year Full 4-digit year (e.g., 2024)
 * @return true if the year is a leap year, false otherwise
 */
bool IsLeapYear(int year)
{
    // A year is a leap year if:
    // - It's divisible by 4, AND
    // - Either it's not divisible by 100, OR it's divisible by 400
    return (year % LEAP_YEAR_DIVISOR_4 == 0 && 
            (year % LEAP_YEAR_DIVISOR_100 != 0 || year % LEAP_YEAR_DIVISOR_400 == 0));
}

/**
 * @brief Calculate the day-of-year for DST start and end dates
 * 
 * This function determines when DST begins and ends in a given year according to
 * US rules (since 2007):
 * - DST starts: 2nd Sunday in March
 * - DST ends: 1st Sunday in November
 * 
 * Algorithm Overview:
 * 1. Use Zeller's Congruence to find the day-of-week for January 1
 * 2. Calculate day-of-week for March 1 and November 1 based on January 1
 * 3. Find the first Sunday in March and November
 * 4. Add 7 days to March's first Sunday to get the 2nd Sunday
 * 5. Convert dates to day-of-year (Julian day)
 * 
 * Zeller's Congruence:
 * A mathematical algorithm to calculate the day of week for any date.
 * Formula: h = (q + ⌊(13(m+1))/5⌋ + K + ⌊K/4⌋ + ⌊J/4⌋ + 5J) mod 7
 * Where:
 *   q = day of month
 *   m = month (March=1, ..., February=12, with Jan/Feb of previous year)
 *   K = year of century (year mod 100)
 *   J = century (year / 100)
 *   h = day of week (0=Saturday, 1=Sunday, ..., 6=Friday)
 * 
 * We calculate for January 1 by treating it as month 13 of the previous year
 * in Zeller's system, then convert the result to standard Sunday=0 format.
 * 
 * @param year Full 4-digit year (e.g., 2024)
 * @param start_day Output: day-of-year when DST begins (e.g., 70 for March 10)
 * @param end_day Output: day-of-year when DST ends (e.g., 308 for November 3)
 */
void CalculateDSTDays(int year, int *start_day, int *end_day)
{
    // Validate input parameters
    if (start_day == NULL || end_day == NULL)
    {
        ESP_LOGE("DST", "CalculateDSTDays: NULL pointer provided");
        return;
    }
    
    // Determine number of days in February for this year
    const bool leap = IsLeapYear(year);
    const int days_in_feb = leap ? DAYS_IN_FEBRUARY_LEAP : DAYS_IN_FEBRUARY_NORMAL;
    
    // === Calculate day-of-week for January 1 using Zeller's congruence ===
    // In Zeller's algorithm, January is treated as month 13 of the previous year
    const int y = year - 1;                                // Previous year for Zeller's
    const int m = ZELLER_JANUARY_AS_MONTH_13;             // January as month 13
    const int q = FIRST_DAY_OF_MONTH;                     // Day 1 of the month
    
    // Apply Zeller's congruence formula
    const int century = y / LEAP_YEAR_DIVISOR_100;        // Century (e.g., 20 for 2023)
    const int year_of_century = y % LEAP_YEAR_DIVISOR_100; // Year within century (e.g., 23)
    
    // Zeller's formula: h = (q + ⌊13(m+1)/5⌋ + K + ⌊K/4⌋ + ⌊J/4⌋ + 5J) mod 7
    const int h = (q + ((ZELLER_MONTH_MULTIPLIER * (m + 1)) / ZELLER_CENTURY_MULTIPLIER) + 
                   year_of_century + (year_of_century / LEAP_YEAR_DIVISOR_4) + 
                   (century / LEAP_YEAR_DIVISOR_4) + ZELLER_CENTURY_MULTIPLIER * century) % DAYS_PER_WEEK;
    
    // Convert Zeller's result (0=Sat, 1=Sun, 2=Mon, ..., 6=Fri)
    // to standard (0=Sun, 1=Mon, ..., 6=Sat)
    const int jan1_dow = (h + ZELLER_OFFSET_TO_STANDARD) % DAYS_PER_WEEK;
    
    // === Calculate Second Sunday in March ===
    // Find day-of-year for March 1
    const int march1_doy = DAYS_IN_JANUARY + days_in_feb + FIRST_DAY_OF_MONTH;
    
    // Calculate day-of-week for March 1 based on January 1
    // (Add days elapsed since Jan 1, minus 1 because we're counting from day 1)
    const int march1_dow = (jan1_dow + (march1_doy - 1)) % DAYS_PER_WEEK;
    
    // Calculate days from March 1 until the first Sunday
    // If March 1 is already Sunday (dow=0), then 0 days; otherwise (7 - dow) days
    const int days_to_first_sunday = (march1_dow == DOW_SUNDAY) ? 0 : (DAYS_PER_WEEK - march1_dow);
    
    // Second Sunday is 7 days after first Sunday
    const int second_sunday_date = FIRST_DAY_OF_MONTH + days_to_first_sunday + DST_SECOND_SUNDAY_OFFSET;
    
    // Convert to day-of-year (subtract 1 because march1_doy already includes March 1)
    *start_day = march1_doy - 1 + second_sunday_date;
    
    // === Calculate First Sunday in November ===
    // Find day-of-year for November 1
    const int nov1_doy = DAYS_IN_JANUARY + days_in_feb + DAYS_IN_MARCH + DAYS_IN_APRIL + 
                         DAYS_IN_MAY + DAYS_IN_JUNE + DAYS_IN_JULY + DAYS_IN_AUGUST + 
                         DAYS_IN_SEPTEMBER + DAYS_IN_OCTOBER + FIRST_DAY_OF_MONTH;
    
    // Calculate day-of-week for November 1 based on January 1
    const int nov1_dow = (jan1_dow + (nov1_doy - 1)) % DAYS_PER_WEEK;
    
    // Calculate days from November 1 until the first Sunday
    const int days_to_first_sunday_nov = (nov1_dow == DOW_SUNDAY) ? 0 : (DAYS_PER_WEEK - nov1_dow);
    
    // First Sunday date in November
    const int first_sunday_date = FIRST_DAY_OF_MONTH + days_to_first_sunday_nov;
    
    // Convert to day-of-year
    *end_day = nov1_doy - 1 + first_sunday_date;
}

/**
 * @brief Check if a given day is within the DST period
 * 
 * Determines whether Daylight Saving Time is in effect on a specific day of the year.
 * Uses the calculateDSTDays() function to find DST boundaries, then checks if the
 * given day falls within that range.
 * 
 * DST Period (US rules since 2007):
 * - Begins: 2:00 AM on 2nd Sunday in March (clocks spring forward)
 * - Ends: 2:00 AM on 1st Sunday in November (clocks fall back)
 * 
 * Note: This function performs a day-level check. It does not account for the
 * specific hour (2:00 AM) when DST transitions occur. For most purposes (including
 * WWVB encoding), day-level precision is sufficient.
 * 
 * Example for 2024:
 * - DST starts: March 10 (day 70) at 2:00 AM
 * - DST ends: November 3 (day 308) at 2:00 AM
 * - isDaylightSavingTime(2024, 150) → true (May 29, within DST period)
 * - isDaylightSavingTime(2024, 50) → false (February 19, before DST)
 * - isDaylightSavingTime(2024, 350) → false (December 15, after DST)
 * 
 * @param year Full 4-digit year (e.g., 2024)
 * @param days_passed Day of year (1-366, where 1 = January 1)
 * @return true if DST is in effect on this day, false otherwise
 */
bool IsDaylightSavingTime(int year, int days_passed)
{
    int start_day, end_day;
    CalculateDSTDays(year, &start_day, &end_day);
    
    // DST is in effect if current day is >= start day AND < end day
    // Note: Uses < (not <=) for end_day because DST ends at 2 AM, so the full day
    // after transition is already in Standard Time
    return (days_passed >= start_day && days_passed < end_day);
}
