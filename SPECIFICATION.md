# NTP2DCF Specification

## Document Information

| Field | Value |
|-------|-------|
| Version | 1.8 |
| Date | January 2026 |
| Status | Release |

---

## 1. Overview

### 1.1 Purpose

NTP2DCF is a DCF77 radio time signal emulator that runs on an ESP8266 microcontroller. It retrieves accurate time from Network Time Protocol (NTP) servers via WiFi and generates a DCF77-compatible signal that can synchronize radio-controlled clocks.

### 1.2 Use Cases

- Synchronizing DCF77 radio-controlled clocks in areas with poor radio reception
- Indoor use where the 77.5 kHz longwave signal cannot penetrate
- Testing and development of DCF77 receiver circuits
- Providing time synchronization without internet-connected clocks
- Providing NTP-synchronized time to external microcontrollers via I2C (DS1307 emulation)

### 1.3 Background

DCF77 is a German longwave time signal broadcast from Mainflingen, Germany. Many European radio-controlled clocks use this signal for automatic time synchronization. This emulator replicates the signal locally using NTP as the time source.

---

## 2. Hardware Requirements

### 2.1 Supported Platforms

| Platform | Board | Status |
|----------|-------|--------|
| ESP8266 | WeMos D1 Mini | Primary target |
| ESP8266 | ESP-01S (1MB) | Supported |

### 2.2 Pin Configuration

| Function | Pin (D1 Mini) | Pin (ESP-01S) | Description |
|----------|---------------|---------------|-------------|
| DCF77 Output | D4 / GPIO2 | GPIO2 | Time signal output |
| I2C SDA | D2 / GPIO4 | N/A | DS1307 emulation data |
| I2C SCL | D1 / GPIO5 | N/A | DS1307 emulation clock |
| Available | D3 / GPIO0 | GPIO0 | Reserved for future use |

Note: I2C DS1307 emulation is only available on D1 Mini due to pin availability.

### 2.3 Signal Output Modes

The output signal polarity is configurable via the web interface:

| Mode | `pause` | `sig` | Compatible Modules |
|------|---------|-------|-------------------|
| Non-inverted (default) | 0 (LOW) | 1 (HIGH) | ELV modules |
| Inverted | 1 (HIGH) | 0 (LOW) | Pollin modules |

### 2.4 Programming Interface (ESP-01S)

```
FTDI232    ESP-01S
-------    -------
VCC   -->  3.3V
VCC   -->  EN (CH_PD)
GND   -->  GND
GND   -->  GPIO0 (for programming mode)
RX    -->  TX
TX    -->  RX
```

---

## 3. Software Architecture

### 3.1 Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| ESP8266WiFi | Built-in | WiFi connectivity |
| WiFiManager | 2.x | Captive portal for WiFi configuration |
| TimeLib | 1.6+ | Time calculations and conversions |
| Ticker | Built-in | 100ms timer interrupt for signal generation |
| WiFiUdp | Built-in | NTP packet communication |
| Wire | Built-in | I2C communication for DS1307 emulation |
| ESP8266WebServer | Built-in | Web configuration interface |
| EEPROM | Built-in | Persistent configuration storage |

### 3.2 Build Configuration

```ini
[env:d1_mini]
platform = espressif8266
board = d1_mini
framework = arduino
monitor_speed = 115200
```

### 3.3 Module Structure

NTP synchronization and DCF77 transmission operate on independent timers:

```
+------------------------------------------------------+
|                     Main Loop                         |
|                                                      |
|  +-- NTP Timer (config.ntpInterval, default 7201s) --+
|  |                                                   |
|  v                                                   |
|  +------------------+                                |
|  |  syncNTP()       |     +-------------------+      |
|  |  - NTP query     |     |  DS1307 Emulation |      |
|  |  - DS1307 sync   |     |  - I2C slave      |      |
|  +------------------+     |  - Register map   |      |
|                           |  - Live time      |      |
|  +-- DCF Timer (300s) ---+-------------------+      |
|  |                                ^                  |
|  v                                |                  |
|  +------------------------+  I2C requests from       |
|  | startDcfTransmission() |  external devices        |
|  | - Read system clock    |                          |
|  | - Array building       |                          |
|  +--------+---------------+                          |
|           |                                          |
|           v                                          |
|  +--------+----------+                               |
|  |  CalculateArray    |                               |
|  |  - DCF77 encoding  |                               |
|  |  - Parity bits     |                               |
|  +--------+----------+                               |
|           |                                          |
|           v                                          |
|  +--------+----------+                               |
|  |  DcfOut (100ms)    |                               |
|  |  - Ticker ISR      |                               |
|  |  - Pulse output    |                               |
|  +-------------------+                               |
+------------------------------------------------------+
```

---

## 4. Functional Specification

### 4.1 Startup Sequence

1. Initialize serial port at 115200 baud
2. Load configuration from EEPROM (or apply defaults)
3. Configure GPIO2 as output (LOW initially)
4. Attach 100ms ticker interrupt for DCF77 output
5. Initialize pulse array with frame markers
6. Start WiFiManager for network configuration
7. Configure timezone using POSIX TZ string from config
8. Start web server for configuration
9. Initialize DS1307 I2C slave emulation at address 0x68
10. Perform initial NTP sync
11. Start first DCF77 transmission immediately (if NTP sync succeeded)
12. Enter main loop (subsequent DCF transmissions every 5 minutes)

### 4.2 WiFi Configuration

On first boot or when no stored credentials exist:

1. Device creates access point named `Ntp2DCF`
2. User connects to AP (default IP: 192.168.4.1)
3. Captive portal presents network selection
4. Credentials are stored in flash memory
5. Device reboots and connects as WiFi client

### 4.3 NTP Synchronization Cycle

NTP sync runs independently at the configured interval (default 7201 seconds):

1. Resolve NTP server hostname (from config)
2. Send NTP request packet to port 123
3. Wait 1 second for response
4. Parse 48-byte NTP response
5. Convert NTP timestamp to Unix time
6. Apply timezone and DST via POSIX TZ string (automatic)
7. Log synced time
8. Update DS1307 emulation registers

### 4.4 DCF77 Transmission Cycle

DCF transmission runs independently every 5 minutes (`DCF_TRANSMIT_INTERVAL = 300s`):

1. Read current time from system clock (`time(nullptr)`)
2. Validate system clock is set (year > 2020)
3. If near minute boundary (>56s), wait for next minute
4. Add 2-minute offset for DCF77 protocol
5. Generate 3-minute pulse array
6. Wait until second 58 of current minute
7. Begin 3-minute transmission
8. Wait for transmission to complete (~150 seconds)

### 4.5 Error Handling

| Condition | Action |
|-----------|--------|
| No NTP response | Retry at next NTP interval, reconnect WiFi after 3 failures |
| WiFi disconnected | Attempt reconnection, restart ESP after 10s timeout |
| System clock not set | Skip DCF transmission, retry at next DCF interval |
| Near minute boundary (>56s) | Wait for next minute, then proceed with transmission |

---

## 5. DCF77 Protocol Implementation

### 5.1 Signal Characteristics

| Parameter | Value |
|-----------|-------|
| Carrier frequency | N/A (baseband signal only) |
| Bit rate | 1 bit per second |
| Frame length | 59 bits (60th second has no pulse) |
| Logic 0 | 100ms pulse reduction |
| Logic 1 | 200ms pulse reduction |

### 5.2 Pulse Timing

Each second is divided into 10 x 100ms intervals:

```
Logic 0 (100ms pulse):
|████████__| (pulse for 100ms, then signal for 900ms)
 0  1  2  3  4  5  6  7  8  9  (100ms intervals)

Logic 1 (200ms pulse):
|████████████____| (pulse for 200ms, then signal for 800ms)
 0  1  2  3  4  5  6  7  8  9

No pulse (minute marker):
|__________| (signal for entire second)
```

### 5.3 Frame Structure

| Bit(s) | Content | Encoding |
|--------|---------|----------|
| 0 | Start of minute | Always 0 |
| 1-14 | Weather/Civil warnings | Not implemented (0) |
| 15 | Call bit | Not implemented (0) |
| 16 | Summer time announcement | Not implemented (0) |
| 17 | CEST (summer time) | 1 if DST active |
| 18 | CET (winter time) | 1 if DST not active |
| 19 | Leap second announcement | Not implemented (0) |
| 20 | Start of time | Always 1 |
| 21-27 | Minutes (BCD) | 0-59, LSB first |
| 28 | Minutes parity | Even parity |
| 29-34 | Hours (BCD) | 0-23, LSB first |
| 35 | Hours parity | Even parity |
| 36-41 | Day of month (BCD) | 1-31, LSB first |
| 42-44 | Day of week | 1=Mon, 7=Sun |
| 45-49 | Month (BCD) | 1-12, LSB first |
| 50-57 | Year (BCD) | 0-99, LSB first |
| 58 | Date parity | Even parity over bits 36-57 |
| 59 | No pulse | Minute marker |

### 5.4 BCD Encoding

Binary-Coded Decimal with LSB transmitted first:

```
Example: 37 minutes
BCD: 0011 0111 (tens=3, ones=7)
Transmission order (bits 21-27): 1,1,1,0,1,1,0
```

### 5.5 Parity Calculation

Even parity is used:
- Count the number of 1-bits in the data field
- Set parity bit so total number of 1s is even

### 5.6 Weekday Encoding

| Day | DCF77 Value |
|-----|-------------|
| Monday | 1 |
| Tuesday | 2 |
| Wednesday | 3 |
| Thursday | 4 |
| Friday | 5 |
| Saturday | 6 |
| Sunday | 7 |

### 5.7 Three-Minute Transmission

The emulator transmits three complete minutes to ensure reliable clock synchronization:

```
Pulse Array Layout (183 pulses total):
+---+---+------------------------+------------------------+------------------------+---+
| 0 | 1 |     Minute 1 (2-61)    |    Minute 2 (62-121)   |   Minute 3 (122-181)   |182|
+---+---+------------------------+------------------------+------------------------+---+
  |   |                                                                               |
  |   +-- Minute marker (no pulse)                                                    |
  +-- Initial sync pulse                                            Closing pulse ---+
```

---

## 6. Daylight Saving Time

### 6.1 POSIX TZ String Handling

DST transitions are handled automatically by the ESP8266's `configTime()` function using the configured POSIX TZ string. The `localtime_r()` function returns the correct DST state via `tm_isdst`, which is used to set the DCF77 CEST/CET bits.

### 6.2 Rules (Central European Time - default)

| Transition | Date | Time | Action |
|------------|------|------|--------|
| Winter to Summer | Last Sunday of March | 02:00 CET | +1 hour (to CEST) |
| Summer to Winter | Last Sunday of October | 03:00 CEST | -1 hour (to CET) |

The POSIX TZ string `CET-1CEST,M3.5.0,M10.5.0/3` encodes these rules. Other timezones with different DST rules are supported by changing the TZ string via the web interface (see section 10.4).

---

## 7. Configuration Parameters

### 7.1 Compile-Time Constants

| Parameter | Default | Description |
|-----------|---------|-------------|
| `LedPin` | 2 (GPIO2) | DCF77 output pin |
| `localPort` | 2390 | UDP port for NTP |
| `DCF_TRANSMIT_INTERVAL` | 300 (5 min) | DCF77 transmission interval in seconds |

### 7.2 Runtime Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| NTP sync interval | Configurable (600-14400s, default 7201s) | Time between NTP queries |
| DCF transmit interval | 300s (5 minutes) | Time between DCF77 transmissions |
| NTP timeout | 1,000 ms | Wait for NTP response |
| WiFi connect timeout | 10,000 ms | Max wait for connection |
| Max NTP failures | 3 | Before WiFi reconnect |
| Late second threshold | 56 | Wait for next minute if past this second |

---

## 8. Timing Diagram

```
Independent Timer Architecture:

NTP Timer (default 7201s interval):
0s     1s
|------|---------> next sync in ~7201s
  NTP
 query
 + DS1307
  sync

DCF Timer (300s interval):
0s         ~2s                   58s        60s       120s      180s
|----------|---------------------|----------|----------|----------|
 Read clock  Calculate arrays      Wait     Minute 1   Minute 2   Minute 3
 + validate  for 3 minutes                  transmit   transmit   transmit

Note: NTP and DCF timers run independently. DCF reads the system clock
directly (kept accurate by ESP8266's internal SNTP) and does not depend
on the NTP timer having run recently.
```

---

## 9. DS1307 I2C Emulation

### 9.1 Overview

The firmware emulates a DS1307 Real-Time Clock chip, allowing external microcontrollers (Arduino, Raspberry Pi, etc.) to read NTP-synchronized time via I2C. This enables devices without WiFi capability to receive accurate time.

### 9.2 I2C Configuration

| Parameter | Value |
|-----------|-------|
| I2C Address | 0x68 (same as real DS1307) |
| SDA Pin | GPIO4 (D2 on D1 Mini) |
| SCL Pin | GPIO5 (D1 on D1 Mini) |
| Mode | Slave |

### 9.3 Register Map

| Address | Name | Format | Description |
|---------|------|--------|-------------|
| 0x00 | Seconds | BCD | Bits 0-6: seconds (0-59), Bit 7: CH (0=running) |
| 0x01 | Minutes | BCD | Minutes (0-59) |
| 0x02 | Hours | BCD | Bits 0-5: hours (0-23), Bit 6: 0 (24-hour mode) |
| 0x03 | Day | 1-7 | Day of week (1=Sunday, 7=Saturday) |
| 0x04 | Date | BCD | Day of month (1-31) |
| 0x05 | Month | BCD | Month (1-12) |
| 0x06 | Year | BCD | Year (00-99, representing 2000-2099) |
| 0x07 | Control | - | Control register (unused) |
| 0x08-0x3F | RAM | - | 56 bytes RAM (unused) |

### 9.4 Usage with RTClib

External devices can read time using standard DS1307 libraries:

```cpp
#include <RTClib.h>
#include <Wire.h>

RTC_DS1307 rtc;

void setup() {
    Wire.begin();
    rtc.begin();  // Connects to NTP2DCF at address 0x68
}

void loop() {
    DateTime now = rtc.now();
    // now contains NTP-synchronized time
}
```

### 9.5 Live Time Updates

The emulator maintains accurate time between NTP syncs by:
1. Storing base time values when NTP sync occurs
2. Tracking elapsed milliseconds since last sync
3. Calculating current time on each I2C read request

This ensures sub-second accuracy for time queries.

### 9.6 Limitations

- Write operations to time registers are accepted but ignored (time comes from NTP)
- RAM registers (0x08-0x3F) are functional for read/write
- Only 24-hour mode is supported
- Date rollover between NTP syncs is not handled (acceptable given typical sync intervals)

---

## 10. Web Configuration Interface

### 10.1 Overview

The firmware includes a built-in web server that provides runtime configuration without recompilation. Access the interface by navigating to the device's IP address in a web browser.

### 10.2 Web Server Configuration

| Parameter | Value |
|-----------|-------|
| Port | 80 (HTTP) |
| URL | `http://<device-ip>/` |

### 10.3 Configuration Options

| Setting | Description | Default | Range |
|---------|-------------|---------|-------|
| NTP Server | NTP pool hostname | `0.de.pool.ntp.org` | Any valid hostname |
| Timezone | POSIX TZ string | `CET-1CEST,M3.5.0,M10.5.0/3` | Valid POSIX TZ |
| NTP Interval | Sync frequency | 7201 seconds | 600-14400 seconds |
| DCF77 Signal Polarity | Output signal mode | Non-inverted | Non-inverted (ELV) / Inverted (Pollin) |

### 10.4 POSIX Timezone String Format

The timezone is configured using POSIX TZ strings, which automatically handle DST transitions.

**Format:** `STD offset [DST[offset],start[/time],end[/time]]`

**Common Examples:**

| Region | POSIX TZ String |
|--------|-----------------|
| Central Europe (CET/CEST) | `CET-1CEST,M3.5.0,M10.5.0/3` |
| UK (GMT/BST) | `GMT0BST,M3.5.0/1,M10.5.0` |
| US Eastern (EST/EDT) | `EST5EDT,M3.2.0,M11.1.0` |
| US Pacific (PST/PDT) | `PST8PDT,M3.2.0,M11.1.0` |
| Japan (JST, no DST) | `JST-9` |
| Australia Eastern | `AEST-10AEDT,M10.1.0,M4.1.0/3` |

### 10.5 Status Display

The web interface displays a compact real-time status card:

| Status Item | Description |
|-------------|-------------|
| Current Time | Local time with timezone (top left, counts up between syncs) |
| Next DCF | Countdown to next DCF77 transmission (top right); shows "Now" during transmission, "Due" when overdue, or countdown like "3m 45s" |
| DCF77 | Transmitting or Idle |
| Transmitting | Time being transmitted and pulse number (e.g., "14:30 (45)") |
| NTP Sync | Time since last successful sync |
| WiFi | Connected SSID and signal strength (RSSI in dBm) |

The footer displays the firmware version, device IP address, and uptime.

### 10.6 API Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/` | GET | Main configuration page |
| `/status` | GET | JSON status data |
| `/config` | GET | JSON current configuration |
| `/config` | POST | Save new configuration |
| `/ntp-refresh` | POST | Trigger immediate NTP sync |

#### `/status` Response Fields

| Field | Type | Description |
|-------|------|-------------|
| `uptime` | int | Seconds since boot |
| `ssid` | string | Connected WiFi network name |
| `rssi` | int | WiFi signal strength in dBm |
| `lastSyncAgo` | int | Seconds since last NTP sync (-1 if never) |
| `currentTime` | string | Local time with timezone (e.g., "14:30:56 CET") |
| `dcfTime` | string | Time being transmitted and pulse count (e.g., "14:30 (45)") |
| `dcfActive` | bool | True if DCF77 transmission is in progress |
| `nextDcfIn` | int | Seconds until next DCF transmission (-1 if transmitting, 0 if due) |
| `ip` | string | Device IP address |

### 10.7 Configuration Storage

- Configuration is stored in EEPROM (256 bytes)
- Changes persist across reboots
- Invalid or corrupted config automatically resets to defaults

---

## 11. Serial Debug Output

Baud rate: 115200

```
INIT DCF77 emulator V 1.8

Config: Loaded from EEPROM
  NTP Server: 0.de.pool.ntp.org
  Timezone: CET-1CEST,M3.5.0,M10.5.0/3
  Sync Interval: 7201s
  DCF Signal: Non-inverted

WiFi: Starting WiFiManager
WiFi: Connected to MyNetwork
Web server started at http://192.168.1.100
I2C: DS1307 emulation active at 0x68

Startup complete

NTP: Querying 0.de.pool.ntp.org (192.53.103.108)
NTP: Synced - 14:30:56 28.01.2026 CET
DCF: Preparing transmission at 14:30:56 CET
DCF: Waiting 22s for minute boundary
DCF: Starting transmission (14:33 - 14:35)
DCF: Transmission complete (14:33 - 14:35)

... (5-minute DCF interval, independent NTP interval) ...

DCF: Preparing transmission at 14:35:01 CET
DCF: Waiting 57s for minute boundary
DCF: Starting transmission (14:38 - 14:40)
DCF: Transmission complete (14:38 - 14:40)

NTP: Querying 0.de.pool.ntp.org (192.53.103.108)
NTP: Synced - 16:31:17 28.01.2026 CET
```

---

## 12. Known Limitations

### 12.1 Timing Accuracy

- NTP network latency is not compensated
- Sub-second accuracy is not guaranteed
- Clock drift between NTP queries is not corrected

### 12.2 DST Transition

- If transmission spans DST change time, all three minutes use the same DST state
- Exact transition at 02:00/03:00 is not implemented
- Clocks may show incorrect time until 03:03 on transition day

### 12.3 Protocol Completeness

The following DCF77 features are NOT implemented:
- Weather information (bits 1-14)
- Call bit (bit 15)
- DST change announcement (bit 16)
- Leap second announcement (bit 19)
- Phase modulation (pseudo-random sequence)

### 12.4 Hardware Limitations

- Output is baseband digital signal only
- No 77.5 kHz carrier modulation
- External circuitry required for RF transmission (if needed)
- Single output pin (no complementary output)

---

## 13. Testing

### 13.1 Unit Tests

#### DCF77 Tests (`test/test_dcf77/test_main.cpp`)

| Test Category | Count | Coverage |
|---------------|-------|----------|
| Bin2Bcd conversion | 3 | Full range |
| Weekday conversion | 2 | All days + bug verification |
| DST calculation | 6 | All months + transitions |
| DCF77 encoding | 12 | All fields + parity |
| Edge cases | 4 | Midnight, 23:59, leap year |
| **Total** | **27** | |

#### DS1307 Emulation Tests (`test/test_ds1307/test_ds1307.cpp`)

| Test Category | Count | Coverage |
|---------------|-------|----------|
| BCD conversion (toBCD) | 8 | All time fields |
| BCD conversion (fromBCD) | 3 | Inverse conversion + roundtrip |
| Register format | 3 | Seconds, hours, day-of-week |
| Boundary values | 4 | Midnight, end-of-day, year boundaries |
| Address pointer | 2 | Wraparound, sequential increment |
| Register constants | 3 | Addresses, I2C address, count |
| **Total** | **23** | |

#### Configuration Storage Tests (`test/test_config/test_config.cpp`)

| Test Category | Count | Coverage |
|---------------|-------|----------|
| Structure validation | 4 | Size, magic, version, field sizes |
| Default values | 3 | NTP server, timezone, interval |
| String lengths | 2 | TZ string, NTP server fit buffers |
| Interval validation | 2 | Min/max range |
| EEPROM constraints | 2 | Size, config fits |
| Example validation | 2 | Common TZ strings, NTP servers |
| **Total** | **15** | |

### 13.2 Running Tests

```bash
pio test -e native
```

---

## 14. Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | Dec 2015 | Initial release (Fuso68) |
| 1.1 | Dec 2015 | WiFi reconnection logic |
| 1.2 | Jun 2021 | WiFiManager integration |
| 1.3 | Dec 2022 | CLT2 adaptation |
| 1.4 | Dec 2022 | Minor fixes |
| 1.5 | Jan 2025 | Fixed weekday encoding bug, fixed timing calculation bug, removed unused code, added unit tests |
| 1.6 | Jan 2025 | Added DS1307 I2C slave emulation for external time access |
| 1.7 | Jan 2025 | Added web configuration interface with POSIX timezone support |
| 1.8 | Jan 2026 | Decoupled NTP sync and DCF77 transmission into independent timers; DCF transmits every 5 minutes using system clock; NTP syncs at configurable interval; immediate DCF transmission after startup NTP sync; compact status card with next-DCF countdown; added pulse count to web status; added NTP refresh button; documented `/status` API response fields; near-minute-boundary waits instead of skipping |

---

## 15. References

1. [DCF77 Wikipedia](https://en.wikipedia.org/wiki/DCF77)
2. [PTB DCF77 Official](https://www.ptb.de/cms/en/ptb/fachabteilungen/abt4/fb-44/ag-442/dissemination-of-legal-time/dcf77.html)
3. [Elektor DCF77 Emulator Project](https://www.elektormagazine.com/labs/dcf77-emulator-with-esp8266-elektor-labs-version-150713)
4. [NTP Protocol (RFC 5905)](https://tools.ietf.org/html/rfc5905)
5. [ESP8266 Arduino Core](https://arduino-esp8266.readthedocs.io/)
6. [DS1307 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds1307.pdf)
7. [RTClib Arduino Library](https://github.com/adafruit/RTClib)
