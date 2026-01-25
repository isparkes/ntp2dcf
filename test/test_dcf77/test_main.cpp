/*
 * Unit Tests for NTP2DCF
 * Tests the DCF77 encoding logic functions
 */
#include <unity.h>
#include "dcf77_logic.h"

// Test array for DCF77 pulses
int TestPulseArray[MaxPulseNumber];

void setUp(void) {
    // Clear test array before each test
    for (int i = 0; i < MaxPulseNumber; i++) {
        TestPulseArray[i] = 0;
    }
}

void tearDown(void) {
    // Nothing to clean up
}

// =============================================================================
// Bin2Bcd Tests
// =============================================================================

void test_Bin2Bcd_single_digit(void) {
    TEST_ASSERT_EQUAL(0, Bin2Bcd(0));
    TEST_ASSERT_EQUAL(1, Bin2Bcd(1));
    TEST_ASSERT_EQUAL(5, Bin2Bcd(5));
    TEST_ASSERT_EQUAL(9, Bin2Bcd(9));
}

void test_Bin2Bcd_double_digit(void) {
    // 10 = 0001 0000 in BCD = 0x10 = 16
    TEST_ASSERT_EQUAL(0x10, Bin2Bcd(10));
    // 23 = 0010 0011 in BCD = 0x23 = 35
    TEST_ASSERT_EQUAL(0x23, Bin2Bcd(23));
    // 59 = 0101 1001 in BCD = 0x59 = 89
    TEST_ASSERT_EQUAL(0x59, Bin2Bcd(59));
    // 99 = 1001 1001 in BCD = 0x99 = 153
    TEST_ASSERT_EQUAL(0x99, Bin2Bcd(99));
}

void test_Bin2Bcd_boundary_values(void) {
    // Minutes: 0-59
    TEST_ASSERT_EQUAL(0x00, Bin2Bcd(0));
    TEST_ASSERT_EQUAL(0x59, Bin2Bcd(59));

    // Hours: 0-23
    TEST_ASSERT_EQUAL(0x23, Bin2Bcd(23));

    // Days: 1-31
    TEST_ASSERT_EQUAL(0x01, Bin2Bcd(1));
    TEST_ASSERT_EQUAL(0x31, Bin2Bcd(31));

    // Months: 1-12
    TEST_ASSERT_EQUAL(0x12, Bin2Bcd(12));
}

// =============================================================================
// Weekday Conversion Tests
// =============================================================================

void test_WeekdayToDcf77_conversion(void) {
    // Time library: Sun=1, Mon=2, Tue=3, Wed=4, Thu=5, Fri=6, Sat=7
    // DCF77:        Mon=1, Tue=2, Wed=3, Thu=4, Fri=5, Sat=6, Sun=7

    TEST_ASSERT_EQUAL(7, WeekdayToDcf77(1));  // Sunday
    TEST_ASSERT_EQUAL(1, WeekdayToDcf77(2));  // Monday
    TEST_ASSERT_EQUAL(2, WeekdayToDcf77(3));  // Tuesday
    TEST_ASSERT_EQUAL(3, WeekdayToDcf77(4));  // Wednesday
    TEST_ASSERT_EQUAL(4, WeekdayToDcf77(5));  // Thursday
    TEST_ASSERT_EQUAL(5, WeekdayToDcf77(6));  // Friday
    TEST_ASSERT_EQUAL(6, WeekdayToDcf77(7));  // Saturday
}

// This test demonstrates the BUG in the original code
void test_WeekdayBug_original_code_is_wrong(void) {
    // The original code passes weekday() directly to Bin2Bcd
    // This is WRONG because weekday() uses Sun=1 but DCF77 uses Mon=1

    // Example: If it's Monday, weekday() returns 2
    // Original code would encode 2, but DCF77 expects 1 for Monday

    int timeLibMonday = 2;  // Time library value for Monday
    int dcf77Monday = 1;    // DCF77 value for Monday

    // This shows the bug: direct use is wrong
    TEST_ASSERT_NOT_EQUAL(dcf77Monday, timeLibMonday);

    // This is the correct conversion
    TEST_ASSERT_EQUAL(dcf77Monday, WeekdayToDcf77(timeLibMonday));
}

// =============================================================================
// Daylight Saving Time Tests
// =============================================================================

void test_CalculateDls_winter_months(void) {
    // January, February, November, December are always winter time
    TEST_ASSERT_EQUAL(0, CalculateDls(15, 1, 3));   // Jan 15
    TEST_ASSERT_EQUAL(0, CalculateDls(15, 2, 4));   // Feb 15
    TEST_ASSERT_EQUAL(0, CalculateDls(15, 11, 5));  // Nov 15
    TEST_ASSERT_EQUAL(0, CalculateDls(15, 12, 6));  // Dec 15
}

void test_CalculateDls_summer_months(void) {
    // April through September are always summer time
    TEST_ASSERT_EQUAL(1, CalculateDls(15, 4, 2));   // Apr 15
    TEST_ASSERT_EQUAL(1, CalculateDls(15, 5, 3));   // May 15
    TEST_ASSERT_EQUAL(1, CalculateDls(15, 6, 4));   // Jun 15
    TEST_ASSERT_EQUAL(1, CalculateDls(15, 7, 5));   // Jul 15
    TEST_ASSERT_EQUAL(1, CalculateDls(15, 8, 6));   // Aug 15
    TEST_ASSERT_EQUAL(1, CalculateDls(15, 9, 7));   // Sep 15
}

void test_CalculateDls_march_before_last_sunday(void) {
    // March days before 25th are always winter time
    TEST_ASSERT_EQUAL(0, CalculateDls(1, 3, 2));    // Mar 1
    TEST_ASSERT_EQUAL(0, CalculateDls(15, 3, 3));   // Mar 15
    TEST_ASSERT_EQUAL(0, CalculateDls(24, 3, 4));   // Mar 24
}

void test_CalculateDls_march_last_sunday_transition(void) {
    // March 31, 2024 was a Sunday - DST starts
    // If March 31 is Sunday (dayOfWeek=1), should be summer time
    TEST_ASSERT_EQUAL(1, CalculateDls(31, 3, 1));   // Mar 31, Sunday

    // March 30, 2024 was Saturday - still winter
    TEST_ASSERT_EQUAL(0, CalculateDls(30, 3, 7));   // Mar 30, Saturday

    // March 25 on a Sunday = summer time
    TEST_ASSERT_EQUAL(1, CalculateDls(25, 3, 1));   // Mar 25, Sunday

    // March 25 on a Monday = still winter (last Sunday hasn't happened)
    TEST_ASSERT_EQUAL(0, CalculateDls(25, 3, 2));   // Mar 25, Monday
}

void test_CalculateDls_october_before_last_sunday(void) {
    // October days before 25th are always summer time
    TEST_ASSERT_EQUAL(1, CalculateDls(1, 10, 2));   // Oct 1
    TEST_ASSERT_EQUAL(1, CalculateDls(15, 10, 3));  // Oct 15
    TEST_ASSERT_EQUAL(1, CalculateDls(24, 10, 4));  // Oct 24
}

void test_CalculateDls_october_last_sunday_transition(void) {
    // October 27, 2024 was a Sunday - DST ends
    // If October 27 is Sunday (dayOfWeek=1), should be winter time
    TEST_ASSERT_EQUAL(0, CalculateDls(27, 10, 1));  // Oct 27, Sunday

    // October 26, 2024 was Saturday - still summer
    TEST_ASSERT_EQUAL(1, CalculateDls(26, 10, 7)); // Oct 26, Saturday

    // October 31 on a Thursday after last Sunday = winter
    TEST_ASSERT_EQUAL(0, CalculateDls(31, 10, 5)); // Oct 31, Thursday
}

// =============================================================================
// DCF77 Array Calculation Tests
// =============================================================================

void test_CalculateArray_minute_encoding(void) {
    // Test encoding minute 30 at 12:30 on Jan 15, 2024 (Monday)
    // Using DCF77 weekday (Monday = 1)
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        30, 12, 15, 1, 1, 2024, 0);

    // Verify minute bits (21-27): 30 in BCD = 0011 0000
    // Bit 21 (LSB) = 0, Bit 22 = 0, Bit 23 = 0, Bit 24 = 0
    // Bit 25 = 1, Bit 26 = 1, Bit 27 = 0
    int minute = ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 21, 7);
    TEST_ASSERT_EQUAL(30, minute);
}

void test_CalculateArray_hour_encoding(void) {
    // Test encoding hour 23 at 23:45 on Jan 15, 2024
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        45, 23, 15, 1, 1, 2024, 0);

    // Verify hour bits (29-34): 23 in BCD = 0010 0011
    int hour = ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 29, 6);
    TEST_ASSERT_EQUAL(23, hour);
}

void test_CalculateArray_day_encoding(void) {
    // Test encoding day 31 at 10:00 on Jan 31, 2024
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 10, 31, 3, 1, 2024, 0);

    // Verify day bits (36-41): 31 in BCD = 0011 0001
    int day = ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 36, 6);
    TEST_ASSERT_EQUAL(31, day);
}

void test_CalculateArray_weekday_encoding(void) {
    // Test encoding weekday 7 (Sunday in DCF77) at 10:00
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 10, 15, 7, 1, 2024, 0);

    // Verify weekday bits (42-44): 7 in BCD = 0111
    int weekday = ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 42, 3);
    TEST_ASSERT_EQUAL(7, weekday);
}

void test_CalculateArray_month_encoding(void) {
    // Test encoding month 12 at 10:00 on Dec 15, 2024
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 10, 15, 1, 12, 2024, 0);

    // Verify month bits (45-49): 12 in BCD = 0001 0010
    int month = ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 45, 5);
    TEST_ASSERT_EQUAL(12, month);
}

void test_CalculateArray_year_encoding(void) {
    // Test encoding year 2024 at 10:00 on Jan 15, 2024
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 10, 15, 1, 1, 2024, 0);

    // Verify year bits (50-57): 24 in BCD = 0010 0100
    int year = ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 50, 8);
    TEST_ASSERT_EQUAL(24, year);
}

void test_CalculateArray_dst_bits_summer(void) {
    // Test DST bits for summer time (Dls = 1)
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 10, 15, 1, 6, 2024, 1);

    // Bit 17 should be 2 (200ms pulse = 1) for summer time
    // Bit 18 should be 1 (100ms pulse = 0) for summer time
    TEST_ASSERT_EQUAL(2, TestPulseArray[17 + FirstMinutePulseBegin]);
    TEST_ASSERT_EQUAL(1, TestPulseArray[18 + FirstMinutePulseBegin]);
}

void test_CalculateArray_dst_bits_winter(void) {
    // Test DST bits for winter time (Dls = 0)
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 10, 15, 1, 1, 2024, 0);

    // Bit 17 should be 1 (100ms pulse = 0) for winter time
    // Bit 18 should be 2 (200ms pulse = 1) for winter time
    TEST_ASSERT_EQUAL(1, TestPulseArray[17 + FirstMinutePulseBegin]);
    TEST_ASSERT_EQUAL(2, TestPulseArray[18 + FirstMinutePulseBegin]);
}

void test_CalculateArray_start_of_time_bit(void) {
    // Bit 20 must always be 1 (200ms pulse) to indicate time is active
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 10, 15, 1, 1, 2024, 0);

    TEST_ASSERT_EQUAL(2, TestPulseArray[20 + FirstMinutePulseBegin]);
}

void test_CalculateArray_minute_marker(void) {
    // Bit 59 must be 0 (no pulse) to mark minute boundary
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 10, 15, 1, 1, 2024, 0);

    TEST_ASSERT_EQUAL(0, TestPulseArray[59 + FirstMinutePulseBegin]);
}

void test_CalculateArray_minute_parity(void) {
    // Test parity for minute 37 (binary: 0011 0111, 5 ones = odd parity)
    // 37 BCD = 0x37 = 0011 0111, bits: 1,1,1,0,1,1,0 = 5 ones
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        37, 10, 15, 1, 1, 2024, 0);

    // Count ones in minute encoding
    int ones = 0;
    for (int i = 21; i < 28; i++) {
        if (TestPulseArray[i + FirstMinutePulseBegin] == 2) ones++;
    }

    // Parity bit (28) should make total even
    int parityBit = (TestPulseArray[28 + FirstMinutePulseBegin] == 2) ? 1 : 0;
    TEST_ASSERT_EQUAL(0, (ones + parityBit) % 2);  // Total should be even
}

void test_CalculateArray_full_time_encoding(void) {
    // Test complete time: 14:35 on Tuesday, July 23, 2024
    // Tuesday in DCF77 = 2
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        35, 14, 23, 2, 7, 2024, 1);

    TEST_ASSERT_EQUAL(35, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 21, 7));
    TEST_ASSERT_EQUAL(14, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 29, 6));
    TEST_ASSERT_EQUAL(23, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 36, 6));
    TEST_ASSERT_EQUAL(2, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 42, 3));
    TEST_ASSERT_EQUAL(7, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 45, 5));
    TEST_ASSERT_EQUAL(24, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 50, 8));
}

// =============================================================================
// Edge Case Tests
// =============================================================================

void test_CalculateArray_midnight(void) {
    // Test midnight: 00:00
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 0, 1, 1, 1, 2024, 0);

    TEST_ASSERT_EQUAL(0, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 21, 7));
    TEST_ASSERT_EQUAL(0, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 29, 6));
}

void test_CalculateArray_end_of_day(void) {
    // Test 23:59
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        59, 23, 1, 1, 1, 2024, 0);

    TEST_ASSERT_EQUAL(59, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 21, 7));
    TEST_ASSERT_EQUAL(23, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 29, 6));
}

void test_CalculateArray_leap_year_feb29(void) {
    // Test February 29, 2024 (leap year)
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin,
                        0, 12, 29, 4, 2, 2024, 0);

    TEST_ASSERT_EQUAL(29, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 36, 6));
    TEST_ASSERT_EQUAL(2, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 45, 5));
}

void test_CalculateArray_different_offsets(void) {
    // Test that different offsets work correctly
    CalculateArrayLogic(TestPulseArray, FirstMinutePulseBegin, 15, 10, 1, 1, 1, 2024, 0);
    CalculateArrayLogic(TestPulseArray, SecondMinutePulseBegin, 16, 10, 1, 1, 1, 2024, 0);
    CalculateArrayLogic(TestPulseArray, ThirdMinutePulseBegin, 17, 10, 1, 1, 1, 2024, 0);

    TEST_ASSERT_EQUAL(15, ExtractValueFromArray(TestPulseArray, FirstMinutePulseBegin, 21, 7));
    TEST_ASSERT_EQUAL(16, ExtractValueFromArray(TestPulseArray, SecondMinutePulseBegin, 21, 7));
    TEST_ASSERT_EQUAL(17, ExtractValueFromArray(TestPulseArray, ThirdMinutePulseBegin, 21, 7));
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Bin2Bcd tests
    RUN_TEST(test_Bin2Bcd_single_digit);
    RUN_TEST(test_Bin2Bcd_double_digit);
    RUN_TEST(test_Bin2Bcd_boundary_values);

    // Weekday conversion tests
    RUN_TEST(test_WeekdayToDcf77_conversion);
    RUN_TEST(test_WeekdayBug_original_code_is_wrong);

    // DST tests
    RUN_TEST(test_CalculateDls_winter_months);
    RUN_TEST(test_CalculateDls_summer_months);
    RUN_TEST(test_CalculateDls_march_before_last_sunday);
    RUN_TEST(test_CalculateDls_march_last_sunday_transition);
    RUN_TEST(test_CalculateDls_october_before_last_sunday);
    RUN_TEST(test_CalculateDls_october_last_sunday_transition);

    // DCF77 array tests
    RUN_TEST(test_CalculateArray_minute_encoding);
    RUN_TEST(test_CalculateArray_hour_encoding);
    RUN_TEST(test_CalculateArray_day_encoding);
    RUN_TEST(test_CalculateArray_weekday_encoding);
    RUN_TEST(test_CalculateArray_month_encoding);
    RUN_TEST(test_CalculateArray_year_encoding);
    RUN_TEST(test_CalculateArray_dst_bits_summer);
    RUN_TEST(test_CalculateArray_dst_bits_winter);
    RUN_TEST(test_CalculateArray_start_of_time_bit);
    RUN_TEST(test_CalculateArray_minute_marker);
    RUN_TEST(test_CalculateArray_minute_parity);
    RUN_TEST(test_CalculateArray_full_time_encoding);

    // Edge case tests
    RUN_TEST(test_CalculateArray_midnight);
    RUN_TEST(test_CalculateArray_end_of_day);
    RUN_TEST(test_CalculateArray_leap_year_feb29);
    RUN_TEST(test_CalculateArray_different_offsets);

    return UNITY_END();
}
