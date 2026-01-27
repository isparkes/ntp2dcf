/**
 * DS1307 I2C Slave Emulation for ESP8266
 *
 * Emulates a DS1307 RTC chip at I2C address 0x68, allowing external devices
 * to read NTP-synchronized time using standard DS1307 libraries (e.g., RTClib).
 *
 * Register Layout:
 *   0x00: Seconds (BCD) - Bit 7 = CH (Clock Halt, 0=running)
 *   0x01: Minutes (BCD)
 *   0x02: Hours (BCD) - Bit 6 = 12/24 mode (0=24hr)
 *   0x03: Day of week (1-7, Sunday=1)
 *   0x04: Date (BCD, 1-31)
 *   0x05: Month (BCD, 1-12)
 *   0x06: Year (BCD, 00-99)
 *   0x07: Control register
 *   0x08-0x3F: 56 bytes RAM
 *
 * Hardware: I2C on GPIO4 (SDA) and GPIO5 (SCL) - D2/D1 on D1 Mini
 */
#ifndef DS1307_EMULATION_H
#define DS1307_EMULATION_H

#ifdef UNIT_TEST
// Stub for unit testing without Arduino
#include <stdint.h>
#else
#include <Arduino.h>
#include <Wire.h>
#endif

// I2C address (same as real DS1307)
#define DS1307_I2C_ADDRESS 0x68

// Register count (8 time registers + 56 bytes RAM)
#define DS1307_REGISTER_COUNT 64

// Register addresses
#define DS1307_REG_SECONDS  0x00
#define DS1307_REG_MINUTES  0x01
#define DS1307_REG_HOURS    0x02
#define DS1307_REG_DAY      0x03
#define DS1307_REG_DATE     0x04
#define DS1307_REG_MONTH    0x05
#define DS1307_REG_YEAR     0x06
#define DS1307_REG_CONTROL  0x07
#define DS1307_REG_RAM_START 0x08

// I2C pins for ESP8266 (D1 Mini: D2=GPIO4=SDA, D1=GPIO5=SCL)
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5

/**
 * Convert binary value (0-99) to BCD format
 */
inline uint8_t DS1307_toBCD(uint8_t value) {
    return ((value / 10) << 4) | (value % 10);
}

/**
 * Convert BCD value to binary
 */
inline uint8_t DS1307_fromBCD(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

#ifndef UNIT_TEST

/**
 * DS1307 Emulator State
 * All fields are volatile for ISR safety
 */
struct DS1307State {
    volatile uint8_t registers[DS1307_REGISTER_COUNT];
    volatile uint8_t addressPointer;
    volatile uint32_t lastSyncMillis;
    volatile uint8_t baseSeconds;
    volatile uint8_t baseMinutes;
    volatile uint8_t baseHours;
};

// Global emulator state
extern DS1307State ds1307;

/**
 * Initialize DS1307 I2C slave emulation
 * Sets up Wire as slave at address 0x68
 */
void DS1307_init();

/**
 * Sync DS1307 registers from NTP time
 * Call this after each NTP sync to update the time registers
 *
 * @param hour       Hour (0-23)
 * @param minute     Minute (0-59)
 * @param second     Second (0-59)
 * @param dayOfWeek  Day of week (Time library format: Sun=1, Mon=2, ..., Sat=7)
 * @param dayOfMonth Day of month (1-31)
 * @param month      Month (1-12)
 * @param year       Year (full year, e.g., 2024)
 */
void DS1307_syncFromNTP(int hour, int minute, int second,
                        int dayOfWeek, int dayOfMonth,
                        int month, int year);

/**
 * Update live time registers based on millis() offset from last sync
 * Called internally before I2C reads to provide real-time seconds
 */
void DS1307_updateLiveTime();

/**
 * I2C receive callback (interrupt context)
 * Handles writes: first byte sets address pointer, subsequent bytes write to registers
 */
void DS1307_onReceive(int numBytes);

/**
 * I2C request callback (interrupt context)
 * Handles reads: sends register data starting from address pointer, auto-increments
 */
void DS1307_onRequest();

// =============================================================================
// Implementation (inline in header for simplicity)
// =============================================================================

// Global state instance
inline DS1307State ds1307 = {};

inline void DS1307_init() {
    // Initialize registers to default values
    for (int i = 0; i < DS1307_REGISTER_COUNT; i++) {
        ds1307.registers[i] = 0;
    }

    // Set CH bit to 0 (clock running) in seconds register
    ds1307.registers[DS1307_REG_SECONDS] = 0x00;

    // Set 24-hour mode (bit 6 = 0) in hours register
    ds1307.registers[DS1307_REG_HOURS] = 0x00;

    // Default day of week to Sunday (1)
    ds1307.registers[DS1307_REG_DAY] = 0x01;

    // Initialize address pointer
    ds1307.addressPointer = 0;

    // Initialize sync tracking
    ds1307.lastSyncMillis = millis();
    ds1307.baseSeconds = 0;
    ds1307.baseMinutes = 0;
    ds1307.baseHours = 0;

    // Initialize I2C as slave
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, DS1307_I2C_ADDRESS);
    Wire.onReceive(DS1307_onReceive);
    Wire.onRequest(DS1307_onRequest);
}

inline void DS1307_syncFromNTP(int hour, int minute, int second,
                               int dayOfWeek, int dayOfMonth,
                               int month, int year) {
    // Disable interrupts during update for thread safety
    noInterrupts();

    // Store base time for real-time tracking
    ds1307.baseSeconds = second;
    ds1307.baseMinutes = minute;
    ds1307.baseHours = hour;
    ds1307.lastSyncMillis = millis();

    // Update time registers in BCD format
    // Seconds: bit 7 (CH) = 0 means clock is running
    ds1307.registers[DS1307_REG_SECONDS] = DS1307_toBCD(second) & 0x7F;
    ds1307.registers[DS1307_REG_MINUTES] = DS1307_toBCD(minute);
    // Hours: bit 6 = 0 for 24-hour mode
    ds1307.registers[DS1307_REG_HOURS] = DS1307_toBCD(hour) & 0x3F;

    // Day of week: DS1307 uses 1-7, Sunday=1 (same as Time library)
    ds1307.registers[DS1307_REG_DAY] = dayOfWeek;

    // Date (day of month)
    ds1307.registers[DS1307_REG_DATE] = DS1307_toBCD(dayOfMonth);

    // Month
    ds1307.registers[DS1307_REG_MONTH] = DS1307_toBCD(month);

    // Year (only last two digits)
    ds1307.registers[DS1307_REG_YEAR] = DS1307_toBCD(year % 100);

    interrupts();
}

inline void DS1307_updateLiveTime() {
    // Calculate elapsed time since last NTP sync
    uint32_t elapsedMs = millis() - ds1307.lastSyncMillis;
    uint32_t elapsedSeconds = elapsedMs / 1000;

    // Calculate current time
    uint32_t totalSeconds = ds1307.baseSeconds + elapsedSeconds;
    uint8_t seconds = totalSeconds % 60;

    uint32_t totalMinutes = ds1307.baseMinutes + (totalSeconds / 60);
    uint8_t minutes = totalMinutes % 60;

    uint32_t totalHours = ds1307.baseHours + (totalMinutes / 60);
    uint8_t hours = totalHours % 24;

    // Update only time registers (date stays from last sync)
    // This is acceptable since NTP syncs every 60 seconds
    ds1307.registers[DS1307_REG_SECONDS] = DS1307_toBCD(seconds) & 0x7F;
    ds1307.registers[DS1307_REG_MINUTES] = DS1307_toBCD(minutes);
    ds1307.registers[DS1307_REG_HOURS] = DS1307_toBCD(hours) & 0x3F;
}

inline void DS1307_onReceive(int numBytes) {
    if (numBytes < 1) return;

    // First byte is always the register address
    ds1307.addressPointer = Wire.read();
    if (ds1307.addressPointer >= DS1307_REGISTER_COUNT) {
        ds1307.addressPointer = 0;
    }
    numBytes--;

    // Subsequent bytes are data to write to registers
    while (numBytes > 0 && Wire.available()) {
        ds1307.registers[ds1307.addressPointer] = Wire.read();
        ds1307.addressPointer++;
        if (ds1307.addressPointer >= DS1307_REGISTER_COUNT) {
            ds1307.addressPointer = 0;
        }
        numBytes--;
    }
}

inline void DS1307_onRequest() {
    // Update time registers with current time before responding
    DS1307_updateLiveTime();

    // Send register data starting from current address pointer
    // DS1307 auto-increments address on each byte read
    // Send up to 32 bytes (Wire library buffer limit)
    uint8_t bytesToSend = DS1307_REGISTER_COUNT - ds1307.addressPointer;
    if (bytesToSend > 32) bytesToSend = 32;

    for (uint8_t i = 0; i < bytesToSend; i++) {
        Wire.write(ds1307.registers[ds1307.addressPointer]);
        ds1307.addressPointer++;
        if (ds1307.addressPointer >= DS1307_REGISTER_COUNT) {
            ds1307.addressPointer = 0;
        }
    }
}

#endif // UNIT_TEST

#endif // DS1307_EMULATION_H
