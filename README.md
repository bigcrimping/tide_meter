# Tide Meter
An Arduino Nano 33 IoT based gadget which interrogates the latest tide height from your chosen UK measurement station.

![image](https://github.com/bigcrimping/tide_meter/assets/74270551/4ed0a469-31e1-4765-ad39-08cfae97c226)

The tide data is scraped from the Environment Agency website: https://environment.data.gov.uk/flood-monitoring/tidegauge/index.html#filter=7&station=E72639

![image](https://github.com/bigcrimping/tide_meter/assets/74270551/2ba11598-a86a-4bd9-a6f0-1221cf00a84d)

The Bristol channel where the measurement is taken has the third highest tidal range in the world

The code is an amalgamation from the following sources

Nixie driver https://github.com/dekuNukem/exixe/tree/master

WiFi https://www.arduino.cc/reference/en/libraries/wifinina/wifi.beginap/

SSL https://www.arduino.cc/reference/en/libraries/wifinina/client.connectssl/

JSON Handling https://arduinojson.org/v6/api/json/deserializejson/

## Project Code

The code is here: https://github.com/bigcrimping/tide_meter/tree/main/code/wave_height_0.2

You will need to add your WiFi details to the arduino_secrets.h file for the network you wish to connect to.


## Mechanical Files

Files in 3mf format are here: https://github.com/bigcrimping/tide_meter/tree/main/mech

![image](https://github.com/bigcrimping/tide_meter/assets/74270551/40fad846-1327-408a-bc02-fce476bf1751)


The design comprises (from top to bottom) 
1 x IN-19V symbol and then 2 x IN-14 numerics
top_case
tube_pcb_crossbar
tube_pcb_frame
arduino_HV
USB+3V3_Bottom
bottom case

![image](https://github.com/bigcrimping/tide_meter/assets/74270551/34597712-df34-4e6c-814a-f571179465d6)

Fixing required 

10 x M3 nuts

20 x M3x8mmmm cap head bolts

STLs for printing available here: 

The boards inside are a USB C PD controller, a 20V to 5V step down, a 20V to 170V step up, 3 "exixe" drivers and a Arduino Nano 33 IOT board
