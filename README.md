<center><bold>ESP8266-Web-Server-for-Mobile-Sensors</bold></center>

This project is based on the EspressIf SDK IoT_Demo example. This initial version functions as a web server with the WIFI 
operating in AP mode. 

This project was built using the EspressIf SDK Version 1.1.1

The ESP8266 access point is hard-coded with the following configuration:

SSID: ESP8266N4
IP: 192.168.22.1
Port: 9703

The web server responds to a request for sensor values and status with a JSON string that can be easily parsed with an AJAX call.

Request URL:

http://192.168.22.1:9703/?request=GetSensors


JSON string returned:

{
"B_Pressure":"29.24",
"B_Temperature":"79.34",
"B_Altitude":"555.0",
"DH_Humidity":"34.7",
"DH_Temperature":"69.1",
"SYS_Time":"38",
"SYS_Heap":"32368",
"SYS_Loopcnt":"10",
"SYS_WifiStatus":"255",
"SYS_WifiRecon":"0",
"SYS_WifiMode":"2"
}

Initial Release: July 9, 2015. 

Along with the "SYS_" values returned in the JSON string, only the BMP085 Temperature and Barometric Pressure Values 
are active with this release.

