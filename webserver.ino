void handleStatusRoot()
{
    String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta http-equiv='refresh' content='10'>"
                     "<title>P1 Meter Status</title>"
                     "<style>body{font-family:sans-serif;margin:2em;}table{border-collapse:collapse;margin-bottom:1.5em;}"
                     "td,th{border:1px solid #ccc;padding:4px 8px;text-align:left;}</style></head><body>");

    html += "<h1>P1 Meter Status</h1>";

    html += "<h2>Connection</h2><table>";
    if (WiFi.getMode() == WIFI_AP)
    {
        html += "<tr><td>WiFi</td><td>AP fallback active - could not join \"" + String(WIFI_SSID) +
                "\" (AP IP: " + WiFi.softAPIP().toString() + ")</td></tr>";
    }
    else
    {
        html += "<tr><td>WiFi</td><td>" + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") +
                " (" + WiFi.SSID() + ", " + WiFi.localIP().toString() + ", RSSI " + String(WiFi.RSSI()) + " dBm)</td></tr>";
    }
    html += "<tr><td>MQTT</td><td>" + String(mqttClient.connected() ? "Connected" : "Disconnected") + "</td></tr>";
    html += "<tr><td>Uptime</td><td>" + String(millis() / 1000) + " s</td></tr>";
    html += "</table>";

    html += "<h2>P1 Serial</h2><table>";
    html += "<tr><td>Lines received</td><td>" + String(P1_LINES_RECEIVED) + "</td></tr>";
    html += "<tr><td>Telegram starts seen ('/')</td><td>" + String(P1_TELEGRAM_START_COUNT) + "</td></tr>";
    html += "<tr><td>CRC valid</td><td>" + String(P1_CRC_VALID_COUNT) + "</td></tr>";
    html += "<tr><td>CRC invalid</td><td>" + String(P1_CRC_INVALID_COUNT) + "</td></tr>";
    html += "<tr><td>Last line age</td><td>" +
            (P1_LINES_RECEIVED > 0 ? String((millis() - LAST_P1_LINE_MILLIS) / 1000) + " s ago" : String("never")) +
            "</td></tr>";

    // Raw bytes rendered as hex + a safe (non-UTF-8-breaking) ASCII preview,
    // since garbled/non-ASCII bytes displayed directly corrupt the page.
    String hexDump = "";
    String asciiDump = "";
    for (unsigned int i = 0; i < LAST_P1_LINE_LEN; i++)
    {
        uint8_t b = (uint8_t)LAST_P1_LINE[i];
        char hexByte[4];
        snprintf(hexByte, sizeof(hexByte), "%02X ", b);
        hexDump += hexByte;
        asciiDump += (b >= 32 && b < 127) ? String((char)b) : ".";
    }
    html += "<tr><td>Last line (hex)</td><td><code>" + hexDump + "</code></td></tr>";
    html += "<tr><td>Last line (ascii)</td><td><code>" + asciiDump + "</code></td></tr>";
    html += "</table>";

    html += "<h2>Last Full Telegram</h2><table>";
    html += "<tr><td>CRC</td><td>" + String(LAST_FULL_TELEGRAM_LEN > 0 ? (LAST_FULL_TELEGRAM_CRC_VALID ? "valid" : "INVALID") : "none yet") + "</td></tr>";
    html += "<tr><td>Size</td><td>" + String(LAST_FULL_TELEGRAM_LEN) + " bytes</td></tr>";
    html += "</table>";

    // Non-printable bytes become '.', CR/LF are kept as real line breaks so
    // the pasted-out text still reads as a normal multi-line P1 telegram.
    String telegramDump = "";
    for (unsigned int i = 0; i < LAST_FULL_TELEGRAM_LEN; i++)
    {
        uint8_t b = (uint8_t)LAST_FULL_TELEGRAM[i];
        if (b == '\r')
            continue;
        if (b == '\n')
            telegramDump += '\n';
        else if (b >= 32 && b < 127)
            telegramDump += (char)b;
        else
            telegramDump += '.';
    }
    html += "<pre style='background:#f0f0f0;padding:1em;overflow-x:auto;'>" + telegramDump + "</pre>";

    html += "<h2>Readouts</h2><table><tr><th>Name</th><th>Value</th></tr>";
    for (int i = 0; i < NUMBER_OF_READOUTS; i++)
    {
        html += "<tr><td>" + telegramObjects[i].name + "</td><td>" + String(telegramObjects[i].value) + "</td></tr>";
    }
    html += "</table>";

    html += "<form method='POST' action='/restart' onsubmit=\"return confirm('Biztosan újraindítod az eszközt?');\">"
            "<button type='submit' style='padding:0.5em 1em;'>Újraindítás</button></form>";

    html += "</body></html>";

    statusServer.send(200, "text/html", html);
}

void handleRestart()
{
    statusServer.send(200, "text/plain", "Restarting...");
    delay(200); // let the response actually get flushed to the client first
    ESP.restart();
}

void setupWebServer()
{
    statusServer.on("/", handleStatusRoot);
    statusServer.on("/restart", HTTP_POST, handleRestart);
    statusServer.begin();
}
