/*
 * DST (Daylight Saving Time) Calculation Module
 */

#include "dst_calc.h"
#include <esp_log.h>

bool isLeapYear(int year)
{
    if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
    {
        return true;
    }
    return false;
}

void calculateDSTDays(int year, int *startDay, int *endDay)
{
    // Validate input parameters
    if (startDay == NULL || endDay == NULL)
    {
        ESP_LOGE("WWVB", "calculateDSTDays: NULL pointer provided");
        return;
    }
    
    bool leap = isLeapYear(year);
    int daysInFeb = leap ? 29 : 28;
    
    // Calculate day-of-week for January 1 using Zeller's congruence
    // For January, we treat it as month 13 of previous year in Zeller's formula
    int y = year - 1;
    int m = 13; // January as month 13 of previous year
    int q = 1;  // day of month (January 1)
    
    // Apply Zeller's congruence formula
    int century = y / 100;
    int year_of_century = y % 100;
    int h = (q + ((13 * (m + 1)) / 5) + year_of_century + (year_of_century / 4) + (century / 4) + 5 * century) % 7;
    
    // Zeller's result: h: 0=Saturday, 1=Sunday, 2=Monday, ..., 6=Friday
    // Convert to standard: 0=Sunday, 1=Monday, ..., 6=Saturday
    int jan1_dow = (h + 6) % 7;
    
    // ===== Calculate Second Sunday in March =====
    int march1_doy = 31 + daysInFeb + 1; // Jan(31) + Feb(28/29) + Mar(1) for March 1
    int march1_dow = (jan1_dow + (march1_doy - 1)) % 7;
    
    // Days from March 1 until first Sunday
    int days_to_first_sunday = (march1_dow == 0) ? 0 : (7 - march1_dow);
    
    // Second Sunday is 7 days after first Sunday
    // If March 1 is a Sunday (days_to_first_sunday == 0), then March 1 is the first Sunday
    int second_sunday_date = 1 + days_to_first_sunday + 7;
    
    *startDay = march1_doy - 1 + second_sunday_date; // -1 because march1_doy includes March 1
    
    // ===== Calculate First Sunday in November =====
    int nov1_doy = 31 + daysInFeb + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 1; // Jan(31) + Feb(28/29) + Mar(31) + Apr(30) + May(31) + Jun(30) + Jul(31) + Aug(31) + Sep(30) + Oct(31) + Nov(1) for Nov 1
    int nov1_dow = (jan1_dow + (nov1_doy - 1)) % 7;
    
    // Days from November 1 until first Sunday
    int days_to_first_sunday_nov = (nov1_dow == 0) ? 0 : (7 - nov1_dow);
    
    int first_sunday_date = 1 + days_to_first_sunday_nov;
    
    *endDay = nov1_doy - 1 + first_sunday_date; // -1 because nov1_doy includes Nov 1
}

bool isDaylightSavingTime(int year, int daysPassed)
{
    int startDay, endDay;
    calculateDSTDays(year, &startDay, &endDay);
    return (daysPassed >= startDay && daysPassed < endDay);
}
