/**
 * Configuration Storage for NTP2DCF
 *
 * Handles persistent storage of user configuration in EEPROM.
 * Configuration includes NTP server, timezone, and sync interval.
 */
#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#ifdef UNIT_TEST
#include <stdint.h>
#include <string.h>
#else
#include <Arduino.h>
#include <EEPROM.h>
#endif

// =============================================================================
// Configuration Constants
// =============================================================================

#define CONFIG_MAGIC 0xDC          // Magic byte to validate EEPROM contents
#define CONFIG_VERSION 1           // Configuration version for future migrations
#define EEPROM_SIZE 256            // Total EEPROM allocation
#define NTP_SERVER_MAX_LEN 64      // Max length for NTP server hostname
#define TZ_POSIX_MAX_LEN 48        // Max length for POSIX TZ string

// Default values
#define DEFAULT_NTP_SERVER "0.de.pool.ntp.org"
#define DEFAULT_TZ_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#define DEFAULT_NTP_INTERVAL 60    // seconds

// =============================================================================
// Configuration Structure
// =============================================================================

/**
 * User-configurable settings stored in EEPROM
 *
 * POSIX TZ String format examples:
 *   CET-1CEST,M3.5.0,M10.5.0/3  - Central European Time
 *   GMT0BST,M3.5.0/1,M10.5.0    - UK Time
 *   EST5EDT,M3.2.0,M11.1.0      - US Eastern Time
 *   PST8PDT,M3.2.0,M11.1.0      - US Pacific Time
 *   JST-9                        - Japan (no DST)
 *   AEST-10AEDT,M10.1.0,M4.1.0/3 - Australia Eastern
 */
struct NTP2DCFConfig {
    uint8_t magic;                      // Magic byte for validation
    uint8_t version;                    // Config version
    char ntpServer[NTP_SERVER_MAX_LEN]; // NTP pool server hostname
    char tzPosix[TZ_POSIX_MAX_LEN];     // POSIX timezone string
    uint16_t ntpInterval;               // NTP sync interval in seconds (60-3600)
    uint8_t checksum;                   // Simple checksum for validation
};

#ifndef UNIT_TEST

// Global configuration instance
inline NTP2DCFConfig config;

// Forward declarations
inline void saveConfig();
inline void setDefaultConfig();

// =============================================================================
// Checksum Calculation
// =============================================================================

/**
 * Calculate simple checksum over config data
 */
inline uint8_t calculateChecksum(const NTP2DCFConfig* cfg) {
    uint8_t sum = 0;
    const uint8_t* data = (const uint8_t*)cfg;
    // Sum all bytes except the checksum field itself
    for (size_t i = 0; i < sizeof(NTP2DCFConfig) - 1; i++) {
        sum += data[i];
    }
    return sum;
}

// =============================================================================
// Configuration Functions
// =============================================================================

/**
 * Initialize configuration with default values
 */
inline void setDefaultConfig() {
    config.magic = CONFIG_MAGIC;
    config.version = CONFIG_VERSION;
    strncpy(config.ntpServer, DEFAULT_NTP_SERVER, NTP_SERVER_MAX_LEN - 1);
    config.ntpServer[NTP_SERVER_MAX_LEN - 1] = '\0';
    strncpy(config.tzPosix, DEFAULT_TZ_POSIX, TZ_POSIX_MAX_LEN - 1);
    config.tzPosix[TZ_POSIX_MAX_LEN - 1] = '\0';
    config.ntpInterval = DEFAULT_NTP_INTERVAL;
    config.checksum = calculateChecksum(&config);
}

/**
 * Load configuration from EEPROM
 * Returns true if valid config loaded, false if defaults were applied
 */
inline bool loadConfig() {
    EEPROM.begin(EEPROM_SIZE);

    // Read config from EEPROM
    EEPROM.get(0, config);

    // Validate magic byte
    if (config.magic != CONFIG_MAGIC) {
        Serial.println("Config: Invalid magic byte, using defaults");
        setDefaultConfig();
        saveConfig();
        return false;
    }

    // Validate checksum
    uint8_t expectedChecksum = calculateChecksum(&config);
    if (config.checksum != expectedChecksum) {
        Serial.println("Config: Checksum mismatch, using defaults");
        setDefaultConfig();
        saveConfig();
        return false;
    }

    // Validate ntpInterval range
    if (config.ntpInterval < 60 || config.ntpInterval > 3600) {
        Serial.println("Config: Invalid NTP interval, correcting");
        config.ntpInterval = DEFAULT_NTP_INTERVAL;
        config.checksum = calculateChecksum(&config);
        saveConfig();
    }

    // Ensure strings are null-terminated
    config.ntpServer[NTP_SERVER_MAX_LEN - 1] = '\0';
    config.tzPosix[TZ_POSIX_MAX_LEN - 1] = '\0';

    Serial.println("Config: Loaded from EEPROM");
    Serial.print("  NTP Server: ");
    Serial.println(config.ntpServer);
    Serial.print("  Timezone: ");
    Serial.println(config.tzPosix);
    Serial.print("  Sync Interval: ");
    Serial.print(config.ntpInterval);
    Serial.println("s");

    return true;
}

/**
 * Save configuration to EEPROM
 */
inline void saveConfig() {
    // Update checksum before saving
    config.checksum = calculateChecksum(&config);

    EEPROM.put(0, config);
    EEPROM.commit();

    Serial.println("Config: Saved to EEPROM");
}

/**
 * Reset configuration to factory defaults
 */
inline void resetConfig() {
    setDefaultConfig();
    saveConfig();
    Serial.println("Config: Reset to factory defaults");
}

#endif // UNIT_TEST

#endif // CONFIG_STORAGE_H
