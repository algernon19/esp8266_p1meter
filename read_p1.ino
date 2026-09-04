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

    char res[16];
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

#ifdef DEBUG
    for (int cnt = 0; cnt < len; cnt++)
    {
        Serial.print(telegram[cnt]);
    }
    Serial.print("\n");
#endif

    if (startChar >= 0)
    {
        // * Start found. Reset CRC calculation
        currentCRC = crc16(0x0000, (unsigned char *)telegram + startChar, len - startChar);
    }
    else if (endChar >= 0)
    {
        // * Add to crc calc
        currentCRC = crc16(currentCRC, (unsigned char *)telegram + endChar, 1);

        char messageCRC[5];
        strncpy(messageCRC, telegram + endChar + 1, 4);

        messageCRC[4] = 0; // * Thanks to HarmOtten (issue 5)
        validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);

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

    // Loops throug all the telegramObjects to find the code in the telegram line
    // If it finds the code the value will be stored in the object so it can later be send to the mqtt broker
    for (int i = 0; i < NUMBER_OF_READOUTS; i++)
    {
        if (strncmp(telegram, telegramObjects[i].code, strlen(telegramObjects[i].code)) == 0)
        {
            long newValue = getValue(telegram, len, telegramObjects[i].startChar, telegramObjects[i].endChar);
            if (newValue != telegramObjects[i].value)
            {
                telegramObjects[i].value = newValue;
                telegramObjects[i].sendData = true;
            }
            break;

#ifdef DEBUG
            Serial.println((String) "Found a Telegram object: " + telegramObjects[i].name + " value: " + telegramObjects[i].value);
#endif
        }
    }

    return validCRCFound;
}

bool readP1Serial()
{
    if (p1Serial.available())
    {
#ifdef DEBUG
        Serial.println("p1Serial is available");
        Serial.println("Memset telegram");
#endif
        memset(telegram, 0, sizeof(telegram));
        while (p1Serial.available())
        {
            // Reads the telegram untill it finds a return character
            // That is after each line in the telegram
            int len = p1Serial.readBytesUntil('\n', telegram, P1_MAXLINELENGTH);

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
