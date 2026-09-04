void blinkLed(int numberOfBlinks, int msBetweenBlinks)
{
  for (int i = 0; i < numberOfBlinks; i++)
  {
    digitalWrite(LED_BUILTIN, LOW); // LED_BUILTIN is active low on most ESP8266 boards
    delay(msBetweenBlinks);
    digitalWrite(LED_BUILTIN, HIGH);
    if (i != numberOfBlinks - 1)
    {
      delay(msBetweenBlinks);
    }
  }
}
