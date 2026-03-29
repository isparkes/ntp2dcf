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
 * V1.8.1 - Fix compiler warnings about buffer size on snprintf
 * V1.8   - Decoupled NTP sync and DCF77 transmission into independent timers
 * V1.7   - Added web-based configuration interface
 * V1.6   - Added DS1307 I2C slave emulation
 * V1.5   - Fixed weekday conversion and timing bugs
 * V1.4   - WiFiManager integration for easy configuration
 * V1.3   - Added WiFi reconnection logic
 * V1.2   - Increased WiFi timeout to 30 seconds
 * V1.1   - Initial NTP integration
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
bool syncNTP(void);               // Fetch time from NTP server, returns true on success
bool startDcfTransmission(void);  // Start DCF77 transmission from system clock, returns true on success
int Bin2Bcd(int);
int WeekdayToDcf77(int);
void CalculateArray(int);
void sendNTPpacket(IPAddress &);
void ReConnectToWiFi(void);
bool testWifi(void);
void delayWithWebServer(unsigned long ms);

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================

const char *FIRMWARE_VERSION = "V 1.8.1 - Independent NTP/DCF timers";

/**
 * DCF77 Signal Polarity Configuration
 * ------------------------------------
 * Different DCF77 receiver modules have different output polarities.
 * This is now configurable via the web interface:
 *
 * For NON-INVERTED modules (e.g., ELV): pause=0, sig=1
 * For INVERTED modules (e.g., Pollin): pause=1, sig=0
 *
 * The signal levels are computed dynamically based on config.dcfSignalInverted
 */

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
unsigned long lastDcfTransmitMillis = 0;           // Timestamp of last DCF transmission attempt

#define DCF_TRANSMIT_INTERVAL 300                  // DCF77 transmission interval in seconds (5 minutes)

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

// DCF77 transmission time tracking (the time being transmitted, which is in the future)
int DcfTransmitHour = 0;           // Hour currently being transmitted
int DcfTransmitMinute = 0;         // Minute currently being transmitted
time_t DcfFirstMinuteTime = 0;     // Unix time of first minute in transmission
time_t DcfLastMinuteTime = 0;      // Unix time of last minute in transmission (for logging)

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
  Serial.println("WiFi: Starting WiFiManager");
  WiFiManager wifiManager;
  wifiManager.autoConnect("Ntp2DCF");
  Serial.print("WiFi: Connected to ");
  Serial.println(WiFi.SSID());
  delay(10);
  pinMode(LED_BUILTIN, OUTPUT);

  // Configure timezone using POSIX TZ string
  // This enables automatic DST handling
  configTime(config.tzPosix, config.ntpServer);

  // Start the web server for configuration
  setupWebServer();

  // Initialize DS1307 I2C emulation
  DS1307_init();
  Serial.println("I2C: DS1307 emulation active at 0x68");

  Serial.println("Startup complete");
  Serial.println();

  // Perform initial NTP sync and start first DCF transmission immediately
  if (syncNTP()) {
    lastNtpCheckMillis = millis();
    if (startDcfTransmission()) {
      lastDcfTransmitMillis = millis();
    }
  }
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
    unsigned long now = millis();

    // --- NTP sync timer (runs at config.ntpInterval) ---
    if (now - lastNtpCheckMillis >= (unsigned long)config.ntpInterval * 1000)
    {
      if (syncNTP()) {
        lastNtpCheckMillis = now;
      }
    }

    // --- DCF transmission timer (runs every DCF_TRANSMIT_INTERVAL) ---
    if (!DCFOutputOn && (now - lastDcfTransmitMillis >= (unsigned long)DCF_TRANSMIT_INTERVAL * 1000))
    {
      if (startDcfTransmission()) {
        lastDcfTransmitMillis = millis();  // Use millis() after transmission completes
      }
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

/**
 * delayWithWebServer()
 * --------------------
 * Performs a delay while continuing to handle web server requests.
 *
 * This prevents HTTP request timeouts during long-running operations
 * like waiting for NTP responses or DCF77 transmission completion.
 *
 * Parameters:
 *   ms - Delay duration in milliseconds
 */
void delayWithWebServer(unsigned long ms)
{
  unsigned long start = millis();
  while (millis() - start < ms)
  {
    webServer.handleClient();
    delay(10);  // Small delay to prevent tight loop
  }
}

// =============================================================================
// NTP TIME FETCHING AND PROCESSING
// =============================================================================

/**
 * syncNTP()
 * ---------
 * Fetches current time from NTP server and updates the system clock.
 * This is independent of DCF77 transmission.
 *
 * Returns:
 *   true  - NTP sync successful
 *   false - NTP sync failed
 */
bool syncNTP()
{
  // Resolve NTP server hostname to IP address (from config)
  WiFi.hostByName(config.ntpServer, timeServerIP);

  Serial.print("NTP: Querying ");
  Serial.print(config.ntpServer);
  Serial.print(" (");
  Serial.print(timeServerIP);
  Serial.println(")");

  udp.begin(localPort);
  sendNTPpacket(timeServerIP);
  delayWithWebServer(1000);  // Wait for NTP reply

  int packetSize = udp.parsePacket();
  if (!packetSize)
  {
    // No NTP response received
    Serial.println("NTP: No response received");

    // After 3 consecutive failures, force WiFi reconnection
    if (UdpNoReplyCounter++ == 3)
    {
      Serial.println("NTP: Too many failures, reconnecting WiFi");
      ReConnectToWiFi();
      UdpNoReplyCounter = 0;
    }
    udp.stop();
    return false;
  }

  // =========================================================================
  // NTP PACKET PARSING
  // =========================================================================

  UdpNoReplyCounter = 0;
  lastNtpSyncMillis = millis();  // Record successful sync time

  // Read NTP packet into buffer
  udp.read(packetBuffer, NTP_PACKET_SIZE);

  // Extract 32-bit NTP timestamp from bytes 40-43
  // NTP timestamp = seconds since January 1, 1900
  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  unsigned long secsSince1900 = highWord << 16 | lowWord;

  // Convert NTP timestamp to Unix timestamp (UTC)
  // Unix epoch starts January 1, 1970 (70 years after NTP epoch)
  const unsigned long seventyYears = 2208988800UL;
  time_t utcTime = secsSince1900 - seventyYears;

  // =========================================================================
  // TIMEZONE AND DST CONVERSION (using POSIX TZ string)
  // =========================================================================

  struct tm timeinfo;
  localtime_r(&utcTime, &timeinfo);

  // Extract current time components for display and DS1307
  ThisSecond = timeinfo.tm_sec;
  ThisMinute = timeinfo.tm_min;
  ThisHour = timeinfo.tm_hour;
  ThisDay = timeinfo.tm_mday;
  ThisMonth = timeinfo.tm_mon + 1;
  ThisYear = timeinfo.tm_year + 1900; 
  DayOfW = timeinfo.tm_wday + 1;
  if (DayOfW == 0) DayOfW = 7;

  // DST flag from the system (automatically calculated from POSIX TZ string)
  Dls = timeinfo.tm_isdst > 0 ? 1 : 0;

  // Format and print local time
  char timeStr[80];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d %02d.%02d.%04d %s",
           ThisHour, ThisMinute, ThisSecond,
           ThisDay, ThisMonth, ThisYear,
           Dls ? "CEST" : "CET");
  Serial.print("NTP: Synced - ");
  Serial.println(timeStr);

  // Sync DS1307 registers with actual NTP time
  DS1307_syncFromNTP(ThisHour, ThisMinute, ThisSecond,
                     DayOfW, ThisDay, ThisMonth, ThisYear);

  udp.stop();
  return true;
}

/**
 * startDcfTransmission()
 * ----------------------
 * Reads the current system clock and generates a 3-minute DCF77
 * pulse train. Independent of NTP - uses whatever time the system
 * clock currently has (kept accurate by ESP8266's internal SNTP).
 *
 * Returns:
 *   true  - Transmission started successfully
 *   false - Could not start (time not set or too close to minute boundary)
 */
bool startDcfTransmission()
{
  // Don't start if a transmission is already in progress
  if (DCFOutputOn) {
    return false;
  }

  // Read current time from system clock
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // Validate that the system clock is set (year > 2020)
  if (timeinfo.tm_year + 1900 < 2021) {
    Serial.println("DCF: System clock not set, skipping transmission");
    return false;
  }

  int currentSecond = timeinfo.tm_sec;

  // If we're too close to minute boundary, wait for the next minute
  if (currentSecond > 56)
  {
    int waitForNextMinute = 62 - currentSecond;  // Wait past second 0 of next minute
    Serial.print("DCF: Near minute boundary, waiting ");
    Serial.print(waitForNextMinute);
    Serial.println("s for next minute");
    delayWithWebServer(waitForNextMinute * 1000);

    // Re-read time after waiting
    now = time(nullptr);
    localtime_r(&now, &timeinfo);
    currentSecond = timeinfo.tm_sec;
  }

  // Update globals from current time
  ThisSecond = timeinfo.tm_sec;
  ThisMinute = timeinfo.tm_min;
  ThisHour = timeinfo.tm_hour;
  ThisDay = timeinfo.tm_mday;
  ThisMonth = timeinfo.tm_mon + 1;
  ThisYear = timeinfo.tm_year + 1900;
  DayOfW = timeinfo.tm_wday + 1;
  if (DayOfW == 0) DayOfW = 7;
  Dls = timeinfo.tm_isdst > 0 ? 1 : 0;

  // Log what time we're starting from
  char timeStr[32];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d %s",
           ThisHour, ThisMinute, ThisSecond,
           Dls ? "CEST" : "CET");
  Serial.print("DCF: Preparing transmission at ");
  Serial.println(timeStr);

  // =========================================================================
  // DCF77 PULSE ARRAY GENERATION
  // =========================================================================
  // Generate pulse sequences for 3 consecutive minutes.
  // DCF77 transmits time for the NEXT minute, and our transmission starts
  // at the next minute boundary, so we add 120 seconds (2 minutes) offset.

  time_t dcfTime = now + (120 - currentSecond);  // Round to start of minute + 2 minutes
  DcfFirstMinuteTime = dcfTime;

  // First minute
  localtime_r(&dcfTime, &timeinfo);
  ThisSecond = timeinfo.tm_sec;
  ThisMinute = timeinfo.tm_min;
  ThisHour = timeinfo.tm_hour;
  ThisDay = timeinfo.tm_mday;
  ThisMonth = timeinfo.tm_mon + 1;
  ThisYear = timeinfo.tm_year + 1900;
  DayOfW = timeinfo.tm_wday + 1;
  if (DayOfW == 0) DayOfW = 7;
  Dls = timeinfo.tm_isdst > 0 ? 1 : 0;
  DcfTransmitHour = ThisHour;
  DcfTransmitMinute = ThisMinute;
  CalculateArray(FirstMinutePulseBegin);

  // Second minute (+1)
  dcfTime += 60;
  localtime_r(&dcfTime, &timeinfo);
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
  dcfTime += 60;
  DcfLastMinuteTime = dcfTime;
  localtime_r(&dcfTime, &timeinfo);
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

  // Re-read current second (time may have passed during array calculation)
  now = time(nullptr);
  localtime_r(&now, &timeinfo);
  int waitSeconds = 58 - timeinfo.tm_sec;
  if (waitSeconds < 0) waitSeconds += 60;  // Wrapped past minute boundary

  Serial.print("DCF: Waiting ");
  Serial.print(waitSeconds);
  Serial.println("s for minute boundary");
  delayWithWebServer(waitSeconds * 1000);

  // Log transmission start
  {
    struct tm startInfo, endInfo;
    localtime_r(&DcfFirstMinuteTime, &startInfo);
    localtime_r(&DcfLastMinuteTime, &endInfo);
    char startStr[16], endStr[16];
    snprintf(startStr, sizeof(startStr), "%02d:%02d", startInfo.tm_hour, startInfo.tm_min);
    snprintf(endStr, sizeof(endStr), "%02d:%02d", endInfo.tm_hour, endInfo.tm_min);
    Serial.print("DCF: Starting transmission (");
    Serial.print(startStr);
    Serial.print(" - ");
    Serial.print(endStr);
    Serial.println(")");
  }

  // Enable DCF77 output - DcfOut() timer will now generate pulses
  DCFOutputOn = 1;

  // Wait for 3-minute transmission to complete
  delayWithWebServer(150000);

  // Log transmission complete
  {
    struct tm startInfo, endInfo;
    localtime_r(&DcfFirstMinuteTime, &startInfo);
    localtime_r(&DcfLastMinuteTime, &endInfo);
    char startStr[16], endStr[16];
    snprintf(startStr, sizeof(startStr), "%02d:%02d", startInfo.tm_hour, startInfo.tm_min);
    snprintf(endStr, sizeof(endStr), "%02d:%02d", endInfo.tm_hour, endInfo.tm_min);
    Serial.print("DCF: Transmission complete (");
    Serial.print(startStr);
    Serial.print(" - ");
    Serial.print(endStr);
    Serial.println(")");
  }

  return true;
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
    // Compute signal levels based on configuration
    // Non-inverted (ELV): pause=0 (LOW), sig=1 (HIGH)
    // Inverted (Pollin):  pause=1 (HIGH), sig=0 (LOW)
    int pause = config.dcfSignalInverted ? 1 : 0;
    int sig = config.dcfSignalInverted ? 0 : 1;

    switch (PartialPulseCount++)
    {
    case 0:
      // Start of second: begin carrier reduction if this is a pulse
      if (PulseArray[PulseCount] != 0)
        digitalWrite(LedPin, pause);  // Reduce carrier (start pulse)

      // Update which minute is being transmitted based on PulseCount
      // Pulses 2-61: first minute, 62-121: second minute, 122-181: third minute
      {
        int minuteOffset = 0;
        if (PulseCount >= ThirdMinutePulseBegin) minuteOffset = 2;
        else if (PulseCount >= SecondMinutePulseBegin) minuteOffset = 1;

        time_t currentMinuteTime = DcfFirstMinuteTime + (minuteOffset * 60);
        struct tm tmInfo;
        localtime_r(&currentMinuteTime, &tmInfo);
        DcfTransmitHour = tmInfo.tm_hour;
        DcfTransmitMinute = tmInfo.tm_min;
      }
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
        digitalWrite(LedPin, pause);
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
