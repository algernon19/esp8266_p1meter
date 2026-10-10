#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>

#include "settings.h"

WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer statusServer(80);

/***********************************
            Main Setup
 ***********************************/
void setup()
{
    // Initialize pins
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // Off (LED_BUILTIN is active low on most ESP8266 boards)
    // P1 data is read on the hardware UART (RX pin) instead of SoftwareSerial:
    // SoftwareSerial's bit-banged timing is unreliable at 115200 baud once
    // WiFi/MQTT/HTTP/OTA compete for CPU time, causing near-constant telegram
    // corruption. The hardware UART has its own clocked receiver, immune to
    // that. Trade-off: this repurposes Serial for P1 reception (RX-only,
    // inverted per the DSMR-style P1 spec), so there's no live USB debug
    // output once the meter is connected - use the HTTP status page instead.
    Serial.begin(BAUD_RATE, SERIAL_8N1, SERIAL_RX_ONLY, 1, true);

#ifdef DEBUG
    Serial.println("Booting - DEBUG mode on");
    blinkLed(2, 100);
    // Quick blink to indicate DEBUG mode without adding meaningful boot delay
    // (this used to take ~8s combined, which matters after a power outage
    // when every second of reconnect time counts).
#endif
    setupDataReadout();

    if (connectWiFiSTA())
    {
        setupOTA();
        mqttClient.setServer(MQTT_HOST, atoi(MQTT_PORT));
        mqttClient.setBufferSize(640);
#ifdef DEBUG
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
#endif
    }
    else
    {
        startAPFallback();
    }

    setupWebServer();
    blinkLed(3, 100); // Quick blink to indicate end of setup
#ifdef DEBUG
    Serial.println("Ready");
#endif
}

/**
 * Tries to join WIFI_SSID for up to WIFI_CONNECT_TIMEOUT milliseconds.
 * Returns true if connected, false if it timed out.
 */
bool connectWiFiSTA()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT)
    {
        delay(250);
    }

    return WiFi.status() == WL_CONNECTED;
}

/**
 * Starts a local AP so the status page (and P1 diagnostics) stay reachable
 * even when WIFI_SSID can't be reached, e.g. due to a weak signal at the
 * meter's install location.
 */
void startAPFallback()
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
#ifdef DEBUG
    Serial.println("Could not join WiFi, started AP fallback:");
    Serial.println(AP_SSID);
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
#endif
}

/***********************************
            Main Loop
 ***********************************/
void loop()
{
    long now = millis();
    statusServer.handleClient();

    if (WiFi.getMode() == WIFI_AP)
    {
        // Running in AP fallback: no MQTT/OTA without the home network, but
        // keep reading P1 data so the status page shows live readings, and
        // retry the home WiFi periodically in case it comes back in range.
        if (now - LAST_RECONNECT_ATTEMPT > WIFI_CONNECT_TIMEOUT)
        {
            LAST_RECONNECT_ATTEMPT = now;
            if (connectWiFiSTA())
            {
                setupOTA();
                mqttClient.setServer(MQTT_HOST, atoi(MQTT_PORT));
                mqttClient.setBufferSize(640);
            }
        }

        readP1Serial();
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        blinkLed(20, 50); // Blink fast to indicate failed WiFi connection
        if (!connectWiFiSTA())
        {
            startAPFallback();
            return;
        }
    }

    ArduinoOTA.handle();

    if (!mqttClient.connected())
    {
        if (now - LAST_RECONNECT_ATTEMPT > 5000)
        {
            LAST_RECONNECT_ATTEMPT = now;

            if (!mqttReconnect())
            {
#ifdef DEBUG
                Serial.println("Connection to MQTT Failed! Rebooting...");
#endif
                delay(5000);
                ESP.restart();
            }
            else
            {
                LAST_RECONNECT_ATTEMPT = 0;
            }
        }
    }
    else
    {
        mqttClient.loop();
    }

    // Drain the P1 serial buffer every loop iteration, not just once per
    // UPDATE_INTERVAL: the meter pushes a full telegram every ~10s, so only
    // reading once a minute lets the SoftwareSerial RX buffer overflow
    // repeatedly between reads, corrupting/truncating telegrams. Publishing
    // to MQTT is still throttled separately by UPDATE_INTERVAL.
    bool freshTelegram = readP1Serial();

    if (freshTelegram && !P1_TOPOLOGY_RESOLVED)
    {
        // Now that we've seen one complete, CRC-valid telegram, we know for
        // certain which fields this meter actually has. Remove any
        // never-seen field's Home Assistant entity in case it was announced
        // unconditionally by an earlier firmware version.
        P1_TOPOLOGY_RESOLVED = true;
        for (int i = 0; i < NUMBER_OF_READOUTS; i++)
        {
            if (!telegramObjects[i].everSeen)
            {
                unpublishHomeAssistantDiscoveryFor(i);
            }
        }
    }

    if (freshTelegram && now - LAST_UPDATE_SENT > UPDATE_INTERVAL)
    {
        LAST_UPDATE_SENT = millis();

        // Publish every field this meter actually has, not just the ones
        // that changed since the last send - otherwise a steady reading
        // (e.g. current staying at 1A for a while) simply never gets
        // re-sent, and HA's history graph shows misleadingly sparse data
        // instead of a continuous line.
        for (int i = 0; i < NUMBER_OF_READOUTS; i++)
        {
            if (telegramObjects[i].everSeen)
            {
                telegramObjects[i].sendData = true;
            }
        }

        sendDataToBroker();
    }
}

/***********************************
            Setup Methods
 ***********************************/

/**
   setupDataReadout()

   This method can be used to create more data readout to mqtt topic.
   Use the name for the mqtt topic.
   The code for finding this in the telegram see
    https://www.netbeheernederland.nl/_upload/Files/Slimme_meter_15_a727fce1f1.pdf for the dutch codes pag. 19 -23
   Use startChar and endChar for setting the boundies where the value is in between.
   Default startChar and endChar is '(' and ')'
   Note: Make sure when you add or remove telegramObject to update the NUMBER_OF_READOUTS accordingly.
*/
void setupDataReadout()
{
    // 1-0:1.8.1(000992.992*kWh) - Hatasos importenergia (A+) tarifa T1
    // Named after the tariff register itself (T1-T4, per the Sagemcom P1
    // spec) rather than "low/high": which register is actually the cheaper
    // one depends on the utility's own billing schedule, not the meter data.
    telegramObjects[0].name = "consumption_tarif_1";
    strcpy(telegramObjects[0].code, "1-0:1.8.1");
    telegramObjects[0].endChar = '*';

    // 1-0:1.8.2(000560.157*kWh) - Hatasos importenergia (A+) tarifa T2
    telegramObjects[1].name = "consumption_tarif_2";
    strcpy(telegramObjects[1].code, "1-0:1.8.2");
    telegramObjects[1].endChar = '*';

    // 1-0:2.8.1(000560.157*kWh) - Hatasos exportenergia (A+) tarifa T1
    telegramObjects[2].name = "returndelivery_tarif_1";
    strcpy(telegramObjects[2].code, "1-0:2.8.1");
    telegramObjects[2].endChar = '*';

    // 1-0:2.8.2(000560.157*kWh) - Hatasos exportenergia (A+) tarifa T2
    telegramObjects[3].name = "returndelivery_tarif_2";
    strcpy(telegramObjects[3].code, "1-0:2.8.2");
    telegramObjects[3].endChar = '*';

    // 1-0:1.7.0(00.424*kW) Actueel verbruik
    // 1-0:1.7.x = Electricity consumption actual usage (DSMR v5.0)
    telegramObjects[4].name = "actual_consumption";
    strcpy(telegramObjects[4].code, "1-0:1.7.0");
    telegramObjects[4].endChar = '*';

    // 1-0:2.7.0(00.000*kW) Actuele teruglevering (-P) in 1 Watt resolution
    telegramObjects[5].name = "actual_received";
    strcpy(telegramObjects[5].code, "1-0:2.7.0");
    telegramObjects[5].endChar = '*';

    // 1-0:21.7.0(00.378*kW)
    // 1-0:21.7.0 = Instantaan vermogen Elektriciteit levering L1
    telegramObjects[6].name = "instant_power_usage_l1";
    strcpy(telegramObjects[6].code, "1-0:21.7.0");
    telegramObjects[6].endChar = '*';

    // 1-0:41.7.0(00.378*kW)
    // 1-0:41.7.0 = Instantaan vermogen Elektriciteit levering L2
    telegramObjects[7].name = "instant_power_usage_l2";
    strcpy(telegramObjects[7].code, "1-0:41.7.0");
    telegramObjects[7].endChar = '*';

    // 1-0:61.7.0(00.378*kW)
    // 1-0:61.7.0 = Instantaan vermogen Elektriciteit levering L3
    telegramObjects[8].name = "instant_power_usage_l3";
    strcpy(telegramObjects[8].code, "1-0:61.7.0");
    telegramObjects[8].endChar = '*';

    // 1-0:22.7.0(00.378*kW)
    // 1-0:22.7.0 = Instantaan vermogen Elektriciteit teruglevering L1
    telegramObjects[9].name = "instant_power_return_l1";
    strcpy(telegramObjects[9].code, "1-0:22.7.0");
    telegramObjects[9].endChar = '*';

    // 1-0:42.7.0(00.378*kW)
    // 1-0:42.7.0 = Instantaan vermogen Elektriciteit teruglevering L2
    telegramObjects[10].name = "instant_power_return_l2";
    strcpy(telegramObjects[10].code, "1-0:42.7.0");
    telegramObjects[10].endChar = '*';

    // 1-0:62.7.0(00.378*kW)
    // 1-0:62.7.0 = Instantaan vermogen Elektriciteit teruglevering L3
    telegramObjects[11].name = "instant_power_return_l3";
    strcpy(telegramObjects[11].code, "1-0:62.7.0");
    telegramObjects[11].endChar = '*';

    // 1-0:31.7.0(002*A)
    // 1-0:31.7.0 = Instantane stroom Elektriciteit L1
    telegramObjects[12].name = "instant_power_current_l1";
    strcpy(telegramObjects[12].code, "1-0:31.7.0");
    telegramObjects[12].endChar = '*';

    // 1-0:51.7.0(002*A)
    // 1-0:51.7.0 = Instantane stroom Elektriciteit L2
    telegramObjects[13].name = "instant_power_current_l2";
    strcpy(telegramObjects[13].code, "1-0:51.7.0");
    telegramObjects[13].endChar = '*';

    // 1-0:71.7.0(002*A)
    // 1-0:71.7.0 = Instantane stroom Elektriciteit L3
    telegramObjects[14].name = "instant_power_current_l3";
    strcpy(telegramObjects[14].code, "1-0:71.7.0");
    telegramObjects[14].endChar = '*';

    // 1-0:32.7.0(232.0*V)
    // 1-0:32.7.0 = Voltage L1
    telegramObjects[15].name = "instant_voltage_l1";
    strcpy(telegramObjects[15].code, "1-0:32.7.0");
    telegramObjects[15].endChar = '*';

    // 1-0:52.7.0(232.0*V)
    // 1-0:52.7.0 = Voltage L2
    telegramObjects[16].name = "instant_voltage_l2";
    strcpy(telegramObjects[16].code, "1-0:52.7.0");
    telegramObjects[16].endChar = '*';

    // 1-0:72.7.0(232.0*V)
    // 1-0:72.7.0 = Voltage L3
    telegramObjects[17].name = "instant_voltage_l3";
    strcpy(telegramObjects[17].code, "1-0:72.7.0");
    telegramObjects[17].endChar = '*';

    // 0-0:96.14.0(0002) - confirmed against a real captured MA110M telegram
    // (the "42.0.0" code documented as "Aktiv tarifa" in the manufacturer's
    // PDF is actually a hex-encoded device identifier string, not the tariff).
    telegramObjects[18].name = "actual_tarif_group";
    strcpy(telegramObjects[18].code, "0-0:96.14.0");

    // 1-0:1.8.0(000992.992*kWh) - Hatasos importenergia (A+) total
    telegramObjects[19].name = "active_energy_import_total";
    strcpy(telegramObjects[19].code, "1-0:1.8.0");
    telegramObjects[19].endChar = '*';

    // 1-0:2.8.0(000992.992*kWh) - Hatasos exportenergia (A+) total
    telegramObjects[20].name = "returndelivery_total";
    strcpy(telegramObjects[20].code, "1-0:2.8.0");
    telegramObjects[20].endChar = '*';

    // 1-0:3.8.0(000992.992*kvarh) - Import meddo energia (R+) total
    telegramObjects[21].name = "reactive_energy_import_total";
    strcpy(telegramObjects[21].code, "1-0:3.8.0");
    telegramObjects[21].endChar = '*';

    // 1-0:4.8.0(000992.992*kvarh) - Export meddo energia (R-) total
    telegramObjects[22].name = "reactive_energy_export_total";
    strcpy(telegramObjects[22].code, "1-0:4.8.0");
    telegramObjects[22].endChar = '*';

    // 1-0:5.8.0(000992.992*kvarh) - Meddo energia (QI), induktiv
    telegramObjects[23].name = "reactive_energy_q1";
    strcpy(telegramObjects[23].code, "1-0:5.8.0");
    telegramObjects[23].endChar = '*';

    // 1-0:6.8.0(000992.992*kvarh) - Meddo energia (QII), kapacitiv
    telegramObjects[24].name = "reactive_energy_q2";
    strcpy(telegramObjects[24].code, "1-0:6.8.0");
    telegramObjects[24].endChar = '*';

    // 1-0:7.8.0(000992.992*kvarh) - Meddo energia (QIII), induktiv
    telegramObjects[25].name = "reactive_energy_q3";
    strcpy(telegramObjects[25].code, "1-0:7.8.0");
    telegramObjects[25].endChar = '*';

    // 1-0:8.8.0(000992.992*kvarh) - Meddo energia (QIV), kapacitiv
    telegramObjects[26].name = "reactive_energy_q4";
    strcpy(telegramObjects[26].code, "1-0:8.8.0");
    telegramObjects[26].endChar = '*';

    // 1-0:15.8.0(000992.992*kWh) - Hatasos energia kombinalt (|+A|+|-A|) total
    telegramObjects[27].name = "active_energy_combined_total";
    strcpy(telegramObjects[27].code, "1-0:15.8.0");
    telegramObjects[27].endChar = '*';

    // 1-0:1.8.3(000000.000*kWh) - Hatasos importenergia (A+) tarifa T3
    telegramObjects[28].name = "consumption_tarif_3";
    strcpy(telegramObjects[28].code, "1-0:1.8.3");
    telegramObjects[28].endChar = '*';

    // 1-0:1.8.4(000000.000*kWh) - Hatasos importenergia (A+) tarifa T4
    telegramObjects[29].name = "consumption_tarif_4";
    strcpy(telegramObjects[29].code, "1-0:1.8.4");
    telegramObjects[29].endChar = '*';

    // 1-0:2.8.3(000000.000*kWh) - Hatasos exportenergia (A+) tarifa T3
    telegramObjects[30].name = "returndelivery_tarif_3";
    strcpy(telegramObjects[30].code, "1-0:2.8.3");
    telegramObjects[30].endChar = '*';

    // 1-0:2.8.4(000000.000*kWh) - Hatasos exportenergia (A+) tarifa T4
    telegramObjects[31].name = "returndelivery_tarif_4";
    strcpy(telegramObjects[31].code, "1-0:2.8.4");
    telegramObjects[31].endChar = '*';

#ifdef DEBUG
    Serial.println("MQTT Topics initialized:");
    for (int i = 0; i < NUMBER_OF_READOUTS; i++)
    {
        Serial.println(String(MQTT_ROOT_TOPIC) + "/" + telegramObjects[i].name);
    }
#endif
}

/**
   Over the Air update setup
*/
void setupOTA()
{
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else // U_FS
            type = "filesystem";

        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
            Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
            Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
            Serial.println("End Failed");
    });

    ArduinoOTA.begin();
}
