/**
 * Unit Tests for Configuration Storage
 * Tests checksum calculation and default values
 */
#include <unity.h>
#include <stdint.h>
#include <string.h>

// UNIT_TEST is defined via build flags in platformio.ini
#include "config_storage.h"

void setUp(void) {
    // Nothing to set up
}

void tearDown(void) {
    // Nothing to clean up
}

// =============================================================================
// Configuration Structure Tests
// =============================================================================

void test_config_struct_size(void) {
    // Verify struct size is reasonable for EEPROM storage
    // Should be well under 256 bytes
    TEST_ASSERT_LESS_THAN(256, sizeof(NTP2DCFConfig));
}

void test_config_magic_value(void) {
    TEST_ASSERT_EQUAL(0xDC, CONFIG_MAGIC);
}

void test_config_version(void) {
    TEST_ASSERT_EQUAL(1, CONFIG_VERSION);
}

void test_config_field_sizes(void) {
    TEST_ASSERT_EQUAL(64, NTP_SERVER_MAX_LEN);
    TEST_ASSERT_EQUAL(48, TZ_POSIX_MAX_LEN);
}

// =============================================================================
// Default Value Tests
// =============================================================================

void test_default_ntp_server(void) {
    TEST_ASSERT_EQUAL_STRING("0.de.pool.ntp.org", DEFAULT_NTP_SERVER);
}

void test_default_timezone(void) {
    // CET-1CEST,M3.5.0,M10.5.0/3 is the POSIX TZ string for Central European Time
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", DEFAULT_TZ_POSIX);
}

void test_default_ntp_interval(void) {
    TEST_ASSERT_EQUAL(60, DEFAULT_NTP_INTERVAL);
}

// =============================================================================
// POSIX Timezone String Format Tests
// =============================================================================

void test_posix_tz_string_length(void) {
    // Verify default TZ string fits in buffer
    TEST_ASSERT_LESS_THAN(TZ_POSIX_MAX_LEN, strlen(DEFAULT_TZ_POSIX) + 1);
}

void test_ntp_server_length(void) {
    // Verify default NTP server fits in buffer
    TEST_ASSERT_LESS_THAN(NTP_SERVER_MAX_LEN, strlen(DEFAULT_NTP_SERVER) + 1);
}

// =============================================================================
// Interval Validation Tests
// =============================================================================

void test_min_ntp_interval(void) {
    // Minimum interval should be 60 seconds (1 minute)
    TEST_ASSERT_GREATER_OR_EQUAL(60, DEFAULT_NTP_INTERVAL);
}

void test_max_ntp_interval_reasonable(void) {
    // Maximum should not exceed 3600 seconds (1 hour)
    TEST_ASSERT_LESS_OR_EQUAL(3600, DEFAULT_NTP_INTERVAL);
}

// =============================================================================
// EEPROM Size Tests
// =============================================================================

void test_eeprom_size(void) {
    TEST_ASSERT_EQUAL(256, EEPROM_SIZE);
}

void test_config_fits_in_eeprom(void) {
    TEST_ASSERT_LESS_OR_EQUAL(EEPROM_SIZE, sizeof(NTP2DCFConfig));
}

// =============================================================================
// Common Timezone String Examples (validation tests)
// =============================================================================

void test_timezone_examples_fit_buffer(void) {
    // Common timezone strings that users might configure
    const char* tz_examples[] = {
        "CET-1CEST,M3.5.0,M10.5.0/3",    // Central Europe
        "GMT0BST,M3.5.0/1,M10.5.0",       // UK
        "EST5EDT,M3.2.0,M11.1.0",         // US Eastern
        "PST8PDT,M3.2.0,M11.1.0",         // US Pacific
        "JST-9",                           // Japan (no DST)
        "AEST-10AEDT,M10.1.0,M4.1.0/3",  // Australia Eastern
        "MSK-3",                           // Moscow (no DST)
        "IST-5:30",                        // India
        "CST-8",                           // China
        "NZST-12NZDT,M9.5.0,M4.1.0/3"    // New Zealand
    };

    int num_examples = sizeof(tz_examples) / sizeof(tz_examples[0]);

    for (int i = 0; i < num_examples; i++) {
        TEST_ASSERT_LESS_THAN_MESSAGE(
            TZ_POSIX_MAX_LEN,
            strlen(tz_examples[i]) + 1,
            tz_examples[i]
        );
    }
}

// =============================================================================
// NTP Server Examples
// =============================================================================

void test_ntp_server_examples_fit_buffer(void) {
    // Common NTP server hostnames
    const char* ntp_examples[] = {
        "pool.ntp.org",
        "time.google.com",
        "time.cloudflare.com",
        "0.de.pool.ntp.org",
        "1.europe.pool.ntp.org",
        "time.windows.com",
        "time.apple.com",
        "ntp.ubuntu.com"
    };

    int num_examples = sizeof(ntp_examples) / sizeof(ntp_examples[0]);

    for (int i = 0; i < num_examples; i++) {
        TEST_ASSERT_LESS_THAN_MESSAGE(
            NTP_SERVER_MAX_LEN,
            strlen(ntp_examples[i]) + 1,
            ntp_examples[i]
        );
    }
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Structure tests
    RUN_TEST(test_config_struct_size);
    RUN_TEST(test_config_magic_value);
    RUN_TEST(test_config_version);
    RUN_TEST(test_config_field_sizes);

    // Default value tests
    RUN_TEST(test_default_ntp_server);
    RUN_TEST(test_default_timezone);
    RUN_TEST(test_default_ntp_interval);

    // POSIX TZ string tests
    RUN_TEST(test_posix_tz_string_length);
    RUN_TEST(test_ntp_server_length);

    // Interval validation tests
    RUN_TEST(test_min_ntp_interval);
    RUN_TEST(test_max_ntp_interval_reasonable);

    // EEPROM tests
    RUN_TEST(test_eeprom_size);
    RUN_TEST(test_config_fits_in_eeprom);

    // Example validation tests
    RUN_TEST(test_timezone_examples_fit_buffer);
    RUN_TEST(test_ntp_server_examples_fit_buffer);

    return UNITY_END();
}
