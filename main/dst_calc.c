/*
 * DST (Daylight Saving Time) Calculation Module
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

bool isLeapYear(int year)
{
    // A year is a leap year if:
    // - It's divisible by 4, AND
    // - Either it's not divisible by 100, OR it's divisible by 400
    return (year % LEAP_YEAR_DIVISOR_4 == 0 && 
            (year % LEAP_YEAR_DIVISOR_100 != 0 || year % LEAP_YEAR_DIVISOR_400 == 0));
}

void calculateDSTDays(int year, int *startDay, int *endDay)
{
    // Validate input parameters
    if (startDay == NULL || endDay == NULL)
    {
        ESP_LOGE("DST", "calculateDSTDays: NULL pointer provided");
        return;
    }
    
    const bool leap = isLeapYear(year);
    const int daysInFeb = leap ? DAYS_IN_FEBRUARY_LEAP : DAYS_IN_FEBRUARY_NORMAL;
    
    // Calculate day-of-week for January 1 using Zeller's congruence
    // For January, we treat it as month 13 of previous year in Zeller's formula
    const int y = year - 1;
    const int m = ZELLER_JANUARY_AS_MONTH_13; // January as month 13 of previous year
    const int q = FIRST_DAY_OF_MONTH;         // day of month (January 1)
    
    // Apply Zeller's congruence formula
    const int century = y / LEAP_YEAR_DIVISOR_100;
    const int year_of_century = y % LEAP_YEAR_DIVISOR_100;
    const int h = (q + ((ZELLER_MONTH_MULTIPLIER * (m + 1)) / ZELLER_CENTURY_MULTIPLIER) + 
                   year_of_century + (year_of_century / LEAP_YEAR_DIVISOR_4) + 
                   (century / LEAP_YEAR_DIVISOR_4) + ZELLER_CENTURY_MULTIPLIER * century) % DAYS_PER_WEEK;
    
    // Zeller's result: h: 0=Saturday, 1=Sunday, 2=Monday, ..., 6=Friday
    // Convert to standard: 0=Sunday, 1=Monday, ..., 6=Saturday
    const int jan1_dow = (h + ZELLER_OFFSET_TO_STANDARD) % DAYS_PER_WEEK;
    
    // ===== Calculate Second Sunday in March =====
    const int march1_doy = DAYS_IN_JANUARY + daysInFeb + FIRST_DAY_OF_MONTH;
    const int march1_dow = (jan1_dow + (march1_doy - 1)) % DAYS_PER_WEEK;
    
    // Days from March 1 until first Sunday
    const int days_to_first_sunday = (march1_dow == DOW_SUNDAY) ? 0 : (DAYS_PER_WEEK - march1_dow);
    
    // Second Sunday is 7 days after first Sunday
    // If March 1 is a Sunday (days_to_first_sunday == 0), then March 1 is the first Sunday
    const int second_sunday_date = FIRST_DAY_OF_MONTH + days_to_first_sunday + DST_SECOND_SUNDAY_OFFSET;
    
    *startDay = march1_doy - 1 + second_sunday_date; // -1 because march1_doy includes March 1
    
    // ===== Calculate First Sunday in November =====
    const int nov1_doy = DAYS_IN_JANUARY + daysInFeb + DAYS_IN_MARCH + DAYS_IN_APRIL + 
                         DAYS_IN_MAY + DAYS_IN_JUNE + DAYS_IN_JULY + DAYS_IN_AUGUST + 
                         DAYS_IN_SEPTEMBER + DAYS_IN_OCTOBER + FIRST_DAY_OF_MONTH;
    const int nov1_dow = (jan1_dow + (nov1_doy - 1)) % DAYS_PER_WEEK;
    
    // Days from November 1 until first Sunday
    const int days_to_first_sunday_nov = (nov1_dow == DOW_SUNDAY) ? 0 : (DAYS_PER_WEEK - nov1_dow);
    
    const int first_sunday_date = FIRST_DAY_OF_MONTH + days_to_first_sunday_nov;
    
    *endDay = nov1_doy - 1 + first_sunday_date; // -1 because nov1_doy includes Nov 1
}

bool isDaylightSavingTime(int year, int daysPassed)
{
    int startDay, endDay;
    calculateDSTDays(year, &startDay, &endDay);
    return (daysPassed >= startDay && daysPassed < endDay);
}
