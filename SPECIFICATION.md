# NTP2DCF Specification

## Document Information

| Field | Value |
|-------|-------|
| Version | 1.7 |
| Date | January 2025 |
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

The output signal polarity is configurable at compile time:

| Mode | `pause` | `sig` | Compatible Modules |
|------|---------|-------|-------------------|
| Not Inverted (default) | 0 (LOW) | 1 (HIGH) | ELV modules |
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

```
+-------------------+
|    Main Loop      |
|  (60s interval)   |
+--------+----------+
         |
         v
+--------+----------+
|  ReadAndDecodeTime |
|  - NTP query       |
|  - DST calculation |
|  - Array building  |
|  - DS1307 sync     |
+--------+----------+
         |
         v
+--------+----------+     +-------------------+
|  CalculateArray    |     |  DS1307 Emulation |
|  - DCF77 encoding  |     |  - I2C slave      |
|  - Parity bits     |     |  - Register map   |
+--------+----------+     |  - Live time      |
         |                 +-------------------+
         v                         ^
+--------+----------+              |
|  DcfOut (100ms)    |     I2C requests from
|  - Ticker ISR      |     external devices
|  - Pulse output    |
+-------------------+
```

---

## 4. Functional Specification

### 4.1 Startup Sequence

1. Initialize serial port at 115200 baud
2. Configure GPIO2 as output (LOW initially)
3. Attach 100ms ticker interrupt for DCF77 output
4. Initialize pulse array with frame markers
5. Start WiFiManager for network configuration
6. Initialize DS1307 I2C slave emulation at address 0x68
7. Enter main loop upon WiFi connection

### 4.2 WiFi Configuration

On first boot or when no stored credentials exist:

1. Device creates access point named `Ntp2DCF`
2. User connects to AP (default IP: 192.168.4.1)
3. Captive portal presents network selection
4. Credentials are stored in flash memory
5. Device reboots and connects as WiFi client

### 4.3 Time Synchronization Cycle

Every 60 seconds (approximately):

1. Resolve NTP server hostname (`0.de.pool.ntp.org`)
2. Send NTP request packet to port 123
3. Wait 1 second for response
4. Parse 48-byte NTP response
5. Convert NTP timestamp to Unix time
6. Apply timezone offset (CET = UTC+1)
7. Calculate daylight saving time status
8. Apply DST offset if applicable (+1 hour)
9. Add 2-minute offset for DCF77 protocol
10. Generate 3-minute pulse array
11. Wait until second 58 of current minute
12. Begin transmission

### 4.4 Error Handling

| Condition | Action |
|-----------|--------|
| No NTP response | Retry next minute, reconnect WiFi after 3 failures |
| WiFi disconnected | Attempt reconnection, restart ESP after 10s timeout |
| Late in minute (>56s) | Skip transmission, retry in 30 seconds |

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

### 6.1 Rules (Central European Time)

| Transition | Date | Time | Action |
|------------|------|------|--------|
| Winter to Summer | Last Sunday of March | 02:00 CET | +1 hour (to CEST) |
| Summer to Winter | Last Sunday of October | 03:00 CEST | -1 hour (to CET) |

### 6.2 Algorithm

```
if (month >= 4 AND month <= 9):
    DST = summer time
else if (month == 3 AND day >= 25):
    if (days until next Sunday >= days until month end):
        DST = summer time
else if (month == 10):
    DST = summer time (default)
    if (day >= 25):
        if (days until next Sunday >= days until month end):
            DST = winter time
else:
    DST = winter time
```

---

## 7. Configuration Parameters

### 7.1 Compile-Time Constants

| Parameter | Default | Description |
|-----------|---------|-------------|
| `LedPin` | 2 (GPIO2) | DCF77 output pin |
| `localPort` | 2390 | UDP port for NTP |
| `ntpServerName` | "0.de.pool.ntp.org" | NTP server pool |
| `timeZone` | 1 | UTC offset (hours) |
| `pause` | 0 | Signal level during pause |
| `sig` | 1 | Signal level during active |

### 7.2 Runtime Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Main loop interval | 60,000 ms | Time between NTP queries |
| NTP timeout | 1,000 ms | Wait for NTP response |
| WiFi connect timeout | 10,000 ms | Max wait for connection |
| Max NTP failures | 3 | Before WiFi reconnect |
| Late second threshold | 56 | Skip if past this second |

---

## 8. Timing Diagram

```
Main Loop Timing (successful transmission):

0s     1s                    58s        60s       120s      180s      210s
|------|---------------------|----------|----------|----------|---------|
  NTP     Calculate arrays      Wait     Minute 1   Minute 2   Minute 3  Safety
 query    for 3 minutes                  transmit   transmit   transmit   wait
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
- Date rollover between NTP syncs is not handled (acceptable for 60-second sync interval)

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
| NTP Interval | Sync frequency | 60 seconds | 60-3600 seconds |

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

The web interface displays real-time status information:

| Status Item | Description |
|-------------|-------------|
| Uptime | Time since device boot |
| WiFi Signal | RSSI in dBm |
| Last NTP Sync | Time since last successful sync |
| Current Time | Local time with timezone |
| DCF77 Status | Transmitting or Idle |

### 10.6 API Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/` | GET | Main configuration page |
| `/status` | GET | JSON status data |
| `/config` | GET | JSON current configuration |
| `/config` | POST | Save new configuration |

### 10.7 Configuration Storage

- Configuration is stored in EEPROM (256 bytes)
- Changes persist across reboots
- Invalid or corrupted config automatically resets to defaults

---

## 11. Serial Debug Output

Baud rate: 115200

```
INIT DCF77 emulator V 1.7

Config: Loaded from EEPROM
  NTP Server: 0.de.pool.ntp.org
  Timezone: CET-1CEST,M3.5.0,M10.5.0/3
  Sync Interval: 60s

Starting WiFi-Manager

WiFi connected
Timezone configured: CET-1CEST,M3.5.0,M10.5.0/3
Web server started at http://192.168.1.100

Startup complete
DS1307 I2C emulation active at address 0x68
Starting UDP
Local port: 2390
TimeServerIP: 192.53.103.108
Sending NTP packet...
NTP packet received, length=48
Seconds since 1900 = 3944123456
Unix time (UTC) = 1735134656
Local time: 25.12.2024 DST=0 14:30:56
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

---

## 15. References

1. [DCF77 Wikipedia](https://en.wikipedia.org/wiki/DCF77)
2. [PTB DCF77 Official](https://www.ptb.de/cms/en/ptb/fachabteilungen/abt4/fb-44/ag-442/dissemination-of-legal-time/dcf77.html)
3. [Elektor DCF77 Emulator Project](https://www.elektormagazine.com/labs/dcf77-emulator-with-esp8266-elektor-labs-version-150713)
4. [NTP Protocol (RFC 5905)](https://tools.ietf.org/html/rfc5905)
5. [ESP8266 Arduino Core](https://arduino-esp8266.readthedocs.io/)
6. [DS1307 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds1307.pdf)
7. [RTClib Arduino Library](https://github.com/adafruit/RTClib)
