void sendMQTTMessage(const char *topic, char *payload)
{
    bool result = mqttClient.publish(topic, payload, false);
}

// Forwards an OBIS line this firmware has no curated field for (e.g. clock,
// breaker status, error/alarm registers) as a raw string, under
// MQTT_ROOT_TOPIC/raw/<obis-code>, so nothing the meter sends is dropped.
void publishRawObisLine(char *line, int len)
{
    int parenPos = -1;
    for (int i = 0; i < len; i++)
    {
        if (line[i] == '(')
        {
            parenPos = i;
            break;
        }
    }
    if (parenPos <= 0)
    {
        return; // not an "OBIS-code(value)" line
    }

    char code[24];
    int codeLen = min(parenPos, (int)sizeof(code) - 1);
    memcpy(code, line, codeLen);
    code[codeLen] = 0;

    int end = len;
    while (end > 0 && (line[end - 1] == '\n' || line[end - 1] == '\r'))
    {
        end--;
    }

    int lastParen = -1;
    for (int i = end - 1; i > parenPos; i--)
    {
        if (line[i] == ')')
        {
            lastParen = i;
            break;
        }
    }
    if (lastParen <= parenPos)
    {
        return;
    }

    char value[64];
    int valueLen = min(lastParen - parenPos - 1, (int)sizeof(value) - 1);
    memcpy(value, line + parenPos + 1, valueLen);
    value[valueLen] = 0;

    String topic = String(MQTT_ROOT_TOPIC) + "/raw/" + code;
    sendMQTTMessage(topic.c_str(), value);
}

struct HassSensorMeta
{
    const char *friendlyName;
    const char *unit;        // nullptr if the sensor has no unit
    const char *deviceClass; // nullptr if none applies
    const char *stateClass;  // nullptr if none applies
};

// Order must match the telegramObjects[] indices set up in setupDataReadout().
// device_class/state_class are required for the HA Energy dashboard to pick
// up a sensor - "energy" + "total_increasing" for cumulative kWh counters,
// "power"/"current"/"voltage" + "measurement" for instantaneous readings.
// kvarh (reactive energy) has no HA device_class, so only state_class is set.
const HassSensorMeta hassSensors[NUMBER_OF_READOUTS] = {
    {"P1 Consumption Tariff 1", "kWh", "energy", "total_increasing"},
    {"P1 Consumption Tariff 2", "kWh", "energy", "total_increasing"},
    {"P1 Return Delivery Tariff 1", "kWh", "energy", "total_increasing"},
    {"P1 Return Delivery Tariff 2", "kWh", "energy", "total_increasing"},
    {"P1 Actual Power Consumption", "kW", "power", "measurement"},
    {"P1 Actual Return Delivery", "kW", "power", "measurement"},
    {"P1 L1 Instant Power Usage", "kW", "power", "measurement"},
    {"P1 L2 Instant Power Usage", "kW", "power", "measurement"},
    {"P1 L3 Instant Power Usage", "kW", "power", "measurement"},
    {"P1 L1 Instant Power Return", "kW", "power", "measurement"},
    {"P1 L2 Instant Power Return", "kW", "power", "measurement"},
    {"P1 L3 Instant Power Return", "kW", "power", "measurement"},
    {"P1 L1 Instant Power Current", "A", "current", "measurement"},
    {"P1 L2 Instant Power Current", "A", "current", "measurement"},
    {"P1 L3 Instant Power Current", "A", "current", "measurement"},
    {"P1 L1 Voltage", "V", "voltage", "measurement"},
    {"P1 L2 Voltage", "V", "voltage", "measurement"},
    {"P1 L3 Voltage", "V", "voltage", "measurement"},
    {"P1 Actual Tariff Group", nullptr, nullptr, nullptr},
    {"P1 Active Energy Import Total", "kWh", "energy", "total_increasing"},
    {"P1 Return Delivery Total", "kWh", "energy", "total_increasing"},
    {"P1 Reactive Energy Import Total", "kvarh", nullptr, "total_increasing"},
    {"P1 Reactive Energy Export Total", "kvarh", nullptr, "total_increasing"},
    {"P1 Reactive Energy QI", "kvarh", nullptr, "total_increasing"},
    {"P1 Reactive Energy QII", "kvarh", nullptr, "total_increasing"},
    {"P1 Reactive Energy QIII", "kvarh", nullptr, "total_increasing"},
    {"P1 Reactive Energy QIV", "kvarh", nullptr, "total_increasing"},
    {"P1 Active Energy Combined Total", "kWh", "energy", "total_increasing"},
    {"P1 Consumption Tariff 3", "kWh", "energy", "total_increasing"},
    {"P1 Consumption Tariff 4", "kWh", "energy", "total_increasing"},
    {"P1 Return Delivery Tariff 3", "kWh", "energy", "total_increasing"},
    {"P1 Return Delivery Tariff 4", "kWh", "energy", "total_increasing"},
};

void publishHomeAssistantDiscoveryFor(int i)
{
    char topic[96];
    char payload[500];
    char uniqueId[48];
    char extra[80];

    const char *objectId = telegramObjects[i].name.c_str();

    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", HOSTNAME, objectId);
    snprintf(uniqueId, sizeof(uniqueId), "%s_%s", HOSTNAME, objectId);

    extra[0] = 0;
    if (hassSensors[i].deviceClass != nullptr)
    {
        char devCla[32];
        snprintf(devCla, sizeof(devCla), ",\"dev_cla\":\"%s\"", hassSensors[i].deviceClass);
        strncat(extra, devCla, sizeof(extra) - strlen(extra) - 1);
    }
    if (hassSensors[i].stateClass != nullptr)
    {
        char statCla[40];
        snprintf(statCla, sizeof(statCla), ",\"stat_cla\":\"%s\"", hassSensors[i].stateClass);
        strncat(extra, statCla, sizeof(extra) - strlen(extra) - 1);
    }

    if (hassSensors[i].unit != nullptr)
    {
        snprintf(payload, sizeof(payload),
                 "{\"name\":\"%s\",\"uniq_id\":\"%s\",\"stat_t\":\"%s/%s\",\"unit_of_meas\":\"%s\",\"val_tpl\":\"{{ value|float / 1000 }}\"%s,\"dev\":{\"ids\":[\"%s\"],\"name\":\"P1 Meter\",\"mf\":\"esp8266_p1meter\",\"mdl\":\"ESP8266 P1 Meter\"}}",
                 hassSensors[i].friendlyName, uniqueId, MQTT_ROOT_TOPIC, objectId, hassSensors[i].unit, extra, HOSTNAME);
    }
    else
    {
        snprintf(payload, sizeof(payload),
                 "{\"name\":\"%s\",\"uniq_id\":\"%s\",\"stat_t\":\"%s/%s\"%s,\"dev\":{\"ids\":[\"%s\"],\"name\":\"P1 Meter\",\"mf\":\"esp8266_p1meter\",\"mdl\":\"ESP8266 P1 Meter\"}}",
                 hassSensors[i].friendlyName, uniqueId, MQTT_ROOT_TOPIC, objectId, extra, HOSTNAME);
    }

    mqttClient.publish(topic, payload, true); // retained
}

// An empty retained payload on a discovery config topic tells Home
// Assistant's MQTT integration to remove that entity - used to clean up
// fields (e.g. L2/L3 on a single-phase meter) that turned out to never
// actually be present, in case they were announced by an earlier firmware.
void unpublishHomeAssistantDiscoveryFor(int i)
{
    char topic[96];
    const char *objectId = telegramObjects[i].name.c_str();
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", HOSTNAME, objectId);
    mqttClient.publish(topic, "", true);
}

// Called on every MQTT (re)connect. Only re-announces fields this meter has
// actually reported at least once (everSeen) - fields never seen yet simply
// get announced later, the first time decodeTelegram() matches them for
// real (see read_p1.ino), so a single-phase meter never gets L2/L3 (etc.)
// entities created in Home Assistant that would sit permanently
// "unavailable".
void publishHomeAssistantDiscovery()
{
    for (int i = 0; i < NUMBER_OF_READOUTS; i++)
    {
        if (telegramObjects[i].everSeen)
        {
            publishHomeAssistantDiscoveryFor(i);
            delay(20);
        }
    }
}

bool mqttReconnect()
{
    int MQTT_RECONNECT_RETRIES = 0;

    while (!mqttClient.connected() && MQTT_RECONNECT_RETRIES < MQTT_MAX_RECONNECT_TRIES)
    {
        MQTT_RECONNECT_RETRIES++;

        if (mqttClient.connect(HOSTNAME, MQTT_USER, MQTT_PASS))
        {
            char *message = new char[16 + strlen(HOSTNAME) + 1];
            strcpy(message, "p1 meter alive: ");
            strcat(message, HOSTNAME);
            mqttClient.publish("hass/status", message);
            publishHomeAssistantDiscovery();
        }
        else
        {
            delay(5000);
        }
    }

    if (MQTT_RECONNECT_RETRIES >= MQTT_MAX_RECONNECT_TRIES)
    {
        return false;
    }

    return true;
}

void sendMetric(String name, long metric)
{
    //if (metric > 0)
    //{
        char output[10];
        ltoa(metric, output, sizeof(output));

        String topic = String(MQTT_ROOT_TOPIC) + "/" + name;
#ifdef DEBUG
        Serial.println(topic);
#endif
        sendMQTTMessage(topic.c_str(), output);
    //}
}

void sendDataToBroker()
{
    for (int i = 0; i < NUMBER_OF_READOUTS; i++)
    {
#ifdef DEBUG
        Serial.println((String) "Sending: " + telegramObjects[i].name + " value: " + telegramObjects[i].value);
#endif
        if (telegramObjects[i].sendData)
        {
            sendMetric(telegramObjects[i].name, telegramObjects[i].value);
            telegramObjects[i].sendData = false;
        }
    }
}
