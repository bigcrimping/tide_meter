#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h>
#include "arduino_secrets.h"
#include "exixe.h"

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;    // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int status = WL_IDLE_STATUS;     // the Wifi radio's status
char server[] = "environment.data.gov.uk";   // environment agency website
String server_results; // Contains the JSON returned data
int cs1 = 8;
int cs2 = 9;
int cs3 = 10;
// MISO is D12, D11 is MOSI, SCK is D13
unsigned char count;

exixe my_tube1 = exixe(cs1);
exixe my_tube2 = exixe(cs2);
exixe my_tube3 = exixe(cs3);

StaticJsonDocument<3072> doc; //Sets the memory allocation for the JSON doc
WiFiSSLClient client; //Defines the client for the SSL routing

void setup() {
  
  Serial.begin(9600); // Start USB Serial Port

  my_tube1.spi_init(); //Initialize the tubes

  my_tube1.clear();    //clear the tubes
  my_tube2.clear();
  my_tube3.clear();
  my_tube1.show_digit(9, 127, 0); //show the sine wave at start up
  
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

  // attempt to connect to Wifi network:

  while (status != WL_CONNECTED) {

    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);
    // wait 5 seconds for connection:
    delay(5000);
  }

}
String connectandgrab(){
  
    String server_result;
    String output;
    // if you get a connection, report back via serial:
    output = client.connectSSL(server, 443);
    
    if (output) {                     //  port 443 for HTTPS
      //Serial.println("connected to server");
      // Make a HTTP request:
      client.println("GET /flood-monitoring/id/measures/E72639-level-tidal_level-Mean-15_min-mAOD.json HTTP/1.1");
      client.println("Host: environment.data.gov.uk");
      client.println("Connection: close");
      client.println();
    }
    else
    {
      Serial.println("failed to connect");
      Serial.println(output);
    }
    delay(1000);
  
    int start = 0; // Reset the flag that defines the start of the JSON data
    // if there are incoming bytes available
     // from the server, read them and print them:
    while (client.available()) {
      char c = client.read();
      if(c == '{') // Look for starting bracket of the JSON data or the deserializeJson doesn't work
      {
        start = 1; // Set the flag to indicate start of valid JSON data
      }
    
      if(start == 1)
      {
        server_result = server_result + c; //Append the new data onto the server_results
      }   
    
    }
  
  // if the server's disconnected, stop the client:
  if (!client.connected()) {
    Serial.println();
    //Serial.println("disconnecting from server.");
    client.stop();
    return server_result;
    }
}

void loop() {
 
    Serial.println("Going into grab routine"); //Notify going into the grab routine
    server_results = connectandgrab(); //Store the JSON string into server_results
    DeserializationError error = deserializeJson(doc, server_results); //Deserialize the data
    
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    else {
      const char* context = doc["@context"];

      JsonObject meta = doc["meta"];    
      JsonObject items = doc["items"];
      JsonObject items_latestReading = items["latestReading"];
      float items_latestReading_value = items_latestReading["value"]; 
      
      Serial.println(items_latestReading_value); 

      String value = String(items_latestReading_value); //This is either something like 7.22 or -1.34
      char sign = value.charAt(0); // + or -
      uint8_t sign_output;
      uint8_t first;
      uint8_t second;
      
      if (sign == '-') {
        //If the first digit is a - then its a negative result, grab the position 1 and position 3 digits and set the first digit to 1 which is "-"
        sign_output = 1;
        first = value.charAt(1)- '0'; // First digit
        second = value.charAt(3)- '0'; // Second digit
      }
      else{
        //There is no leading sign so set the first digit to 1 for "+" then grab the 0 and and 2 index
        sign_output = 3;
        first = value.charAt(0)- '0'; // First digit
        second = value.charAt(2)- '0'; // Second digit
      }


 
      Serial.print("Value is: ");
      Serial.println(value);
      Serial.print("Sign is: ");
      Serial.println(sign_output);
      Serial.print("First is: ");
      Serial.println(first);
      Serial.print("Second is: ");
      Serial.println(second);
      //IN-19V 1=-, 2=>, 3=+,4=blank, 5=dB, 6=N,7=blank,8=A/B,9=wave
      
      my_tube1.show_digit(sign_output, 127, 0);
      my_tube2.show_digit(first, 127, 0);
      my_tube2.set_dots(0,127);
      my_tube3.show_digit(second, 127, 0);
      

      
      my_tube1.set_led(0, 0, 0);
      my_tube2.set_led(0, 0, 0); 
      my_tube3.set_led(0, 0, 0); 
    }
      
    // wait 3 mins for next grab
    delay(300000);
}
