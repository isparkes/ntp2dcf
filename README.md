# NTP2DCF

A DCF77 radio time signal emulator for the ESP8266. Fetches accurate time from NTP servers via WiFi and generates a DCF77-compatible signal to synchronize radio-controlled clocks.

## Features

- **DCF77 signal generation** -- 3-minute pulse train every 5 minutes, compatible with standard radio-controlled clocks
- **NTP time sync** -- configurable sync interval (default 2 hours), automatic DST handling via POSIX timezone strings
- **Web configuration** -- built-in web interface for NTP server, timezone, sync interval, and signal polarity settings
- **DS1307 I2C emulation** -- acts as a DS1307 RTC slave at address 0x68, allowing external microcontrollers to read NTP-synchronized time
- **Configurable signal polarity** -- supports both ELV (non-inverted) and Pollin (inverted) DCF77 receiver modules

## Hardware

| Board | Status |
|-------|--------|
| WeMos D1 Mini | Primary target |
| ESP-01S (1MB) | Supported |

### Pin Configuration (D1 Mini)

| Function | Pin | Description |
|----------|-----|-------------|
| DCF77 Output | D4 / GPIO2 | Time signal output |
| I2C SDA | D2 / GPIO4 | DS1307 emulation data |
| I2C SCL | D1 / GPIO5 | DS1307 emulation clock |

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE plugin)

### Build and Flash

```bash
pio run -e d1_mini -t upload
```

### Monitor Serial Output

```bash
pio device monitor -b 115200
```

### First Boot

1. The device creates a WiFi access point named **Ntp2DCF**
2. Connect to it and configure your WiFi credentials via the captive portal
3. After connecting, the device syncs with NTP and begins DCF77 transmission immediately
4. Open `http://<device-ip>/` in a browser to configure NTP server, timezone, sync interval, and signal polarity

## Configuration

All settings are configurable at runtime through the web interface and stored in EEPROM.

| Setting | Default | Range |
|---------|---------|-------|
| NTP Server | `0.de.pool.ntp.org` | Any valid hostname |
| Timezone | `CET-1CEST,M3.5.0,M10.5.0/3` | Any POSIX TZ string |
| NTP Interval | 7201 seconds | 600--14400 seconds |
| Signal Polarity | Non-inverted (ELV) | Non-inverted / Inverted (Pollin) |

### Common Timezone Strings

| Region | POSIX TZ String |
|--------|-----------------|
| Central Europe (CET/CEST) | `CET-1CEST,M3.5.0,M10.5.0/3` |
| UK (GMT/BST) | `GMT0BST,M3.5.0/1,M10.5.0` |
| US Eastern (EST/EDT) | `EST5EDT,M3.2.0,M11.1.0` |
| US Pacific (PST/PDT) | `PST8PDT,M3.2.0,M11.1.0` |
| Japan (JST, no DST) | `JST-9` |
| Australia Eastern | `AEST-10AEDT,M10.1.0,M4.1.0/3` |

## Architecture

NTP synchronization and DCF77 transmission run on independent timers:

- **NTP timer** -- queries the NTP server at the configured interval (default 7201s) and updates the system clock and DS1307 registers
- **DCF timer** -- every 5 minutes, reads the system clock, encodes 3 minutes of DCF77 data, and transmits the pulse train
- **DS1307 emulation** -- responds to I2C requests with the current system time in real-time
- **Web server** -- serves the configuration page and status API, remains responsive during DCF77 transmission

## API

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/` | GET | Configuration page |
| `/status` | GET | JSON status data |
| `/config` | GET | Current configuration (JSON) |
| `/config` | POST | Save new configuration |
| `/ntp-refresh` | POST | Trigger immediate NTP sync |

## Running Tests

```bash
pio test -e native
```

65 unit tests covering DCF77 encoding, DS1307 BCD conversion, and configuration storage validation.

## Documentation

See [SPECIFICATION.md](SPECIFICATION.md) for the full technical specification including DCF77 protocol details, timing diagrams, register maps, and version history.

## Credits

- Original: Fuso68 (2015)
- WiFi config: cactus-online (2021)
- Based on: UDP NTP Client by Michael Margolis, Tom Igoe, Ivan Grokhotkov

## License

Public Domain
