#define DEBUG
// Update treshold in milliseconds, messages will only be sent on this interval
//#define UPDATE_INTERVAL 1000 // 1 second
//#define UPDATE_INTERVAL 10000 // 10 seconds
#define UPDATE_INTERVAL 60000 // 1 minute
//#define UPDATE_INTERVAL 300000 // 5 minutes

#define HOSTNAME "p1meter"
#define OTA_PASSWORD "p1meter123"

// If WIFI_SSID can't be reached within this time, fall back to a local AP
// (AP_SSID/AP_PASSWORD) so the status page can still be reached directly.
#define WIFI_CONNECT_TIMEOUT 15000 // 15 seconds
#define AP_SSID "P1Meter-Setup"
#define AP_PASSWORD "p1meter123"

#define BAUD_RATE 115200

// P1 data is read on the hardware UART's RX pin (GPIO3, labeled "RX" on a
// Wemos D1 mini / NodeMCU board) - NOT GPIO4/D2. This sacrifices live USB
// debug output while the meter is connected (see esp8266_p1meter.ino setup())
// in exchange for reliable reception at 115200 baud.
#define P1_MAXLINELENGTH 1050

#define MQTT_MAX_RECONNECT_TRIES 100
#define MQTT_ROOT_TOPIC "sensors/power/p1meter"

#define NUMBER_OF_READOUTS 32

long LAST_RECONNECT_ATTEMPT = 0;
long LAST_UPDATE_SENT = 0;

char WIFI_SSID[32] = "Krumpli";
char WIFI_PASS[32] = "4LzxBj6cCUhrxv";

char MQTT_HOST[64] = "10.10.0.252";
char MQTT_PORT[6] = "1883";
char MQTT_USER[32] = "villanyora";
char MQTT_PASS[32] = "123Teszt123";

char telegram[P1_MAXLINELENGTH];

struct TelegramDecodedObject
{
  String name;
  long value;
  char code[16];
  char startChar = '(';
  char endChar = ')';
  bool sendData = false;
  // True once this OBIS code has actually been seen in a telegram line -
  // lets us tell "genuinely reads as 0" apart from "this meter doesn't have
  // this phase/field at all", so we don't publish fake readings for fields
  // (e.g. L2/L3 on a single-phase meter) that never actually appear.
  bool everSeen = false;
};

struct TelegramDecodedObject telegramObjects[NUMBER_OF_READOUTS];

unsigned int currentCRC = 0;

// Set once, after the first fully-valid telegram has been processed: used to
// trigger a one-time cleanup pass that removes any Home Assistant entities
// for fields this meter turns out to never actually report (e.g. L2/L3 on a
// single-phase meter), in case an earlier firmware version had announced
// them unconditionally.
bool P1_TOPOLOGY_RESOLVED = false;

// Diagnostic state for the web status page (see webserver.ino)
unsigned long P1_LINES_RECEIVED = 0;
unsigned long P1_TELEGRAM_START_COUNT = 0;
unsigned long P1_CRC_VALID_COUNT = 0;
unsigned long P1_CRC_INVALID_COUNT = 0;
unsigned long LAST_P1_LINE_MILLIS = 0;
unsigned int LAST_P1_LINE_LEN = 0;
char LAST_P1_LINE[P1_MAXLINELENGTH];

// Accumulates the telegram currently being received (reset on '/', finalized
// on '!'+CRC) so the full raw telegram - not just the last line - can be
// inspected on the status page.
#define P1_MAXTELEGRAMLENGTH 2048
char CURRENT_TELEGRAM_BUFFER[P1_MAXTELEGRAMLENGTH];
unsigned int CURRENT_TELEGRAM_LEN = 0;
char LAST_FULL_TELEGRAM[P1_MAXTELEGRAMLENGTH];
unsigned int LAST_FULL_TELEGRAM_LEN = 0;
bool LAST_FULL_TELEGRAM_CRC_VALID = false;
