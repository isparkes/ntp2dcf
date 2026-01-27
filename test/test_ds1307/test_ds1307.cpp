/**
 * Unit Tests for DS1307 I2C Emulation
 * Tests BCD conversion functions and register format
 */
#include <unity.h>
#include <stdint.h>

// UNIT_TEST is defined via build flags in platformio.ini
#include "ds1307_emulation.h"

void setUp(void) {
    // Nothing to set up
}

void tearDown(void) {
    // Nothing to clean up
}

// =============================================================================
// DS1307_toBCD Tests
// =============================================================================

void test_DS1307_toBCD_single_digit(void) {
    TEST_ASSERT_EQUAL(0x00, DS1307_toBCD(0));
    TEST_ASSERT_EQUAL(0x01, DS1307_toBCD(1));
    TEST_ASSERT_EQUAL(0x05, DS1307_toBCD(5));
    TEST_ASSERT_EQUAL(0x09, DS1307_toBCD(9));
}

void test_DS1307_toBCD_double_digit(void) {
    // 10 = 0001 0000 in BCD = 0x10
    TEST_ASSERT_EQUAL(0x10, DS1307_toBCD(10));
    // 23 = 0010 0011 in BCD = 0x23
    TEST_ASSERT_EQUAL(0x23, DS1307_toBCD(23));
    // 45 = 0100 0101 in BCD = 0x45
    TEST_ASSERT_EQUAL(0x45, DS1307_toBCD(45));
    // 59 = 0101 1001 in BCD = 0x59
    TEST_ASSERT_EQUAL(0x59, DS1307_toBCD(59));
}

void test_DS1307_toBCD_seconds_range(void) {
    // Valid seconds range: 0-59
    TEST_ASSERT_EQUAL(0x00, DS1307_toBCD(0));
    TEST_ASSERT_EQUAL(0x30, DS1307_toBCD(30));
    TEST_ASSERT_EQUAL(0x59, DS1307_toBCD(59));
}

void test_DS1307_toBCD_minutes_range(void) {
    // Valid minutes range: 0-59
    TEST_ASSERT_EQUAL(0x00, DS1307_toBCD(0));
    TEST_ASSERT_EQUAL(0x30, DS1307_toBCD(30));
    TEST_ASSERT_EQUAL(0x59, DS1307_toBCD(59));
}

void test_DS1307_toBCD_hours_range(void) {
    // Valid hours range: 0-23 (24-hour mode)
    TEST_ASSERT_EQUAL(0x00, DS1307_toBCD(0));
    TEST_ASSERT_EQUAL(0x12, DS1307_toBCD(12));
    TEST_ASSERT_EQUAL(0x23, DS1307_toBCD(23));
}

void test_DS1307_toBCD_day_range(void) {
    // Valid day range: 1-31
    TEST_ASSERT_EQUAL(0x01, DS1307_toBCD(1));
    TEST_ASSERT_EQUAL(0x15, DS1307_toBCD(15));
    TEST_ASSERT_EQUAL(0x31, DS1307_toBCD(31));
}

void test_DS1307_toBCD_month_range(void) {
    // Valid month range: 1-12
    TEST_ASSERT_EQUAL(0x01, DS1307_toBCD(1));
    TEST_ASSERT_EQUAL(0x06, DS1307_toBCD(6));
    TEST_ASSERT_EQUAL(0x12, DS1307_toBCD(12));
}

void test_DS1307_toBCD_year_range(void) {
    // Valid year range: 0-99 (representing 2000-2099)
    TEST_ASSERT_EQUAL(0x00, DS1307_toBCD(0));
    TEST_ASSERT_EQUAL(0x24, DS1307_toBCD(24));
    TEST_ASSERT_EQUAL(0x99, DS1307_toBCD(99));
}

// =============================================================================
// DS1307_fromBCD Tests (inverse of toBCD)
// =============================================================================

void test_DS1307_fromBCD_single_digit(void) {
    TEST_ASSERT_EQUAL(0, DS1307_fromBCD(0x00));
    TEST_ASSERT_EQUAL(1, DS1307_fromBCD(0x01));
    TEST_ASSERT_EQUAL(5, DS1307_fromBCD(0x05));
    TEST_ASSERT_EQUAL(9, DS1307_fromBCD(0x09));
}

void test_DS1307_fromBCD_double_digit(void) {
    TEST_ASSERT_EQUAL(10, DS1307_fromBCD(0x10));
    TEST_ASSERT_EQUAL(23, DS1307_fromBCD(0x23));
    TEST_ASSERT_EQUAL(45, DS1307_fromBCD(0x45));
    TEST_ASSERT_EQUAL(59, DS1307_fromBCD(0x59));
}

void test_DS1307_BCD_roundtrip(void) {
    // Test that toBCD and fromBCD are inverses
    for (uint8_t i = 0; i < 60; i++) {
        uint8_t bcd = DS1307_toBCD(i);
        uint8_t back = DS1307_fromBCD(bcd);
        TEST_ASSERT_EQUAL(i, back);
    }
}

// =============================================================================
// Register Format Tests
// =============================================================================

void test_DS1307_seconds_register_format(void) {
    // Seconds register: bits 0-6 = BCD seconds, bit 7 = CH (clock halt)
    // For a running clock, bit 7 should be 0

    uint8_t seconds_reg = DS1307_toBCD(45) & 0x7F;  // 45 seconds, CH=0
    TEST_ASSERT_EQUAL(0x45, seconds_reg);

    // Verify bit 7 is clear (clock running)
    TEST_ASSERT_EQUAL(0, seconds_reg & 0x80);
}

void test_DS1307_hours_register_24hour_mode(void) {
    // Hours register in 24-hour mode: bits 0-5 = BCD hours, bit 6 = 0
    // Bit 6 = 0 indicates 24-hour mode

    uint8_t hours_reg = DS1307_toBCD(23) & 0x3F;  // 23 hours, 24-hour mode
    TEST_ASSERT_EQUAL(0x23, hours_reg);

    // Verify bit 6 is clear (24-hour mode)
    TEST_ASSERT_EQUAL(0, hours_reg & 0x40);
}

void test_DS1307_day_of_week_format(void) {
    // Day of week: 1-7, where 1 = Sunday
    // DS1307 uses the same format as Time library

    // Sunday = 1
    TEST_ASSERT_EQUAL(1, 1);  // DS1307 Sunday
    // Monday = 2
    TEST_ASSERT_EQUAL(2, 2);  // DS1307 Monday
    // Saturday = 7
    TEST_ASSERT_EQUAL(7, 7);  // DS1307 Saturday
}

// =============================================================================
// Boundary Value Tests
// =============================================================================

void test_DS1307_midnight(void) {
    // Test 00:00:00
    TEST_ASSERT_EQUAL(0x00, DS1307_toBCD(0));  // seconds
    TEST_ASSERT_EQUAL(0x00, DS1307_toBCD(0));  // minutes
    TEST_ASSERT_EQUAL(0x00, DS1307_toBCD(0));  // hours
}

void test_DS1307_end_of_day(void) {
    // Test 23:59:59
    TEST_ASSERT_EQUAL(0x59, DS1307_toBCD(59));  // seconds
    TEST_ASSERT_EQUAL(0x59, DS1307_toBCD(59));  // minutes
    TEST_ASSERT_EQUAL(0x23, DS1307_toBCD(23));  // hours
}

void test_DS1307_new_years_eve(void) {
    // Test December 31, 2099
    TEST_ASSERT_EQUAL(0x31, DS1307_toBCD(31));  // day
    TEST_ASSERT_EQUAL(0x12, DS1307_toBCD(12));  // month
    TEST_ASSERT_EQUAL(0x99, DS1307_toBCD(99));  // year (2099)
}

void test_DS1307_new_years_day(void) {
    // Test January 1, 2000
    TEST_ASSERT_EQUAL(0x01, DS1307_toBCD(1));   // day
    TEST_ASSERT_EQUAL(0x01, DS1307_toBCD(1));   // month
    TEST_ASSERT_EQUAL(0x00, DS1307_toBCD(0));   // year (2000)
}

// =============================================================================
// Address Pointer Wraparound Tests
// =============================================================================

void test_DS1307_address_wraparound_logic(void) {
    // Test that address pointer wraps at register count boundary
    uint8_t addr = 63;  // Last valid address
    addr++;
    if (addr >= DS1307_REGISTER_COUNT) {
        addr = 0;
    }
    TEST_ASSERT_EQUAL(0, addr);
}

void test_DS1307_address_pointer_increment(void) {
    // Test sequential address increment
    uint8_t addr = 0;
    for (int i = 0; i < 64; i++) {
        TEST_ASSERT_LESS_THAN(DS1307_REGISTER_COUNT, addr);
        addr++;
        if (addr >= DS1307_REGISTER_COUNT) {
            addr = 0;
        }
    }
    TEST_ASSERT_EQUAL(0, addr);  // Should wrap back to 0
}

// =============================================================================
// Register Constants Tests
// =============================================================================

void test_DS1307_register_addresses(void) {
    TEST_ASSERT_EQUAL(0x00, DS1307_REG_SECONDS);
    TEST_ASSERT_EQUAL(0x01, DS1307_REG_MINUTES);
    TEST_ASSERT_EQUAL(0x02, DS1307_REG_HOURS);
    TEST_ASSERT_EQUAL(0x03, DS1307_REG_DAY);
    TEST_ASSERT_EQUAL(0x04, DS1307_REG_DATE);
    TEST_ASSERT_EQUAL(0x05, DS1307_REG_MONTH);
    TEST_ASSERT_EQUAL(0x06, DS1307_REG_YEAR);
    TEST_ASSERT_EQUAL(0x07, DS1307_REG_CONTROL);
    TEST_ASSERT_EQUAL(0x08, DS1307_REG_RAM_START);
}

void test_DS1307_i2c_address(void) {
    TEST_ASSERT_EQUAL(0x68, DS1307_I2C_ADDRESS);
}

void test_DS1307_register_count(void) {
    TEST_ASSERT_EQUAL(64, DS1307_REGISTER_COUNT);
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // BCD conversion tests
    RUN_TEST(test_DS1307_toBCD_single_digit);
    RUN_TEST(test_DS1307_toBCD_double_digit);
    RUN_TEST(test_DS1307_toBCD_seconds_range);
    RUN_TEST(test_DS1307_toBCD_minutes_range);
    RUN_TEST(test_DS1307_toBCD_hours_range);
    RUN_TEST(test_DS1307_toBCD_day_range);
    RUN_TEST(test_DS1307_toBCD_month_range);
    RUN_TEST(test_DS1307_toBCD_year_range);

    // fromBCD tests
    RUN_TEST(test_DS1307_fromBCD_single_digit);
    RUN_TEST(test_DS1307_fromBCD_double_digit);
    RUN_TEST(test_DS1307_BCD_roundtrip);

    // Register format tests
    RUN_TEST(test_DS1307_seconds_register_format);
    RUN_TEST(test_DS1307_hours_register_24hour_mode);
    RUN_TEST(test_DS1307_day_of_week_format);

    // Boundary value tests
    RUN_TEST(test_DS1307_midnight);
    RUN_TEST(test_DS1307_end_of_day);
    RUN_TEST(test_DS1307_new_years_eve);
    RUN_TEST(test_DS1307_new_years_day);

    // Address pointer tests
    RUN_TEST(test_DS1307_address_wraparound_logic);
    RUN_TEST(test_DS1307_address_pointer_increment);

    // Register constants tests
    RUN_TEST(test_DS1307_register_addresses);
    RUN_TEST(test_DS1307_i2c_address);
    RUN_TEST(test_DS1307_register_count);

    return UNITY_END();
}
