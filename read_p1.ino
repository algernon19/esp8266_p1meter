unsigned int crc16(unsigned int crc, unsigned char *buf, int len)
{
    for (int pos = 0; pos < len; pos++)
    {
        crc ^= (unsigned int)buf[pos];

        for (int i = 8; i != 0; i--)
        {
            if ((crc & 0x0001) != 0)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool isNumber(char *res, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (((res[i] < '0') || (res[i] > '9')) && (res[i] != '.' && res[i] != 0))
        {
            return false;
        }
    }
    return true;
}

int findCharInArrayRev(char array[], char c, int len)
{
    for (int i = len - 1; i >= 0; i--)
    {
        if (array[i] == c)
        {
            return i;
        }
    }

    return -1;
}

long getValue(char *buffer, int maxlen, char startchar, char endchar)
{
    int s = findCharInArrayRev(buffer, startchar, maxlen - 2);
    int l = findCharInArrayRev(buffer, endchar, maxlen - 2) - s - 1;

    // Guard against startchar/endchar not being found (or found out of
    // order): findCharInArrayRev returns -1 for "not found", which would
    // otherwise turn into a negative length reinterpreted as a huge unsigned
    // size in strncpy - a buffer overflow that also explains bogus,
    // near-INT_MAX readout values.
    char res[16];
    if (s < 0 || l <= 0 || l >= (int)sizeof(res))
    {
        return 0;
    }

    memset(res, 0, sizeof(res));

    if (strncpy(res, buffer + s + 1, l))
    {
        if (endchar == '*')
        {
            if (isNumber(res, l))
                return (1000 * atof(res));
        }
        else if (endchar == ')')
        {
            if (isNumber(res, l))
                return atof(res);
        }
    }

    return 0;
}

/**
 *  Decodes the telegram PER line. Not the complete message. 
 */
bool decodeTelegram(int len)
{
    int startChar = findCharInArrayRev(telegram, '/', len);
    int endChar = findCharInArrayRev(telegram, '!', len);
    bool validCRCFound = false;

    P1_LINES_RECEIVED++;
    LAST_P1_LINE_MILLIS = millis();
    int copyLen = min(len, (int)sizeof(LAST_P1_LINE) - 1);
    memcpy(LAST_P1_LINE, telegram, copyLen);
    LAST_P1_LINE_LEN = copyLen;

#ifdef DEBUG
    for (int cnt = 0; cnt < len; cnt++)
    {
        Serial.print(telegram[cnt]);
    }
    Serial.print("\n");
#endif

    if (startChar >= 0)
    {
        // * Start found. Reset CRC calculation and begin a fresh capture.
        P1_TELEGRAM_START_COUNT++;
        currentCRC = crc16(0x0000, (unsigned char *)telegram + startChar, len - startChar);
        CURRENT_TELEGRAM_LEN = 0;
    }
    else if (endChar >= 0)
    {
        // * Add to crc calc
        currentCRC = crc16(currentCRC, (unsigned char *)telegram + endChar, 1);

        char messageCRC[5];
        strncpy(messageCRC, telegram + endChar + 1, 4);

        messageCRC[4] = 0; // * Thanks to HarmOtten (issue 5)
        validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);

        if (validCRCFound)
            P1_CRC_VALID_COUNT++;
        else
            P1_CRC_INVALID_COUNT++;

#ifdef DEBUG
        if (validCRCFound)
            Serial.println(F("CRC Valid!"));
        else
            Serial.println(F("CRC Invalid!"));
#endif
        currentCRC = 0;
    }
    else
    {
        currentCRC = crc16(currentCRC, (unsigned char *)telegram, len);
    }

    // Accumulate this line into the current telegram capture buffer, so the
    // full raw telegram (not just the last line) can be inspected on the
    // status page.
    int room = P1_MAXTELEGRAMLENGTH - 1 - (int)CURRENT_TELEGRAM_LEN;
    if (room > 0)
    {
        int appendLen = min(len, room);
        memcpy(CURRENT_TELEGRAM_BUFFER + CURRENT_TELEGRAM_LEN, telegram, appendLen);
        CURRENT_TELEGRAM_LEN += appendLen;
    }

    if (endChar >= 0)
    {
        // Telegram complete (valid or not) - snapshot it for the status page.
        int snapshotLen = min((int)CURRENT_TELEGRAM_LEN, (int)sizeof(LAST_FULL_TELEGRAM) - 1);
        memcpy(LAST_FULL_TELEGRAM, CURRENT_TELEGRAM_BUFFER, snapshotLen);
        LAST_FULL_TELEGRAM_LEN = snapshotLen;
        LAST_FULL_TELEGRAM_CRC_VALID = validCRCFound;
    }

    // Loops throug all the telegramObjects to find the code in the telegram line
    // If it finds the code the value will be stored in the object so it can later be send to the mqtt broker
    bool matchedKnownCode = false;
    for (int i = 0; i < NUMBER_OF_READOUTS; i++)
    {
        if (strncmp(telegram, telegramObjects[i].code, strlen(telegramObjects[i].code)) == 0)
        {
            matchedKnownCode = true;
            if (!telegramObjects[i].everSeen)
            {
                // First time this OBIS code has ever been seen: announce it
                // to Home Assistant now that we know the meter really has it.
                telegramObjects[i].everSeen = true;
                publishHomeAssistantDiscoveryFor(i);
            }

            long newValue = getValue(telegram, len, telegramObjects[i].startChar, telegramObjects[i].endChar);
            if (newValue != telegramObjects[i].value)
            {
                telegramObjects[i].value = newValue;
                telegramObjects[i].sendData = true;
            }
            break;
        }
    }

    // Any OBIS-coded line we don't have a curated field for still gets
    // forwarded as-is (e.g. date/time, breaker status), so nothing the
    // meter sends is silently dropped.
    if (!matchedKnownCode && startChar < 0 && endChar < 0)
    {
        publishRawObisLine(telegram, len);
    }

    return validCRCFound;
}

bool readP1Serial()
{
    if (Serial.available())
    {
        memset(telegram, 0, sizeof(telegram));
        while (Serial.available())
        {
            // Reads the telegram untill it finds a return character
            // That is after each line in the telegram
            int len = Serial.readBytesUntil('\n', telegram, P1_MAXLINELENGTH);

            telegram[len] = '\n';
            telegram[len + 1] = 0;

            bool result = decodeTelegram(len + 1);
            // When the CRC is check which is also the end of the telegram
            // if valid decode return true
            if (result)
            {
                return true;
            }

            // Reset the ESP8266 watchdog: SoftwareSerial reads can take a
            // while and there is no yield() inside readBytesUntil().
            yield();
        }
    }
    return false;
}
