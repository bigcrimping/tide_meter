#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h>    // version 7 or later
#include <FlashStorage.h>
#include "exixe.h"

// WiFi details are no longer compiled in. They are entered over the USB serial
// port (Serial Monitor, line ending set to Newline) and kept in the SAMD21's
// flash. Type "help" for the commands. Uploading a new sketch erases them.

char server[] = "environment.data.gov.uk";   // environment agency website
// The measure to display. Change the id for another station.
const char MEASURE_PATH[] = "/flood-monitoring/id/measures/E72639-level-tidal_level-Mean-15_min-mAOD.json";

int cs1 = 8;
int cs2 = 9;
int cs3 = 10;
// MISO is D12, D11 is MOSI, SCK is D13

exixe my_tube1 = exixe(cs1);   // IN-19V symbol tube
exixe my_tube2 = exixe(cs2);   // IN-14 first digit (with decimal point)
exixe my_tube3 = exixe(cs3);   // IN-14 second digit

WiFiSSLClient client; //Defines the client for the SSL routing

// The Environment Agency publishes a new reading every 15 minutes; ask every 5.
const unsigned long UPDATE_INTERVAL_MS = 300000UL;
const unsigned long HTTP_TIMEOUT_MS = 10000UL;

// A failed fetch is retried after this long instead of a full update interval.
const unsigned long RETRY_AFTER_FAILURE_MS = 30000UL;
// Pause between WiFi coming up and the first request, so the link (DHCP, DNS)
// has settled; the first request straight after connecting could time out.
const unsigned long SETTLE_AFTER_CONNECT_MS = 3000UL;

// How long one WiFi.begin() may block, and how long to wait between attempts.
// Serial commands are handled between attempts.
const unsigned long WIFI_ATTEMPT_TIMEOUT_MS = 15000UL;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;

// If no good reading has arrived for this long (three missed updates, for any
// reason: WiFi down, server down, bad response) the digits are blanked so a
// stale reading cannot be mistaken for a live one.
const unsigned long STALE_AFTER_MS = 3UL * UPDATE_INTERVAL_MS + 60000UL;

// While no WiFi details are stored, remind whoever is on the serial port.
const unsigned long PROMPT_INTERVAL_MS = 10000UL;

//IN-19V 1=-, 2=>, 3=+, 4=blank, 5=dB, 6=N, 7=blank, 8=A/B, 9=wave
const uint8_t SYM_MINUS = 1;
const uint8_t SYM_PLUS = 3;
const uint8_t SYM_WAVE = 9;
const uint8_t TUBE_BRIGHTNESS = 127;

// ---------------------------------------------------------------------------
// Stored WiFi details
// ---------------------------------------------------------------------------

// Marks a flash record written by this sketch. Freshly uploaded flash reads
// back as something else, which is how "nothing stored" is detected.
const uint32_t CREDENTIALS_MAGIC = 0x54494445UL;   // "TIDE"

typedef struct {
  uint32_t magic;
  char ssid[33];   // SSIDs are at most 32 characters
  char pass[64];   // WPA passphrases are at most 63 characters; empty = open network
} Credentials;

FlashStorage(credential_store, Credentials);

Credentials creds;     // what the WiFi connection uses
Credentials staged;    // what the ssid/pass commands edit, until "save"
bool have_credentials = false;

// ---------------------------------------------------------------------------
// Scheduling state (everything is millis() based so loop() never blocks for
// long and serial commands keep working)
// ---------------------------------------------------------------------------

bool wifi_attempt_due = true;
unsigned long last_wifi_attempt_ms = 0;
unsigned long last_fetch_ms = 0;              // when the wait for the next fetch began
unsigned long fetch_wait_ms = SETTLE_AFTER_CONNECT_MS;   // how long that wait is
bool showing_level = false;
unsigned long last_good_ms = 0;
unsigned long last_prompt_ms = 0;

char line_buffer[112];
size_t line_length = 0;
bool line_overflow = false;

void setup() {

  Serial.begin(9600); // Start USB Serial Port

  my_tube1.spi_init(); //Initialize the tubes

  my_tube1.clear();    //clear the tubes
  my_tube2.clear();
  my_tube3.clear();
  my_tube1.show_digit(SYM_WAVE, TUBE_BRIGHTNESS, 0); //show the sine wave at start up

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  WiFi.setTimeout(WIFI_ATTEMPT_TIMEOUT_MS);

  loadCredentials();
}

// ---------------------------------------------------------------------------
// Credentials
// ---------------------------------------------------------------------------

void loadCredentials() {
  creds = credential_store.read();
  have_credentials = (creds.magic == CREDENTIALS_MAGIC) && creds.ssid[0] != '\0';
  if (!have_credentials) {
    memset(&creds, 0, sizeof(creds));
  }
  // Guard against a record that is not terminated.
  creds.ssid[sizeof(creds.ssid) - 1] = '\0';
  creds.pass[sizeof(creds.pass) - 1] = '\0';
  staged = creds;
}

// Drop the current connection and start again with whatever is in `creds`.
void restartWiFi() {
  client.stop();
  WiFi.disconnect();
  wifi_attempt_due = true;
}

// Fetch soon after WiFi (re)connects, once the link has settled.
void scheduleFetchAfterConnect() {
  last_fetch_ms = millis();
  fetch_wait_ms = SETTLE_AFTER_CONNECT_MS;
}

// ---------------------------------------------------------------------------
// Serial commands
// ---------------------------------------------------------------------------

void printHelp() {
  Serial.println();
  Serial.println("Tide meter commands (send each line with Newline):");
  Serial.println("  ssid <name>      set the WiFi network name");
  Serial.println("  pass <password>  set the WiFi password (\"pass\" alone = open network)");
  Serial.println("  save             store both in flash and connect");
  Serial.println("  show             show stored details and connection state");
  Serial.println("  clear            erase the stored details");
  Serial.println("  help             this list");
  Serial.println("Stored details are erased when a new sketch is uploaded.");
}

void printStatus() {
  Serial.print("Stored SSID: ");
  Serial.println(have_credentials ? creds.ssid : "(none)");
  Serial.print("Stored password: ");
  if (!have_credentials) Serial.println("(none)");
  else Serial.println(creds.pass[0] ? "(set)" : "(empty, open network)");

  if (strcmp(staged.ssid, creds.ssid) != 0 || strcmp(staged.pass, creds.pass) != 0) {
    Serial.print("Unsaved changes: SSID \"");
    Serial.print(staged.ssid);
    Serial.println("\" - type \"save\" to use them");
  }

  Serial.print("WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("connected, IP ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("not connected");
  }
}

// Copy a command argument into a fixed-size field, refusing anything too long.
bool setField(char* field, size_t field_size, const char* value, const char* name) {
  if (strlen(value) >= field_size) {
    Serial.print(name);
    Serial.print(" is too long, the limit is ");
    Serial.print(field_size - 1);
    Serial.println(" characters");
    return false;
  }
  strcpy(field, value);
  return true;
}

void handleCommand(char* line) {
  while (*line == ' ') line++;   // ignore leading spaces
  if (*line == '\0') return;

  if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
    printHelp();
  }
  else if (strncmp(line, "ssid ", 5) == 0) {
    if (line[5] == '\0') {
      Serial.println("Usage: ssid <name>");
    } else if (setField(staged.ssid, sizeof(staged.ssid), line + 5, "SSID")) {
      Serial.print("SSID set to \"");
      Serial.print(staged.ssid);
      Serial.println("\" - type \"save\" when the password is set too");
    }
  }
  else if (strcmp(line, "pass") == 0 || strncmp(line, "pass ", 5) == 0) {
    const char* value = (line[4] == ' ') ? line + 5 : "";
    if (setField(staged.pass, sizeof(staged.pass), value, "Password")) {
      Serial.println(value[0] ? "Password set - type \"save\" to store and connect"
                              : "Password cleared (open network) - type \"save\" to store and connect");
    }
  }
  else if (strcmp(line, "save") == 0) {
    if (staged.ssid[0] == '\0') {
      Serial.println("Set an SSID first: ssid <name>");
      return;
    }
    staged.magic = CREDENTIALS_MAGIC;
    credential_store.write(staged);
    creds = staged;
    have_credentials = true;
    Serial.println("Saved. Connecting...");
    restartWiFi();
  }
  else if (strcmp(line, "show") == 0) {
    printStatus();
  }
  else if (strcmp(line, "clear") == 0) {
    memset(&creds, 0, sizeof(creds));
    credential_store.write(creds);
    staged = creds;
    have_credentials = false;
    client.stop();
    WiFi.disconnect();
    showStale();
    Serial.println("Stored WiFi details erased.");
  }
  else {
    Serial.println("Unknown command. Type \"help\".");
  }
}

// Collect characters into a line and run it when the line ends. Never blocks.
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line_overflow) {
        Serial.println("Line too long, ignored");
      } else {
        line_buffer[line_length] = '\0';
        handleCommand(line_buffer);
      }
      line_length = 0;
      line_overflow = false;
    }
    else if (line_length < sizeof(line_buffer) - 1) {
      line_buffer[line_length++] = c;
    }
    else {
      line_overflow = true;
    }
  }
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

// Make one connection attempt with the stored details. Blocks for at most
// WIFI_ATTEMPT_TIMEOUT_MS; loop() calls it again later if it did not work.
void attemptWiFi() {
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(creds.ssid);

  if (creds.pass[0]) WiFi.begin(creds.ssid, creds.pass);   // WPA/WPA2
  else WiFi.begin(creds.ssid);                              // open network

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP ");
    Serial.println(WiFi.localIP());
    scheduleFetchAfterConnect();
  } else {
    Serial.println("Not connected yet, will retry. Type \"show\" or \"help\" if the details are wrong.");
  }
}

// ---------------------------------------------------------------------------
// Tide data
// ---------------------------------------------------------------------------

// Fetch the latest tide level (metres above ordnance datum) into `level`.
// Returns false on any failure, leaving `level` untouched.
//
// The JSON is parsed straight from the socket with a filter, so only
// items.latestReading.value is stored. The full response is ~2 KB, which the
// old approach copied into a String one character at a time and then again
// into a 3 KB document: too much churn for a 32 KB SAMD21 over weeks.
bool fetchTideLevel(float& level) {

  int ok = client.connectSSL(server, 443);   // port 443 for HTTPS
  if (!ok) {
    Serial.println("failed to connect");
    client.stop();
    return false;
  }

  // HTTP/1.0 so the server never uses chunked transfer encoding, which would
  // put a hex chunk-length line in front of the JSON.
  client.print("GET ");
  client.print(MEASURE_PATH);
  client.println(" HTTP/1.0");
  client.println("Host: environment.data.gov.uk");
  client.println("Connection: close");
  client.println();

  // Wait (with a deadline) for the response to start arriving instead of a
  // fixed pause; a slow TLS handshake used to leave us with an empty body.
  unsigned long start = millis();
  while (client.connected() && !client.available()) {
    if (millis() - start > HTTP_TIMEOUT_MS) {
      Serial.println("timeout waiting for response");
      client.stop();
      return false;
    }
    delay(10);
  }

  // Skip the headers: everything up to the blank line that terminates them.
  client.setTimeout(HTTP_TIMEOUT_MS);
  if (!client.find("\r\n\r\n")) {
    Serial.println("no end of headers found");
    client.stop();
    return false;
  }

  JsonDocument filter;
  filter["items"]["latestReading"]["value"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));
  client.stop();

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }

  JsonVariant value = doc["items"]["latestReading"]["value"];
  if (value.isNull()) {
    Serial.println("items.latestReading.value missing");
    return false;
  }

  level = value.as<float>();
  return true;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

// Show a level such as 7.28 or -1.34 as a sign symbol plus two digits with
// the decimal point after the first: "+7.3", "-1.3". Uses integer maths on
// tenths so the value is rounded (not truncated), and a value that rounds to
// zero is always shown as +0.0.
void showLevel(float level) {
  long tenths = lround(fabs(level) * 10.0f);
  bool negative = level < 0 && tenths > 0;
  if (tenths > 99) {
    Serial.println("Level beyond 9.9, clamping display");
    tenths = 99;
  }
  uint8_t first = tenths / 10;
  uint8_t second = tenths % 10;

  Serial.print("Value is: ");
  Serial.println(level);
  Serial.print("Display: ");
  Serial.print(negative ? '-' : '+');
  Serial.print(first);
  Serial.print('.');
  Serial.println(second);

  my_tube1.show_digit(negative ? SYM_MINUS : SYM_PLUS, TUBE_BRIGHTNESS, 0);
  my_tube2.show_digit(first, TUBE_BRIGHTNESS, 0);
  my_tube2.set_dots(0, TUBE_BRIGHTNESS);
  my_tube3.show_digit(second, TUBE_BRIGHTNESS, 0);

  my_tube1.set_led(0, 0, 0);
  my_tube2.set_led(0, 0, 0);
  my_tube3.set_led(0, 0, 0);

  showing_level = true;
  last_good_ms = millis();
}

// No trustworthy data: wave symbol only, digits blank.
void showStale() {
  my_tube1.show_digit(SYM_WAVE, TUBE_BRIGHTNESS, 0);
  my_tube2.clear();
  my_tube3.clear();
  showing_level = false;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void loop() {

  handleSerial();

  unsigned long now = millis();

  // Blank a reading that has not been refreshed for too long, whatever the cause.
  if (showing_level && now - last_good_ms >= STALE_AFTER_MS) {
    Serial.println("No fresh reading for too long, blanking the digits");
    showStale();
  }

  // Nothing to connect with yet: wait for the serial commands.
  if (!have_credentials) {
    if (last_prompt_ms == 0 || now - last_prompt_ms >= PROMPT_INTERVAL_MS) {
      last_prompt_ms = now ? now : 1;
      Serial.println("No WiFi details stored. Type \"help\" to set them.");
    }
    return;
  }

  // Connect, or recover from a dropout, one bounded attempt at a time.
  if (WiFi.status() != WL_CONNECTED) {
    if (wifi_attempt_due || now - last_wifi_attempt_ms >= WIFI_RETRY_INTERVAL_MS) {
      wifi_attempt_due = false;
      attemptWiFi();
      last_wifi_attempt_ms = millis();
    }
    return;
  }

  // Connected: fetch when due. After a failure, retry sooner than a full
  // interval, but never in a tight loop.
  if (now - last_fetch_ms >= fetch_wait_ms) {
    Serial.println("Going into grab routine");
    float level;
    if (fetchTideLevel(level)) {
      showLevel(level);
      fetch_wait_ms = UPDATE_INTERVAL_MS;
    } else {
      Serial.print("Fetch failed, retrying in ");
      Serial.print(RETRY_AFTER_FAILURE_MS / 1000);
      Serial.println(" seconds");
      fetch_wait_ms = RETRY_AFTER_FAILURE_MS;
    }
    last_fetch_ms = millis();
  }
}
