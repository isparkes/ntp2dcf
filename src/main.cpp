/**
 * =============================================================================
 * NTP2DCF - DCF77 Emulator for ESP8266
 * =============================================================================
 *
 * This firmware simulates a DCF77 radio time signal transmitter using an
 * ESP8266 microcontroller. It fetches accurate time from NTP servers and
 * generates the corresponding DCF77 pulse sequence that radio-controlled
 * clocks can decode.
 *
 * DCF77 PROTOCOL OVERVIEW:
 * ------------------------
 * DCF77 is a German longwave time signal that transmits at 77.5 kHz.
 * Each second, a pulse is transmitted:
 *   - 100ms pulse = logical 0
 *   - 200ms pulse = logical 1
 *   - No pulse at second 59 = minute marker
 *
 * The time information is BCD-encoded across 59 bits per minute:
 *   - Bits 0-14:  Unused (always 0)
 *   - Bit 15:     Call bit (antenna, unused here)
 *   - Bit 16:     Summer time announcement
 *   - Bit 17:     CEST (Central European Summer Time) active
 *   - Bit 18:     CET (Central European Time) active
 *   - Bit 19:     Leap second announcement
 *   - Bit 20:     Start of time information (always 1)
 *   - Bits 21-27: Minutes (BCD, 7 bits)
 *   - Bit 28:     Minute parity (even)
 *   - Bits 29-34: Hours (BCD, 6 bits)
 *   - Bit 35:     Hour parity (even)
 *   - Bits 36-41: Day of month (BCD, 6 bits)
 *   - Bits 42-44: Day of week (1=Monday, 7=Sunday)
 *   - Bits 45-49: Month (BCD, 5 bits)
 *   - Bits 50-57: Year (BCD, 8 bits, 00-99)
 *   - Bit 58:     Date parity (even, covers bits 36-57)
 *   - Bit 59:     No pulse (minute marker)
 *
 * TRANSMISSION STRATEGY:
 * ----------------------
 * This emulator transmits a 3-minute pulse train to ensure reliable
 * synchronization. The sequence includes:
 *   - 1 sync pulse + 1 blank (simulates minute start)
 *   - 60 pulses for minute 1
 *   - 60 pulses for minute 2
 *   - 60 pulses for minute 3
 *   - 1 closing pulse
 *
 * KNOWN LIMITATIONS:
 * ------------------
 * - DST transitions at 3:00 AM may cause brief inaccuracy until 03:03
 * - NTP packet transit delay not compensated (sub-second precision)
 *
 * HARDWARE CONNECTIONS:
 * ---------------------
 * Output: GPIO2 (D4 on NodeMCU/D1 Mini)
 *
 * Programming ESP-01S via FTDI (3.3V):
 *   FTDI232    ESP-01S
 *   VCC   -->  3.3V, EN
 *   GND   -->  GND, GPIO0 (for flash mode)
 *   RX    -->  TX
 *   TX    -->  RX
 *
 * VERSION HISTORY:
 * ----------------
 * V1.7 - Added web-based configuration interface
 * V1.6 - Added DS1307 I2C slave emulation
 * V1.5 - Fixed weekday conversion and timing bugs
 * V1.4 - WiFiManager integration for easy configuration
 * V1.3 - Added WiFi reconnection logic
 * V1.2 - Increased WiFi timeout to 30 seconds
 * V1.1 - Initial NTP integration
 *
 * CREDITS:
 * --------
 * Original: Fuso68 (05/12/2015)
 * WiFi config: cactus-online (03/06/2021)
 * Based on: UDP NTP Client by Michael Margolis, Tom Igoe, Ivan Grokhotkov
 *
 * License: Public Domain
 * =============================================================================
 */

// =============================================================================
// INCLUDES
// =============================================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <Ticker.h>
#include <TimeLib.h>
#include <time.h>
#include "ds1307_emulation.h"
#include "config_storage.h"
#include "web_server.h"

// =============================================================================
// FUNCTION PROTOTYPES
// =============================================================================

void DcfOut(void);
void ReadAndDecodeTime(void);
int Bin2Bcd(int);
int WeekdayToDcf77(int);
void CalculateArray(int);
void sendNTPpacket(IPAddress &);
void ReConnectToWiFi(void);
bool testWifi(void);

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================

const char *FIRMWARE_VERSION = "V 1.7 - Web configuration interface";

/**
 * DCF77 Signal Polarity Configuration
 * ------------------------------------
 * Different DCF77 receiver modules have different output polarities.
 * Uncomment the appropriate pair for your hardware:
 *
 * For INVERTED modules (e.g., Pollin):
 *   #define pause 1
 *   #define sig 0
 *
 * For NON-INVERTED modules (e.g., ELV):
 *   #define pause 0
 *   #define sig 1
 */
#define pause 0  // Signal level during pause (no carrier reduction)
#define sig 1    // Signal level during pulse (carrier reduction)

// =============================================================================
// NTP CONFIGURATION
// =============================================================================

unsigned int localPort = 2390;                     // Local UDP port for NTP
IPAddress timeServerIP;                            // NTP server IP (resolved at runtime)
const int NTP_PACKET_SIZE = 48;                    // Standard NTP packet size
byte packetBuffer[NTP_PACKET_SIZE];                // NTP packet buffer
WiFiUDP udp;                                       // UDP instance for NTP communication
int UdpNoReplyCounter = 0;                         // Counter for failed NTP requests

// =============================================================================
// TIMING AND STATUS TRACKING
// =============================================================================

unsigned long lastNtpSyncMillis = 0;               // Timestamp of last successful NTP sync
unsigned long lastNtpCheckMillis = 0;              // Timestamp of last NTP check attempt

// =============================================================================
// DCF77 PROTOCOL CONSTANTS
// =============================================================================

#define LedPin 2  // GPIO2 (D4 on NodeMCU) - DCF77 output pin

/**
 * Pulse Array Structure
 * ---------------------
 * Total pulses: 183 = 2 (header) + 60*3 (three minutes) + 1 (tail)
 *
 * Array layout:
 *   [0]     = Sync pulse (1)
 *   [1]     = Blank (0) - simulates minute marker
 *   [2-61]  = First minute (60 pulses)
 *   [62-121]= Second minute (60 pulses)
 *   [122-181]= Third minute (60 pulses)
 *   [182]   = Closing pulse (1)
 *
 * Pulse values:
 *   0 = No pulse (used for second 59, minute marker)
 *   1 = 100ms pulse (logical 0)
 *   2 = 200ms pulse (logical 1)
 */
#define MaxPulseNumber 183
#define FirstMinutePulseBegin 2
#define SecondMinutePulseBegin 62
#define ThirdMinutePulseBegin 122

// =============================================================================
// GLOBAL STATE VARIABLES
// =============================================================================

Ticker DcfOutTimer;                // Hardware timer for 100ms pulse timing
int PulseArray[MaxPulseNumber];    // Complete 3-minute pulse sequence
int PulseCount = 0;                // Current position in pulse array
int DCFOutputOn = 0;               // Flag: 1 = transmission active
int PartialPulseCount = 0;         // Sub-second counter (0-9 for 100ms intervals)

// Current time components (updated from NTP)
int ThisHour, ThisMinute, ThisSecond;
int ThisDay, ThisMonth, ThisYear;
int DayOfW;                        // Day of week (Time library format: Sun=1)
int Dls;                           // Daylight Saving flag: 0=CET, 1=CEST

// =============================================================================
// SETUP - Initialization
// =============================================================================

/**
 * setup()
 * -------
 * Initializes the ESP8266 for DCF77 emulation:
 * 1. Configures serial output for debugging
 * 2. Sets up GPIO for DCF77 signal output
 * 3. Starts the 100ms timer for pulse generation
 * 4. Initializes the pulse array with frame markers
 * 5. Connects to WiFi (or starts configuration AP)
 */
void setup()
{
  // Initialize serial communication for debugging
  Serial.begin(115200);
  Serial.println();
  Serial.print("INIT DCF77 emulator ");
  Serial.println(FIRMWARE_VERSION);
  delay(1000);

  // Load configuration from EEPROM (or use defaults)
  loadConfig();

  // Configure DCF77 output pin
  pinMode(LedPin, OUTPUT);
  digitalWrite(LedPin, LOW);
  delay(1000);

  // Start hardware timer: calls DcfOut() every 100ms
  // This provides the precise timing needed for DCF77 pulses
  DcfOutTimer.attach_ms(100, DcfOut);

  // Initialize pulse array frame markers
  // First pulse: sync signal before transmission
  PulseArray[0] = 1;
  // Second position: blank (no pulse) to simulate minute marker
  PulseArray[1] = 0;
  // Final pulse: closing signal after 3-minute transmission
  PulseArray[MaxPulseNumber - 1] = 1;

  // Initialize transmission state
  PulseCount = 0;
  DCFOutputOn = 0;  // Transmission starts only after NTP sync

  // Initialize WiFi using WiFiManager
  // If no saved credentials, creates AP "Ntp2DCF" for configuration
  Serial.println();
  Serial.println("Starting WiFi-Manager");
  Serial.println();
  WiFiManager wifiManager;
  wifiManager.autoConnect("Ntp2DCF");

  Serial.println("WiFi connected");
  delay(10);
  pinMode(LED_BUILTIN, OUTPUT);

  // Configure timezone using POSIX TZ string
  // This enables automatic DST handling
  configTime(config.tzPosix, config.ntpServer);
  Serial.print("Timezone configured: ");
  Serial.println(config.tzPosix);

  // Start the web server for configuration
  setupWebServer();

  Serial.println();
  Serial.println("Startup complete");

  // Initialize DS1307 I2C emulation
  DS1307_init();
  Serial.println("DS1307 I2C emulation active at address 0x68");
}

// =============================================================================
// MAIN LOOP
// =============================================================================

/**
 * loop()
 * ------
 * Main program loop (non-blocking):
 * 1. Handles web server requests
 * 2. Checks WiFi connection status
 * 3. At configured intervals: fetches NTP time and starts DCF77 transmission
 *
 * Note: The actual DCF77 pulse generation runs independently via
 * the DcfOutTimer interrupt, allowing this loop to handle web requests
 * and NTP synchronization without blocking.
 */
void loop()
{
  // Handle web server requests (non-blocking)
  webServer.handleClient();

  if (WiFi.status() == WL_CONNECTED)
  {
    // Check if it's time for an NTP sync
    unsigned long now = millis();
    if (now - lastNtpCheckMillis >= (unsigned long)config.ntpInterval * 1000)
    {
      lastNtpCheckMillis = now;
      ReadAndDecodeTime();
    }
  }
  else
  {
    ReConnectToWiFi();
  }

  // Small delay to prevent tight loop and allow background tasks
  delay(10);
}

// =============================================================================
// WIFI CONNECTION MANAGEMENT
// =============================================================================

/**
 * testWifi()
 * ----------
 * Tests WiFi connection with timeout.
 *
 * Waits up to 10 seconds (20 x 500ms) for WiFi to connect.
 * If connection fails, restarts the ESP8266 to trigger
 * WiFiManager's configuration portal.
 *
 * Returns:
 *   true  - WiFi connected successfully
 *   false - Never returned (restarts on timeout)
 */
bool testWifi(void)
{
  int attempts = 0;
  Serial.println("Waiting for WiFi to connect");

  while (attempts < 20)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      return true;
    }
    delay(500);
    Serial.print("*");
    attempts++;
  }

  Serial.println("");
  Serial.println("Connection timed out, Restarting...");
  ESP.restart();
  return false;  // Never reached
}

/**
 * ReConnectToWiFi()
 * -----------------
 * Attempts to reconnect to WiFi after connection loss.
 *
 * Disconnects from current network, waits briefly, then
 * attempts to reconnect using saved credentials.
 */
void ReConnectToWiFi()
{
  WiFi.disconnect();
  Serial.println("!!! WiFi Disconnected !");
  delay(1000);

  WiFi.begin();
  if (testWifi())
  {
    Serial.println("!!! Successfully Reconnected !!!");
    return;
  }
}

// =============================================================================
// NTP TIME FETCHING AND PROCESSING
// =============================================================================

/**
 * ReadAndDecodeTime()
 * -------------------
 * Core function that:
 * 1. Fetches current time from NTP server
 * 2. Calculates daylight saving time status
 * 3. Generates DCF77 pulse arrays for 3 minutes
 * 4. Initiates DCF77 transmission at the correct moment
 *
 * Timing Strategy:
 * - DCF77 transmits time for the NEXT minute (not current)
 * - We prepare 3 consecutive minutes of data
 * - Transmission starts at second 58 of current minute
 * - This ensures the first complete minute starts at :00
 */
void ReadAndDecodeTime()
{
  // Resolve NTP server hostname to IP address (from config)
  WiFi.hostByName(config.ntpServer, timeServerIP);

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
  Serial.print("TimeServerIP: ");
  Serial.println(timeServerIP);
  Serial.print("Uptime (s): ");
  Serial.println(millis() / 1000);

  // Send NTP request and wait for response
  sendNTPpacket(timeServerIP);
  delay(1000);  // Wait for NTP reply

  int packetSize = udp.parsePacket();
  if (!packetSize)
  {
    // No NTP response received
    Serial.println("No NTP packet received");

    // After 3 consecutive failures, force WiFi reconnection
    if (UdpNoReplyCounter++ == 3)
    {
      Serial.println("!!! Too many UDP errors, reconnecting WiFi");
      ReConnectToWiFi();
      UdpNoReplyCounter = 0;
    }
  }
  else
  {
    // =========================================================================
    // NTP PACKET PARSING
    // =========================================================================

    UdpNoReplyCounter = 0;
    lastNtpSyncMillis = millis();  // Record successful sync time
    Serial.print("NTP packet received, length=");
    Serial.println(packetSize);

    // Read NTP packet into buffer
    udp.read(packetBuffer, NTP_PACKET_SIZE);

    // Extract 32-bit NTP timestamp from bytes 40-43
    // NTP timestamp = seconds since January 1, 1900
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since 1900 = ");
    Serial.println(secsSince1900);

    // Convert NTP timestamp to Unix timestamp (UTC)
    // Unix epoch starts January 1, 1970 (70 years after NTP epoch)
    const unsigned long seventyYears = 2208988800UL;
    time_t utcTime = secsSince1900 - seventyYears;

    // Add 120 seconds (2 minutes) because:
    //   - DCF77 transmits time for the NEXT minute (+60s)
    //   - Our transmission starts at the NEXT minute boundary (+60s more)
    utcTime += 120;

    Serial.print("Unix time (UTC) = ");
    Serial.println(utcTime);

    // =========================================================================
    // TIMEZONE AND DST CONVERSION (using POSIX TZ string)
    // =========================================================================
    // The configTime() function was called with the POSIX TZ string,
    // so localtime_r() will automatically apply timezone and DST rules.

    struct tm timeinfo;
    localtime_r(&utcTime, &timeinfo);

    // Extract time components
    ThisSecond = timeinfo.tm_sec;
    ThisMinute = timeinfo.tm_min;
    ThisHour = timeinfo.tm_hour;
    ThisDay = timeinfo.tm_mday;
    ThisMonth = timeinfo.tm_mon + 1;  // tm_mon is 0-11
    ThisYear = timeinfo.tm_year + 1900;  // tm_year is years since 1900
    DayOfW = timeinfo.tm_wday + 1;  // tm_wday is 0-6 (Sun=0), we need 1-7 (Sun=1)
    if (DayOfW == 0) DayOfW = 7;  // Handle edge case

    // DST flag from the system (automatically calculated from POSIX TZ string)
    Dls = timeinfo.tm_isdst > 0 ? 1 : 0;

    Serial.print("Local time: ");
    Serial.print(ThisDay);
    Serial.print(".");
    Serial.print(ThisMonth);
    Serial.print(".");
    Serial.print(ThisYear);
    Serial.print(" ");
    Serial.print("DST=");
    Serial.print(Dls);
    Serial.print(" ");

    Serial.print(ThisHour);
    Serial.print(':');
    Serial.print(ThisMinute);
    Serial.print(':');
    Serial.println(ThisSecond);

    // Sync DS1307 registers with NTP time
    DS1307_syncFromNTP(ThisHour, ThisMinute, ThisSecond,
                       DayOfW, ThisDay, ThisMonth, ThisYear);

    // If we're too close to minute boundary, skip this cycle
    // (not enough time to calculate and start transmission)
    if (ThisSecond > 56)
    {
      Serial.println("Too late in minute, skipping to next cycle");
      delay(30000);
      return;
    }

    // =========================================================================
    // DCF77 PULSE ARRAY GENERATION
    // =========================================================================
    // Generate pulse sequences for 3 consecutive minutes.
    // Save the original second before calculations modify time variables.

    int OriginalSecond = ThisSecond;

    // First minute (the minute we're about to transmit)
    CalculateArray(FirstMinutePulseBegin);

    // Second minute (+1)
    utcTime += 60;
    localtime_r(&utcTime, &timeinfo);
    ThisSecond = timeinfo.tm_sec;
    ThisMinute = timeinfo.tm_min;
    ThisHour = timeinfo.tm_hour;
    ThisDay = timeinfo.tm_mday;
    ThisMonth = timeinfo.tm_mon + 1;
    ThisYear = timeinfo.tm_year + 1900;
    DayOfW = timeinfo.tm_wday + 1;
    if (DayOfW == 0) DayOfW = 7;
    Dls = timeinfo.tm_isdst > 0 ? 1 : 0;
    CalculateArray(SecondMinutePulseBegin);

    // Third minute (+2)
    utcTime += 60;
    localtime_r(&utcTime, &timeinfo);
    ThisSecond = timeinfo.tm_sec;
    ThisMinute = timeinfo.tm_min;
    ThisHour = timeinfo.tm_hour;
    ThisDay = timeinfo.tm_mday;
    ThisMonth = timeinfo.tm_mon + 1;
    ThisYear = timeinfo.tm_year + 1900;
    DayOfW = timeinfo.tm_wday + 1;
    if (DayOfW == 0) DayOfW = 7;
    Dls = timeinfo.tm_isdst > 0 ? 1 : 0;
    CalculateArray(ThirdMinutePulseBegin);

    // =========================================================================
    // TRANSMISSION START
    // =========================================================================
    // Wait until second 58, then start transmission.
    // The sync pulse at array[0] plays during second 58,
    // the blank at array[1] at second 59 (minute marker),
    // and the actual time data starts at second 0 of the new minute.

    int waitSeconds = 58 - OriginalSecond;
    Serial.print("Waiting ");
    Serial.print(waitSeconds);
    Serial.println(" seconds to start transmission");
    delay(waitSeconds * 1000);

    // Enable DCF77 output - DcfOut() timer will now generate pulses
    DCFOutputOn = 1;

    // Wait for 3-minute transmission to complete (180s)
    // Plus 30s buffer to ensure we're mid-minute for next cycle
    // Total: 150s here + 60s in main loop = 210s = 3.5 minutes
    delay(150000);
  }

  udp.stop();
}

// =============================================================================
// DCF77 ENCODING
// =============================================================================

/**
 * CalculateArray()
 * ----------------
 * Encodes one minute of time data into DCF77 pulse format.
 *
 * Parameters:
 *   ArrayOffset - Starting index in PulseArray for this minute
 *
 * Uses global variables: ThisMinute, ThisHour, ThisDay, DayOfW,
 *                        ThisMonth, ThisYear, Dls
 *
 * DCF77 Bit Layout (60 bits per minute):
 *   Bits 0-14:   Reserved/unused (logical 0, 100ms pulses)
 *   Bit 15:      Call bit (unused, set to 0)
 *   Bit 16:      DST change announcement (unused, set to 0)
 *   Bit 17:      CEST active (1 if summer time)
 *   Bit 18:      CET active (1 if winter time)
 *   Bit 19:      Leap second announcement (unused, set to 0)
 *   Bit 20:      Start of time data (always 1)
 *   Bits 21-27:  Minutes (BCD, 7 bits: 4 ones + 3 tens)
 *   Bit 28:      Minute parity (even parity over bits 21-27)
 *   Bits 29-34:  Hours (BCD, 6 bits: 4 ones + 2 tens)
 *   Bit 35:      Hour parity (even parity over bits 29-34)
 *   Bits 36-41:  Day of month (BCD, 6 bits)
 *   Bits 42-44:  Day of week (1=Mon, 7=Sun)
 *   Bits 45-49:  Month (BCD, 5 bits)
 *   Bits 50-57:  Year (BCD, 8 bits, 00-99)
 *   Bit 58:      Date parity (even parity over bits 36-57)
 *   Bit 59:      No pulse (minute marker)
 *
 * Pulse encoding in array:
 *   0 = No pulse (only for bit 59)
 *   1 = 100ms pulse (logical 0)
 *   2 = 200ms pulse (logical 1)
 */
void CalculateArray(int ArrayOffset)
{
  int n, Tmp, TmpIn;
  int ParityCount = 0;

  // -------------------------------------------------------------------------
  // Bits 0-19: Reserved bits (all logical 0 = 100ms pulses)
  // -------------------------------------------------------------------------
  for (n = 0; n < 20; n++)
    PulseArray[n + ArrayOffset] = 1;

  // -------------------------------------------------------------------------
  // Bits 17-18: Daylight Saving Time indicators
  // -------------------------------------------------------------------------
  // Bit 17 = CEST (summer time), Bit 18 = CET (winter time)
  // Exactly one of these should be set to 1
  if (Dls == 1)
    PulseArray[17 + ArrayOffset] = 2;  // Summer time (CEST)
  else
    PulseArray[18 + ArrayOffset] = 2;  // Winter time (CET)

  // -------------------------------------------------------------------------
  // Bit 20: Start of time information (always 1)
  // -------------------------------------------------------------------------
  PulseArray[20 + ArrayOffset] = 2;

  // -------------------------------------------------------------------------
  // Bits 21-27: Minutes (BCD encoded, LSB first)
  // -------------------------------------------------------------------------
  TmpIn = Bin2Bcd(ThisMinute);
  for (n = 21; n < 28; n++)
  {
    Tmp = TmpIn & 1;
    PulseArray[n + ArrayOffset] = Tmp + 1;  // 0->1 (100ms), 1->2 (200ms)
    ParityCount += Tmp;
    TmpIn >>= 1;
  }

  // Bit 28: Minute parity (even parity)
  PulseArray[28 + ArrayOffset] = ((ParityCount & 1) == 0) ? 1 : 2;

  // -------------------------------------------------------------------------
  // Bits 29-34: Hours (BCD encoded, LSB first)
  // -------------------------------------------------------------------------
  ParityCount = 0;
  TmpIn = Bin2Bcd(ThisHour);
  for (n = 29; n < 35; n++)
  {
    Tmp = TmpIn & 1;
    PulseArray[n + ArrayOffset] = Tmp + 1;
    ParityCount += Tmp;
    TmpIn >>= 1;
  }

  // Bit 35: Hour parity (even parity)
  PulseArray[35 + ArrayOffset] = ((ParityCount & 1) == 0) ? 1 : 2;

  // -------------------------------------------------------------------------
  // Bits 36-57: Date fields (share one parity bit)
  // -------------------------------------------------------------------------
  ParityCount = 0;

  // Bits 36-41: Day of month (BCD, 6 bits)
  TmpIn = Bin2Bcd(ThisDay);
  for (n = 36; n < 42; n++)
  {
    Tmp = TmpIn & 1;
    PulseArray[n + ArrayOffset] = Tmp + 1;
    ParityCount += Tmp;
    TmpIn >>= 1;
  }

  // Bits 42-44: Day of week (1=Monday, 7=Sunday)
  // Convert from Time library format (Sun=1) to DCF77 format (Mon=1)
  TmpIn = Bin2Bcd(WeekdayToDcf77(DayOfW));
  for (n = 42; n < 45; n++)
  {
    Tmp = TmpIn & 1;
    PulseArray[n + ArrayOffset] = Tmp + 1;
    ParityCount += Tmp;
    TmpIn >>= 1;
  }

  // Bits 45-49: Month (BCD, 5 bits)
  TmpIn = Bin2Bcd(ThisMonth);
  for (n = 45; n < 50; n++)
  {
    Tmp = TmpIn & 1;
    PulseArray[n + ArrayOffset] = Tmp + 1;
    ParityCount += Tmp;
    TmpIn >>= 1;
  }

  // Bits 50-57: Year within century (BCD, 8 bits, 00-99)
  TmpIn = Bin2Bcd(ThisYear - 2000);
  for (n = 50; n < 58; n++)
  {
    Tmp = TmpIn & 1;
    PulseArray[n + ArrayOffset] = Tmp + 1;
    ParityCount += Tmp;
    TmpIn >>= 1;
  }

  // Bit 58: Date parity (even parity over bits 36-57)
  PulseArray[58 + ArrayOffset] = ((ParityCount & 1) == 0) ? 1 : 2;

  // -------------------------------------------------------------------------
  // Bit 59: Minute marker (no pulse)
  // -------------------------------------------------------------------------
  PulseArray[59 + ArrayOffset] = 0;
}

// =============================================================================
// NTP PACKET HANDLING
// =============================================================================

/**
 * sendNTPpacket()
 * ---------------
 * Sends an NTP time request to the specified server.
 *
 * NTP Packet Structure (48 bytes):
 *   Byte 0:     LI (2 bits) | Version (3 bits) | Mode (3 bits)
 *               - LI = 11 (clock unsynchronized)
 *               - Version = 4
 *               - Mode = 3 (client)
 *   Byte 1:     Stratum (0 = unspecified)
 *   Byte 2:     Poll interval (6 = 64 seconds)
 *   Byte 3:     Precision (0xEC = -20, ~1 microsecond)
 *   Bytes 4-11: Root delay & dispersion (0)
 *   Bytes 12-15: Reference ID ("1N14" for client request)
 *   ...remaining bytes: timestamps (0 for request)
 *
 * Parameters:
 *   address - IP address of NTP server
 */
void sendNTPpacket(IPAddress &address)
{
  Serial.println("Sending NTP packet...");

  // Clear packet buffer
  memset(packetBuffer, 0, NTP_PACKET_SIZE);

  // Build NTP request header
  packetBuffer[0] = 0b11100011;  // LI=11, Version=4, Mode=3 (client)
  packetBuffer[1] = 0;           // Stratum (unspecified for client)
  packetBuffer[2] = 6;           // Poll interval (64 seconds)
  packetBuffer[3] = 0xEC;        // Precision (~1 microsecond)
  // Bytes 4-11: Root delay & dispersion (left as 0)

  // Reference identifier (arbitrary for client request)
  packetBuffer[12] = 49;   // '1'
  packetBuffer[13] = 0x4E; // 'N'
  packetBuffer[14] = 49;   // '1'
  packetBuffer[15] = 52;   // '4'

  // Send UDP packet to NTP server port 123
  udp.beginPacket(address, 123);
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

// =============================================================================
// DCF77 SIGNAL GENERATION (Timer Interrupt)
// =============================================================================

/**
 * DcfOut()
 * --------
 * Timer interrupt handler called every 100ms.
 * Generates the DCF77 amplitude-modulated signal.
 *
 * DCF77 Timing (per second):
 *   - Logical 0: 100ms low, 900ms high (carrier reduced for 100ms)
 *   - Logical 1: 200ms low, 800ms high (carrier reduced for 200ms)
 *   - No pulse:  No carrier reduction (full second high)
 *
 * PartialPulseCount cycles 0-9 (ten 100ms intervals per second):
 *   0: Start of second - begin pulse (go LOW if pulse exists)
 *   1: After 100ms - end pulse if it was a short (100ms) pulse
 *   2: After 200ms - end pulse (all pulses end by 200ms)
 *   3-8: Idle (signal stays HIGH)
 *   9: End of second - advance to next pulse, reset counter
 *
 * Signal levels (active-low for typical DCF77 modules):
 *   pause (0) = carrier ON  (signal high on receiver)
 *   sig (1)   = carrier OFF (signal low on receiver)
 */
void DcfOut()
{
  if (DCFOutputOn == 1)
  {
    switch (PartialPulseCount++)
    {
    case 0:
      // Start of second: begin carrier reduction if this is a pulse
      if (PulseArray[PulseCount] != 0)
        digitalWrite(LedPin, pause);  // Reduce carrier (start pulse)
      break;

    case 1:
      // At 100ms: end pulse if it's a short pulse (logical 0)
      if (PulseArray[PulseCount] == 1)
        digitalWrite(LedPin, sig);    // Restore carrier
      break;

    case 2:
      // At 200ms: all remaining pulses end (logical 1)
      digitalWrite(LedPin, sig);      // Restore carrier
      break;

    case 9:
      // End of second: advance to next pulse
      if (PulseCount++ == (MaxPulseNumber - 1))
      {
        // Transmission complete - reset for next cycle
        PulseCount = 0;
        DCFOutputOn = 0;
      }
      PartialPulseCount = 0;
      break;

    default:
      // Cases 3-8: idle period, signal stays high
      break;
    }
  }
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

/**
 * Bin2Bcd()
 * ---------
 * Converts a binary number (0-99) to Binary-Coded Decimal (BCD).
 *
 * BCD represents each decimal digit with 4 bits:
 *   Example: 23 decimal = 0010 0011 BCD = 0x23 = 35 decimal
 *
 * Parameters:
 *   dato - Binary value (0-99)
 *
 * Returns:
 *   BCD-encoded value
 *
 * Note: For single-digit values (0-9), binary = BCD.
 */
int Bin2Bcd(int dato)
{
  if (dato < 10)
    return dato;

  int tens = (dato / 10) << 4;  // Tens digit in upper nibble
  int ones = dato % 10;         // Ones digit in lower nibble
  return tens + ones;
}

/**
 * WeekdayToDcf77()
 * ----------------
 * Converts Time library weekday format to DCF77 weekday format.
 *
 * Time library format: Sunday=1, Monday=2, ..., Saturday=7
 * DCF77 format:        Monday=1, Tuesday=2, ..., Sunday=7
 *
 * Parameters:
 *   timeLibWeekday - Day of week in Time library format (1-7)
 *
 * Returns:
 *   Day of week in DCF77 format (1-7)
 */
int WeekdayToDcf77(int timeLibWeekday)
{
  if (timeLibWeekday == 1)
    return 7;  // Sunday: 1 -> 7
  return timeLibWeekday - 1;  // Mon-Sat: shift down by 1
}
