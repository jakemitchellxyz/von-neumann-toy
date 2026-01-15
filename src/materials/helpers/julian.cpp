// ============================================================================
// Julian Date to Month Conversion
// ============================================================================

#include "../earth/earth-material.h"
#include <algorithm>
#include <cmath>

namespace
{
// Constants from Jean Meeus "Astronomical Algorithms"
constexpr double JULIAN_DATE_OFFSET = 0.5;
constexpr double GREGORIAN_CALENDAR_START_JD = 2299161.0;
constexpr double GREGORIAN_CALENDAR_BASE_JD = 1867216.25;
constexpr double DAYS_PER_CENTURY = 36524.25;
constexpr double JULIAN_TO_GREGORIAN_OFFSET = 1524.0;
constexpr double MONTH_CALCULATION_OFFSET = 122.1;
constexpr double DAYS_PER_YEAR = 365.25;
constexpr double DAYS_PER_MONTH_APPROX = 30.6001;
constexpr int MONTH_INDEX_THRESHOLD = 14;
constexpr int MONTH_OFFSET_EARLY = 1;
constexpr int MONTH_OFFSET_LATE = 13;
constexpr int MIN_MONTH = 1;
constexpr int MAX_MONTH = 12;
constexpr int QUARTER_YEAR_DIVISOR = 4;
} // namespace

int EarthMaterial::getMonthFromJulianDate(double julianDate)
{
    // Algorithm from Jean Meeus "Astronomical Algorithms"
    double julianDayInteger = std::floor(julianDate + JULIAN_DATE_OFFSET);
    double fractionalDay = (julianDate + JULIAN_DATE_OFFSET) - julianDayInteger;
    (void)fractionalDay; // Unused but part of algorithm

    double adjustedJulianDay = julianDayInteger;
    if (julianDayInteger >= GREGORIAN_CALENDAR_START_JD)
    {
        double gregorianCorrection = std::floor((julianDayInteger - GREGORIAN_CALENDAR_BASE_JD) / DAYS_PER_CENTURY);
        adjustedJulianDay = julianDayInteger + MONTH_OFFSET_EARLY + gregorianCorrection -
                            std::floor(gregorianCorrection / QUARTER_YEAR_DIVISOR);
    }

    double intermediateValue = adjustedJulianDay + JULIAN_TO_GREGORIAN_OFFSET;
    double centuryValue = std::floor((intermediateValue - MONTH_CALCULATION_OFFSET) / DAYS_PER_YEAR);
    double daysInCentury = std::floor(DAYS_PER_YEAR * centuryValue);
    double monthIndex = std::floor((intermediateValue - daysInCentury) / DAYS_PER_MONTH_APPROX);

    int month = MIN_MONTH;
    if (monthIndex < MONTH_INDEX_THRESHOLD)
    {
        month = static_cast<int>(monthIndex) - MONTH_OFFSET_EARLY;
    }
    else
    {
        month = static_cast<int>(monthIndex) - MONTH_OFFSET_LATE;
    }

    month = std::max(MIN_MONTH, month);
    month = std::min(MAX_MONTH, month);

    return month;
}
