/**
 * Web Server for NTP2DCF Configuration
 *
 * Provides a web-based configuration interface for runtime settings
 * and displays device status information.
 */
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#ifndef UNIT_TEST

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include "config_storage.h"

// =============================================================================
// Global State (shared with main.cpp)
// =============================================================================

// These are defined in main.cpp and shared here
extern unsigned long lastNtpSyncMillis;
extern unsigned long lastNtpCheckMillis;
extern unsigned long lastDcfTransmitMillis;
extern int Dls;
extern int DCFOutputOn;
extern int PulseCount;         // Current position in pulse array (0-182)
extern int DcfTransmitMinute;  // The minute currently being transmitted (0-59)
extern int DcfTransmitHour;    // The hour currently being transmitted (0-23)
extern bool syncNTP();               // Fetch time from NTP server
extern bool startDcfTransmission();  // Start DCF77 transmission from system clock

// =============================================================================
// Web Server Instance
// =============================================================================

inline ESP8266WebServer webServer(80);

// =============================================================================
// HTML Page (stored in PROGMEM to save RAM)
// =============================================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NTP2DCF Configuration</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: #1a1a2e;
            color: #eee;
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
        }
        h1 {
            text-align: center;
            color: #00d9ff;
            margin-bottom: 20px;
            font-size: 1.8em;
        }
        .card {
            background: #16213e;
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
        }
        .card h2 {
            color: #00d9ff;
            font-size: 1.2em;
            margin-bottom: 15px;
            border-bottom: 1px solid #0f3460;
            padding-bottom: 10px;
        }
        .status-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }
        .status-item {
            background: #0f3460;
            padding: 12px;
            border-radius: 8px;
        }
        .status-item .label {
            font-size: 0.85em;
            color: #888;
            margin-bottom: 4px;
        }
        .status-item .value {
            font-size: 1.1em;
            font-weight: 500;
            color: #00d9ff;
        }
        .status-item.full-width {
            grid-column: 1 / -1;
        }
        .form-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            margin-bottom: 6px;
            color: #aaa;
            font-size: 0.9em;
        }
        input[type="text"], input[type="number"] {
            width: 100%;
            padding: 12px;
            border: 1px solid #0f3460;
            border-radius: 8px;
            background: #0f3460;
            color: #fff;
            font-size: 1em;
        }
        input:focus {
            outline: none;
            border-color: #00d9ff;
        }
        .hint {
            font-size: 0.8em;
            color: #666;
            margin-top: 4px;
        }
        button {
            width: 100%;
            padding: 14px;
            background: #00d9ff;
            color: #1a1a2e;
            border: none;
            border-radius: 8px;
            font-size: 1em;
            font-weight: 600;
            cursor: pointer;
            transition: background 0.2s;
        }
        button:hover {
            background: #00b8d9;
        }
        button:disabled {
            background: #555;
            cursor: not-allowed;
        }
        .message {
            padding: 12px;
            border-radius: 8px;
            margin-bottom: 15px;
            display: none;
        }
        .message.success {
            background: #1b4332;
            color: #95d5b2;
            display: block;
        }
        .message.error {
            background: #4a1515;
            color: #f5a5a5;
            display: block;
        }
        .dcf-active {
            color: #4ade80 !important;
        }
        .dcf-idle {
            color: #fbbf24 !important;
        }
        @media (max-width: 480px) {
            .status-grid {
                grid-template-columns: 1fr;
            }
            body { padding: 10px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>NTP2DCF Configuration</h1>

        <div class="card">
            <h2>Status</h2>
            <div class="status-grid">
                <div class="status-item full-width">
                    <div style="display: flex; justify-content: space-between; align-items: baseline;">
                        <div>
                            <div class="label">Current Time</div>
                            <div class="value" id="currentTime">--</div>
                        </div>
                        <div style="text-align: right;">
                            <div class="label">Next DCF</div>
                            <div class="value" id="nextDcf">--</div>
                        </div>
                    </div>
                </div>
                <div class="status-item">
                    <div class="label">DCF77</div>
                    <div class="value" id="dcfStatus">--</div>
                </div>
                <div class="status-item">
                    <div class="label">Transmitting</div>
                    <div class="value" id="dcfTime">--</div>
                </div>
                <div class="status-item">
                    <div class="label">NTP Sync</div>
                    <div class="value" id="lastSync">--</div>
                </div>
                <div class="status-item">
                    <div class="label">WiFi</div>
                    <div class="value"><span id="ssid">--</span> <span id="rssi" style="font-size:0.8em;color:#888;">--</span></div>
                </div>
            </div>
            <div style="display: flex; gap: 10px; margin-top: 15px;">
                <button type="button" id="ntpRefreshBtn" style="flex:1;">Refresh NTP Now</button>
            </div>
        </div>

        <div class="card">
            <h2>Configuration</h2>
            <div id="message" class="message"></div>
            <form id="configForm">
                <div class="form-group">
                    <label for="ntpServer">NTP Server</label>
                    <input type="text" id="ntpServer" name="ntpServer" maxlength="63" placeholder="0.de.pool.ntp.org">
                    <div class="hint">e.g., pool.ntp.org, time.google.com, 0.de.pool.ntp.org</div>
                </div>
                <div class="form-group">
                    <label for="tzPosix">Timezone (POSIX format)</label>
                    <input type="text" id="tzPosix" name="tzPosix" maxlength="47" placeholder="CET-1CEST,M3.5.0,M10.5.0/3">
                    <div class="hint">
                        Examples: CET-1CEST,M3.5.0,M10.5.0/3 (Central Europe),
                        GMT0BST,M3.5.0/1,M10.5.0 (UK),
                        EST5EDT,M3.2.0,M11.1.0 (US Eastern)
                    </div>
                </div>
                <div class="form-group">
                    <label for="ntpInterval">NTP Sync Interval (seconds)</label>
                    <input type="number" id="ntpInterval" name="ntpInterval" min="600" max="14400" step="1" placeholder="7201">
                    <div class="hint">Range: 600-14400 seconds (10 minutes to 4 hours)</div>
                </div>
                <div class="form-group">
                    <label for="dcfInverted">DCF77 Signal Polarity</label>
                    <select id="dcfInverted" name="dcfInverted" style="width: 100%; padding: 12px; border: 1px solid #0f3460; border-radius: 8px; background: #0f3460; color: #fff; font-size: 1em;">
                        <option value="0">Non-inverted (ELV modules)</option>
                        <option value="1">Inverted (Pollin modules)</option>
                    </select>
                    <div class="hint">Match your DCF77 receiver module type</div>
                </div>
                <button type="submit" id="saveBtn">Save Configuration</button>
            </form>
        </div>

        <div class="card" style="text-align: center; padding: 15px;">
            <div style="color: #666; font-size: 0.85em;">
                NTP2DCF v1.8 | <span id="ipAddr">--</span>
            </div>
        </div>
    </div>

    <script>
        function formatUptime(seconds) {
            const d = Math.floor(seconds / 86400);
            const h = Math.floor((seconds % 86400) / 3600);
            const m = Math.floor((seconds % 3600) / 60);
            const s = seconds % 60;
            if (d > 0) return d + 'd ' + h + 'h ' + m + 'm';
            if (h > 0) return h + 'h ' + m + 'm ' + s + 's';
            return m + 'm ' + s + 's';
        }

        function formatTimeSince(seconds) {
            if (seconds < 0) return 'Never';
            if (seconds < 60) return seconds + 's ago';
            if (seconds < 3600) return Math.floor(seconds / 60) + 'm ago';
            return Math.floor(seconds / 3600) + 'h ago';
        }

        function formatNextDcf(seconds) {
            if (seconds < 0) return 'Now';
            if (seconds === 0) return 'Due';
            const m = Math.floor(seconds / 60);
            const s = seconds % 60;
            if (m > 0) return m + 'm ' + s + 's';
            return s + 's';
        }

        async function loadStatus() {
            try {
                const resp = await fetch('/status');
                const data = await resp.json();
                document.getElementById('ssid').textContent = data.ssid;
                document.getElementById('rssi').textContent = data.rssi + ' dBm';
                document.getElementById('lastSync').textContent = formatTimeSince(data.lastSyncAgo);
                document.getElementById('currentTime').textContent = data.currentTime;
                document.getElementById('ipAddr').textContent = data.ip + ' | up ' + formatUptime(data.uptime);

                const dcfEl = document.getElementById('dcfStatus');
                const dcfTimeEl = document.getElementById('dcfTime');
                const nextDcfEl = document.getElementById('nextDcf');
                if (data.dcfActive) {
                    dcfEl.textContent = 'Transmitting';
                    dcfEl.className = 'value dcf-active';
                    dcfTimeEl.textContent = data.dcfTime;
                    dcfTimeEl.className = 'value dcf-active';
                    nextDcfEl.textContent = 'Now';
                    nextDcfEl.className = 'value dcf-active';
                } else {
                    dcfEl.textContent = 'Idle';
                    dcfEl.className = 'value dcf-idle';
                    dcfTimeEl.textContent = '--:-- (---)';
                    dcfTimeEl.className = 'value';
                    nextDcfEl.textContent = formatNextDcf(data.nextDcfIn);
                    nextDcfEl.className = 'value';
                }
            } catch (e) {
                console.error('Status fetch error:', e);
            }
        }

        async function loadConfig() {
            try {
                const resp = await fetch('/config');
                const data = await resp.json();
                document.getElementById('ntpServer').value = data.ntpServer;
                document.getElementById('tzPosix').value = data.tzPosix;
                document.getElementById('ntpInterval').value = data.ntpInterval;
                document.getElementById('dcfInverted').value = data.dcfInverted;
            } catch (e) {
                console.error('Config fetch error:', e);
            }
        }

        function showMessage(text, isError) {
            const msg = document.getElementById('message');
            msg.textContent = text;
            msg.className = 'message ' + (isError ? 'error' : 'success');
            setTimeout(() => { msg.className = 'message'; }, 3000);
        }

        document.getElementById('configForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            const btn = document.getElementById('saveBtn');
            btn.disabled = true;
            btn.textContent = 'Saving...';

            try {
                const formData = new URLSearchParams();
                formData.append('ntpServer', document.getElementById('ntpServer').value);
                formData.append('tzPosix', document.getElementById('tzPosix').value);
                formData.append('ntpInterval', document.getElementById('ntpInterval').value);
                formData.append('dcfInverted', document.getElementById('dcfInverted').value);

                const resp = await fetch('/config', {
                    method: 'POST',
                    body: formData,
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
                });

                if (resp.ok) {
                    showMessage('Configuration saved successfully!', false);
                } else {
                    showMessage('Failed to save configuration', true);
                }
            } catch (e) {
                showMessage('Error: ' + e.message, true);
            }

            btn.disabled = false;
            btn.textContent = 'Save Configuration';
        });

        // NTP Refresh button handler
        document.getElementById('ntpRefreshBtn').addEventListener('click', async () => {
            const btn = document.getElementById('ntpRefreshBtn');
            btn.disabled = true;
            btn.textContent = 'Refreshing...';

            try {
                const resp = await fetch('/ntp-refresh', { method: 'POST' });
                if (resp.ok) {
                    showMessage('NTP refresh started', false);
                } else {
                    showMessage('Failed to start NTP refresh', true);
                }
            } catch (e) {
                showMessage('Error: ' + e.message, true);
            }

            btn.disabled = false;
            btn.textContent = 'Refresh NTP Now';
            loadStatus();
        });

        // Initial load
        loadStatus();
        loadConfig();

        // Auto-refresh status every 5 seconds
        setInterval(loadStatus, 5000);
    </script>
</body>
</html>
)rawliteral";

// =============================================================================
// Request Handlers
// =============================================================================

/**
 * Handle GET / - Serve the main HTML page
 */
inline void handleRoot() {
    webServer.send(200, "text/html", HTML_PAGE);
}

/**
 * Handle GET /status - Return current status as JSON
 */
inline void handleGetStatus() {
    unsigned long uptimeSeconds = millis() / 1000;
    long lastSyncAgo = (lastNtpSyncMillis > 0) ?
        (long)((millis() - lastNtpSyncMillis) / 1000) : -1;

    // Get current time dynamically from system clock (counts up between NTP syncs)
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Format current time string
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d %s",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             Dls ? "CEST" : "CET");

    // Format DCF transmission time with pulse count (only valid when transmitting)
    char dcfTimeStr[32] = "--:-- (---)";
    if (DCFOutputOn) {
        snprintf(dcfTimeStr, sizeof(dcfTimeStr), "%02d:%02d (%d)",
                 DcfTransmitHour, DcfTransmitMinute, PulseCount);
    }

    // Calculate time until next DCF transmission
    long nextDcfIn = -1;  // -1 means transmitting now
    if (DCFOutputOn) {
        nextDcfIn = -1;
    } else {
        unsigned long elapsed = millis() - lastDcfTransmitMillis;
        unsigned long intervalMs = (unsigned long)300 * 1000;  // DCF_TRANSMIT_INTERVAL
        if (elapsed < intervalMs) {
            nextDcfIn = (long)((intervalMs - elapsed) / 1000);
        } else {
            nextDcfIn = 0;  // Due now
        }
    }

    // Build JSON response
    String json = "{";
    json += "\"uptime\":" + String(uptimeSeconds) + ",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"lastSyncAgo\":" + String(lastSyncAgo) + ",";
    json += "\"currentTime\":\"" + String(timeStr) + "\",";
    json += "\"dcfTime\":\"" + String(dcfTimeStr) + "\",";
    json += "\"dcfActive\":" + String(DCFOutputOn ? "true" : "false") + ",";
    json += "\"nextDcfIn\":" + String(nextDcfIn) + ",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += "}";

    webServer.send(200, "application/json", json);
}

/**
 * Handle GET /config - Return current configuration as JSON
 */
inline void handleGetConfig() {
    String json = "{";
    json += "\"ntpServer\":\"" + String(config.ntpServer) + "\",";
    json += "\"tzPosix\":\"" + String(config.tzPosix) + "\",";
    json += "\"ntpInterval\":" + String(config.ntpInterval) + ",";
    json += "\"dcfInverted\":" + String(config.dcfSignalInverted);
    json += "}";

    webServer.send(200, "application/json", json);
}

/**
 * Handle POST /config - Save new configuration
 */
inline void handlePostConfig() {
    bool changed = false;

    if (webServer.hasArg("ntpServer")) {
        String newServer = webServer.arg("ntpServer");
        newServer.trim();
        if (newServer.length() > 0 && newServer.length() < NTP_SERVER_MAX_LEN) {
            strncpy(config.ntpServer, newServer.c_str(), NTP_SERVER_MAX_LEN - 1);
            config.ntpServer[NTP_SERVER_MAX_LEN - 1] = '\0';
            changed = true;
            Serial.print("Config: NTP server changed to ");
            Serial.println(config.ntpServer);
        }
    }

    if (webServer.hasArg("tzPosix")) {
        String newTz = webServer.arg("tzPosix");
        newTz.trim();
        if (newTz.length() > 0 && newTz.length() < TZ_POSIX_MAX_LEN) {
            strncpy(config.tzPosix, newTz.c_str(), TZ_POSIX_MAX_LEN - 1);
            config.tzPosix[TZ_POSIX_MAX_LEN - 1] = '\0';
            changed = true;
            Serial.print("Config: Timezone changed to ");
            Serial.println(config.tzPosix);

            // Apply new timezone immediately
            configTime(config.tzPosix, config.ntpServer);
        }
    }

    if (webServer.hasArg("ntpInterval")) {
        int newInterval = webServer.arg("ntpInterval").toInt();
        if (newInterval >= 600 && newInterval <= 14400) {
            config.ntpInterval = newInterval;
            changed = true;
            Serial.print("Config: NTP interval changed to ");
            Serial.print(config.ntpInterval);
            Serial.println("s");
        }
    }

    if (webServer.hasArg("dcfInverted")) {
        int newInverted = webServer.arg("dcfInverted").toInt();
        if (newInverted == 0 || newInverted == 1) {
            config.dcfSignalInverted = newInverted;
            changed = true;
            Serial.print("Config: DCF signal polarity changed to ");
            Serial.println(config.dcfSignalInverted ? "Inverted" : "Non-inverted");
        }
    }

    if (changed) {
        saveConfig();
        webServer.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No valid parameters\"}");
    }
}

/**
 * Handle POST /ntp-refresh - Trigger an NTP sync
 */
inline void handleNtpRefresh() {
    Serial.println("Web: NTP refresh requested");
    webServer.send(200, "application/json", "{\"status\":\"ok\"}");

    // Trigger NTP sync and reset the check timer
    syncNTP();
    lastNtpCheckMillis = millis();
}

/**
 * Handle 404 - Not Found
 */
inline void handleNotFound() {
    webServer.send(404, "text/plain", "Not Found");
}

// =============================================================================
// Web Server Setup
// =============================================================================

/**
 * Initialize and start the web server
 * Call this after WiFi is connected
 */
inline void setupWebServer() {
    webServer.on("/", HTTP_GET, handleRoot);
    webServer.on("/status", HTTP_GET, handleGetStatus);
    webServer.on("/config", HTTP_GET, handleGetConfig);
    webServer.on("/config", HTTP_POST, handlePostConfig);
    webServer.on("/ntp-refresh", HTTP_POST, handleNtpRefresh);
    webServer.onNotFound(handleNotFound);

    webServer.begin();

    Serial.print("Web server started at http://");
    Serial.println(WiFi.localIP());
}

#endif // UNIT_TEST

#endif // WEB_SERVER_H
