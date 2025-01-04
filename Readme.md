# Simple MQTT Gateway V 01 (SMG)

Change:
* 1.1.2025: Initial release

## Features

First useable version of a very simple BLE to MQTT Gateway it features

* Forwarding of Bluetooth-Low Energy Packet Servicedata to MQTT in form of publishing to a topic like 
 ```home/omg/BTtoMQTT/C129FF449E37 {"id":"C1:29:FF:44:9E:37","servicedata":"b10f7506f50b080203020202be00310200000000b100","rssi":-67,"phy":1,"gw":"smgC33"}```
* Leading part of Topic (Like home/omg/BTtoMQTT) can be configured, rest cososts of BLE Adresse of the device
* phy: describes the physical layer, the SMG has support for phy=3 (BLE Long Range, coded PHY)
* gw: can be choosen as the name of the device, it is also the ID of the device to connect to MQTT, so do not have more than gateway (gw) with this name in your network.
* Can be selected, that only packets that have service-data will be forwarded
* Can be selected, that the servicedata will be stripped out of the forward. This isregardless if servicedata is available or not and this can be used, if e.g. only the rssi or even only the presence of a device is of interest

### Whitelist (currently under development)
* per MQTT a whitelist of permissable devices can be set, by publishing an list of BT-adresses to the whitelist-topic like 
 ```mosquitto_pub -h 192.168.0.XXX -u  USERNAME -P PASSWORD -t home/omg/whitelist -m "AA:BB:CC:DD:EE:FF,11:22:33:44:55:66,7C:DF:A1:E5:7D:E1,2C:BE:EB:14:AF:C7,54:99:5B:65:4E:D2,2C:BE:EB:14:AF:C7"```
 Check config (platform.ini) for MQTT message size and whitelist size
* whitelist will be reset after each restart.

## Useage (Manual)

* On first start, the SMG will set up an accesspoint with SSIG=SMG (no password) and a webserver at 192.168.7.1. 
* Connect to the Webserver at 192.168.7.1 and put in the data. Note that the topic should NOT start with an "/" and not end with an "/"
* The device will restart and start working
* For a factory reset (reenter all the configuration values above) just press reset-button in short sequence 5 to 6 times. This will empty the config data and lets you reconfigure the device

## Programming
* uses libraries
```
 #include "NimBLEDevice.h"
 #include <WiFi.h>
 #include <PubSubClient.h>
 #include <WebServer.h>
 #include <Preferences.h>
 ```
* getting the right setup for coded PHY was a bit tedious, flags are 

## Special...

For me the whitelist must be
```mosquitto_pub -h 192.168.0.XXX -u  USERNAME -P PASSWORD -t home/omg/whitelist -m "E5:81:44:86:69:15,EF:3C:B9:4A:3D:AD,C1:29:FF:44:9E:37,E7:E2:A1:4C:BD:0C,DB:EB:BF:A5:2D:F2,DC:C6:F4:CB:AE:CD,F2:BC:30:43:70:EB,E5:7A:C6:3D:4D:EE,CA:CA:5A:D2:65:B3,DE:18:AC:8B:0C:81,C3:7B:7F:9A:34:B6,FC:DB:5F:E1:D9:DA,EF:92:C7:C3:28:49,CC:EB:EA:A3:A5:14,E7:65:A6:D0:53:60,F3:8A:29:25:2B:11,D4:15:0A:7A:B0:8F,CE:0A:DB:3F:09:B4,C2:0A:53:54:AE:62,F6:09:02:D0:E0:0C"```