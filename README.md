# esp8266_p1meter
Software for the ESP8266 (Wemos D1 mini, NodeMCU, etc.) that decodes and sends P1 smart meter (DSMR) data to a MQTT broker, with the possibility for Over The Air (OTA) firmware updates.

## About this fork
This fork is based on the [esp32_p1meter fork by RoySprnger](https://github.com/RoySprnger/esp32_p1meter), ported (back) to the ESP8266 so it can be powered directly from USB without needing a separate 5V supply.

The ESP8266 only has a single hardware UART that is also used for flashing and USB debug output, so this fork reads the P1 data using `SoftwareSerial` on a separate GPIO pin instead of a second hardware serial port. This keeps the USB port free for `Serial.print` debugging and for the initial flash. Because we only need one successful (CRC-valid) telegram per update interval, the occasional corrupted line that `SoftwareSerial` produces at 115200 baud is not a problem — the firmware simply waits for the next telegram.

By default this fork only reads/publishes data once a minute (`UPDATE_INTERVAL` in `settings.h`), which is plenty for Home Assistant graphs and keeps MQTT traffic low.

## Setup
This setup requires:
- An ESP8266 board (Wemos D1 mini or NodeMCU recommended)
- Small breadboard
- A 10k ohm resistor
- A 4 pin (RJ11) or [6 pin (RJ12) cable](https://www.tinytronics.nl/shop/nl/kabels/adapters/rj12-naar-6-pins-dupont-jumper-adapter)

Setting up your Arduino IDE:
- Ensure you have selected the right board (install the ESP8266 board package via the Boards Manager if needed).
- Using the Tools->Manage Libraries... install `PubSubClient`. `SoftwareSerial` and `ArduinoOTA` are bundled with the ESP8266 core.
- In the file `settings.h` change all values accordingly (WiFi, MQTT, update interval, etc.) — copy it from `settings.h.default` first if it doesn't exist yet.
- Write to your device via USB the first time, you can do it OTA all times thereafter.

### Circuit diagram
Connect the ESP8266 to an RJ11 cable/connector following the diagram. By default this fork uses GPIO4 (labeled `D2` on a Wemos D1 mini / NodeMCU) for the P1 data line — this can be changed via `P1_RX_PIN` in `settings.h`.

| P1 pin   | ESP8266 Pin |
| ----     | ---- |
| 2 - RTS  | 3.3v |
| 3 - GND  | GND  |
| 4 -      |      |
| 5 - RXD (data) | D2 (GPIO4) |

On most models a 10K resistor should be used between the ESP's 3.3v and the p1's DATA (RXD) pin. Many howto's mention RTS requires 5V (VIN) to activate the P1 port, but for most meters 3V3 suffices.

<details><summary>Optional: Powering the ESP8266 using your DSMR5+ meter</summary>
<p>
When using a 6 pin cable you can use the power source provided by the meter instead of USB.

| P1 pin   | ESP8266 Pin |
| ----     | ---- |
| 1 - 5v out | 5v or Vin |
| 2 - RTS  | 3.3v |
| 3 - GND  | GND  |
| 4 -      |      |
| 5 - RXD (data) | D2 (GPIO4) |
| 6 - GND  | GND  |

</p>
</details>

### Data Sent

All metrics are send to their own MQTT topic.
The software generates all the topic through the Serial monitor when starting up
Example topics are:

```
sensors/power/p1meter/consumption_low_tarif
sensors/power/p1meter/consumption_high_tarif
sensors/power/p1meter/returndelivery_low_tarif
sensors/power/p1meter/returndelivery_high_tarif
sensors/power/p1meter/actual_consumption
sensors/power/p1meter/actual_received
sensors/power/p1meter/instant_power_usage_l1
sensors/power/p1meter/instant_power_usage_l2
sensors/power/p1meter/instant_power_usage_l3
sensors/power/p1meter/instant_power_return_l1
sensors/power/p1meter/instant_power_return_l2
sensors/power/p1meter/instant_power_return_l3
sensors/power/p1meter/instant_power_current_l1
sensors/power/p1meter/instant_power_current_l2
sensors/power/p1meter/instant_power_current_l3
sensors/power/p1meter/instant_voltage_l1
sensors/power/p1meter/instant_voltage_l2
sensors/power/p1meter/instant_voltage_l3
sensors/power/p1meter/actual_tarif_group
sensors/power/p1meter/gas_meter_m3
```

But all the metrics you need are easily added using the `setupDataReadout()` method. With the DEBUG mode it is easy to see all the topics you add/create by the serial monitor. To see what your telegram is outputting in the Netherlands see: https://www.netbeheernederland.nl/_upload/Files/Slimme_meter_15_a727fce1f1.pdf for the dutch codes pag. 19 -23

### Home Assistant Configuration

Use [assets/p1_sensors.yaml](assets/p1_sensors.yaml) for Home Assistant's `sensor.yaml` — the topic names in there match `setupDataReadout()` in [esp8266_p1meter.ino](esp8266_p1meter.ino).

## Known limitations and issues
`SoftwareSerial` at 115200 baud on the ESP8266 will occasionally drop or corrupt a telegram line. This is not a problem for the default once-a-minute update interval since a CRC-invalid telegram is simply discarded and retried on the next second. If you need faster updates and see missing/incorrect readings, consider switching `P1_RX_PIN`/the read logic to the hardware UART (`Serial`) in receive-only + inverted mode instead — at the cost of losing USB debug output while the meter is connected.

## Thanks to
- [RoySprnger/esp32_p1meter](https://github.com/RoySprnger/esp32_p1meter)
- [fliphess/esp8266_p1meter](https://github.com/fliphess/esp8266_p1meter)
- [daniel-jong/esp8266_p1meter](https://github.com/daniel-jong/esp8266_p1meter)
- [WhoSayIn/esp8266_dsmr2mqtt](https://github.com/WhoSayIn/esp8266_dsmr2mqtt)
- [jhockx/esp8266_p1meter](https://github.com/jhockx/esp8266_p1meter)
- https://github.com/jantenhove/P1-Meter-ESP8266
- https://github.com/neographikal/P1-Meter-ESP8266-MQTT
- http://gejanssen.com/howto/Slimme-meter-uitlezen/
- https://github.com/rroethof/p1reader/
- http://romix.macuser.nl/software.html
- http://blog.regout.info/category/slimmeter/
- http://domoticx.com/p1-poort-slimme-meter-hardware/

Other sources:
- [DSMR 5.0 documentation](https://www.netbeheernederland.nl/_upload/Files/Slimme_meter_15_a727fce1f1.pdf)
