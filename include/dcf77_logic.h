/*
 * DCF77 Logic Functions
 * Extracted for unit testing
 */
#ifndef DCF77_LOGIC_H
#define DCF77_LOGIC_H

#include <stdint.h>

// DCF77 pulse array constants
#define MaxPulseNumber 183
#define FirstMinutePulseBegin 2
#define SecondMinutePulseBegin 62
#define ThirdMinutePulseBegin 122

// Convert binary to BCD
inline int Bin2Bcd(int dato)
{
    int msb, lsb;

    if (dato < 10)
        return dato;
    msb = (dato / 10) << 4;
    lsb = dato % 10;
    return msb + lsb;
}

// Convert Time library weekday (Sun=1) to DCF77 weekday (Mon=1, Sun=7)
inline int WeekdayToDcf77(int timeLibWeekday)
{
    // Time library: Sunday=1, Monday=2, ..., Saturday=7
    // DCF77: Monday=1, Tuesday=2, ..., Sunday=7
    if (timeLibWeekday == 1) {
        return 7;  // Sunday
    }
    return timeLibWeekday - 1;
}

// Calculate Daylight Saving Time status
// Returns 1 for summer time (CEST), 0 for winter time (CET)
// Parameters: day, month, dayOfWeek (Time library format: Sun=1)
inline int CalculateDls(int day, int month, int dayOfWeek)
{
    int DayToEndOfMonth, DayOfWeekToSunday;

    // Default winter time
    int dls = 0;

    // From April to September we are surely on summer time
    if (month > 3 && month < 10) {
        return 1;
    }

    // March: change winter->summer time on last Sunday
    // March has 31 days, so from 25th onwards we might be in summer time
    if (month == 3 && day >= 25) {
        DayToEndOfMonth = 31 - day;
        DayOfWeekToSunday = 7 - dayOfWeek;
        // If the next Sunday is after month end, we've passed the last Sunday
        if (DayOfWeekToSunday >= DayToEndOfMonth) {
            dls = 1;
        }
        // Also check if today IS Sunday (dayOfWeek == 1)
        if (dayOfWeek == 1) {
            dls = 1;
        }
    }

    // October: change summer->winter time on last Sunday
    // October has 31 days
    if (month == 10) {
        dls = 1;  // Default to summer time in October
        if (day >= 25) {
            DayToEndOfMonth = 31 - day;
            int DayOfWeekToEnd = 7 - dayOfWeek;
            if (DayOfWeekToEnd >= DayToEndOfMonth) {
                dls = 0;
            }
            // If today is Sunday, we're now in winter time
            if (dayOfWeek == 1) {
                dls = 0;
            }
        }
    }

    return dls;
}

// Calculate DCF77 pulse array for one minute
// PulseArray: output array, ArrayOffset: starting position
// ThisMinute, ThisHour, ThisDay, DayOfW, ThisMonth, ThisYear: time components
// Dls: daylight saving flag
inline void CalculateArrayLogic(int* PulseArray, int ArrayOffset,
                                int ThisMinute, int ThisHour, int ThisDay,
                                int DayOfW, int ThisMonth, int ThisYear, int Dls)
{
    int n, Tmp, TmpIn;
    int ParityCount = 0;

    // first 20 bits are logical 0s (100ms pulses)
    for (n = 0; n < 20; n++)
        PulseArray[n + ArrayOffset] = 1;

    // DayLightSaving bits (bits 17 and 18)
    // Bit 17 = CEST (summer), Bit 18 = CET (winter)
    if (Dls == 1)
        PulseArray[17 + ArrayOffset] = 2;
    else
        PulseArray[18 + ArrayOffset] = 2;

    // bit 20 must be 1 to indicate time active
    PulseArray[20 + ArrayOffset] = 2;

    // calculate minutes bits (bits 21-27)
    TmpIn = Bin2Bcd(ThisMinute);
    for (n = 21; n < 28; n++) {
        Tmp = TmpIn & 1;
        PulseArray[n + ArrayOffset] = Tmp + 1;
        ParityCount += Tmp;
        TmpIn >>= 1;
    }
    // minute parity (bit 28)
    if ((ParityCount & 1) == 0)
        PulseArray[28 + ArrayOffset] = 1;
    else
        PulseArray[28 + ArrayOffset] = 2;

    // calculate hour bits (bits 29-34)
    ParityCount = 0;
    TmpIn = Bin2Bcd(ThisHour);
    for (n = 29; n < 35; n++) {
        Tmp = TmpIn & 1;
        PulseArray[n + ArrayOffset] = Tmp + 1;
        ParityCount += Tmp;
        TmpIn >>= 1;
    }
    // hour parity (bit 35)
    if ((ParityCount & 1) == 0)
        PulseArray[35 + ArrayOffset] = 1;
    else
        PulseArray[35 + ArrayOffset] = 2;

    // calculate day bits (bits 36-41)
    ParityCount = 0;
    TmpIn = Bin2Bcd(ThisDay);
    for (n = 36; n < 42; n++) {
        Tmp = TmpIn & 1;
        PulseArray[n + ArrayOffset] = Tmp + 1;
        ParityCount += Tmp;
        TmpIn >>= 1;
    }

    // calculate weekday bits (bits 42-44)
    TmpIn = Bin2Bcd(DayOfW);
    for (n = 42; n < 45; n++) {
        Tmp = TmpIn & 1;
        PulseArray[n + ArrayOffset] = Tmp + 1;
        ParityCount += Tmp;
        TmpIn >>= 1;
    }

    // calculate month bits (bits 45-49)
    TmpIn = Bin2Bcd(ThisMonth);
    for (n = 45; n < 50; n++) {
        Tmp = TmpIn & 1;
        PulseArray[n + ArrayOffset] = Tmp + 1;
        ParityCount += Tmp;
        TmpIn >>= 1;
    }

    // calculate year bits (bits 50-57)
    TmpIn = Bin2Bcd(ThisYear - 2000);
    for (n = 50; n < 58; n++) {
        Tmp = TmpIn & 1;
        PulseArray[n + ArrayOffset] = Tmp + 1;
        ParityCount += Tmp;
        TmpIn >>= 1;
    }

    // date parity (bit 58)
    if ((ParityCount & 1) == 0)
        PulseArray[58 + ArrayOffset] = 1;
    else
        PulseArray[58 + ArrayOffset] = 2;

    // last missing pulse (bit 59) - marks minute boundary
    PulseArray[59 + ArrayOffset] = 0;
}

// Extract value from DCF77 pulse array (for test verification)
// Returns the decoded integer value from the specified bit range
inline int ExtractValueFromArray(int* PulseArray, int ArrayOffset, int startBit, int numBits)
{
    int value = 0;
    for (int i = 0; i < numBits; i++) {
        int bit = (PulseArray[startBit + i + ArrayOffset] == 2) ? 1 : 0;
        value |= (bit << i);
    }
    // Convert from BCD
    int ones = value & 0x0F;
    int tens = (value >> 4) & 0x0F;
    return tens * 10 + ones;
}

#endif // DCF77_LOGIC_H
