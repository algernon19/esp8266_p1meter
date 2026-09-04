#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>

#include "settings.h"

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// P1 telegrams are sent inverted TTL at 115200 baud. Using SoftwareSerial for
// this (instead of the hardware UART) keeps Serial free for USB debug output
// and OTA. The P1 port is receive-only, so no TX pin is needed.
SoftwareSerial p1Serial(P1_RX_PIN, -1, true);

/***********************************
            Main Setup
 ***********************************/
void setup()
{
    // Initialize pins
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // Off (LED_BUILTIN is active low on most ESP8266 boards)
    Serial.begin(BAUD_RATE);
    p1Serial.begin(BAUD_RATE);

#ifdef DEBUG
    Serial.println("Booting - DEBUG mode on");
    blinkLed(2, 500);
    delay(500);
    blinkLed(2, 2000);
    // Blinking 2 times fast and two times slower to indicate DEBUG mode
#endif
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
#ifdef DEBUG
        Serial.println("Connection Failed! Rebooting...");
#endif
        delay(5000);
        ESP.restart();
    }
    delay(3000);
    setupDataReadout();
    setupOTA();

    mqttClient.setServer(MQTT_HOST, atoi(MQTT_PORT));
    blinkLed(5, 500); // Blink 5 times to indicate end of setup
#ifdef DEBUG
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
#endif
}

/***********************************
            Main Loop
 ***********************************/
void loop()
{
    long now = millis();
    if (WiFi.status() != WL_CONNECTED)
    {
        blinkLed(20, 50); // Blink fast to indicate failed WiFi connection
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        while (WiFi.waitForConnectResult() != WL_CONNECTED)
        {
#ifdef DEBUG
            Serial.println("Connection Failed! Rebooting...");
#endif
            delay(5000);
            ESP.restart();
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

    // Check if we want a full update of all the data including the unchanged data.
    if (now - LAST_FULL_UPDATE_SENT > UPDATE_FULL_INTERVAL)
    {
        for (int i = 0; i < NUMBER_OF_READOUTS; i++)
        {
            telegramObjects[i].sendData = true;
            LAST_FULL_UPDATE_SENT = millis();
        }
    }

    if (now - LAST_UPDATE_SENT > UPDATE_INTERVAL)
    {
        if (readP1Serial())
        {
            LAST_UPDATE_SENT = millis();
            sendDataToBroker();
        }
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
    // 1-0:1.8.1(000992.992*kWh)
    // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v5.0)
    telegramObjects[0].name = "consumption_low_tarif";
    strcpy(telegramObjects[0].code, "1-0:1.8.1");
    telegramObjects[0].endChar = '*';

    // 1-0:1.8.2(000560.157*kWh)
    // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v5.0)
    telegramObjects[1].name = "consumption_high_tarif";
    strcpy(telegramObjects[1].code, "1-0:1.8.2");
    telegramObjects[1].endChar = '*';

    // 1-0:2.8.1(000560.157*kWh)
    // 1-0:2.8.1 = Elektra teruglevering laag tarief (DSMR v5.0)
    telegramObjects[2].name = "returndelivery_low_tarif";
    strcpy(telegramObjects[2].code, "1-0:2.8.1");
    telegramObjects[2].endChar = '*';

    // 1-0:2.8.2(000560.157*kWh)
    // 1-0:2.8.2 = Elektra teruglevering hoog tarief (DSMR v5.0)
    telegramObjects[3].name = "returndelivery_high_tarif";
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

    // 0-0:96.14.0(0001)
    // 0-0:96.14.0 = Actual Tarif
    telegramObjects[18].name = "actual_tarif_group";
    strcpy(telegramObjects[18].code, "0-0:96.14.0");

    // 0-1:24.2.3(150531200000S)(00811.923*m3)
    // 0-1:24.2.3 = Gas (DSMR v5.0) on Belgian meters
    telegramObjects[19].name = "gas_meter_m3";
    strcpy(telegramObjects[19].code, "0-1:24.2.3");
    telegramObjects[19].endChar = '*';

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

    ArduinoOTA
        .onStart([]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
                type = "sketch";
            else // U_FS
                type = "filesystem";

            Serial.println("Start updating " + type);
        })
        .onEnd([]() {
            Serial.println("\nEnd");
        })
        .onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        })
        .onError([](ota_error_t error) {
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
