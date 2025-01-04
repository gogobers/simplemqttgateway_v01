/** Example of continuous scanning for BLE advertisements.
 * This example will scan forever while consuming as few resources as possible
 * and report all advertisments on the serial monitor.
 *
 * Created: on January 31 2021
 *      Author: H2zero
 * Class reference: https://h2zero.github.io/NimBLE-Arduino/index.html
 *
 */


/* Hilfreiche Aufrufe für mosquitto
mosquitto_pub -h 192.168.0.245 -u  openhabian -P openhabian -t home/MYomg/whitelist -m "AA:BB:CC:DD:EE:FF,11:22:33:44:55:66,7C:DF:A1:E5:7D:E1,2C:BE:EB:14:AF:C7,54:99:5B:65:4E:D2,2C:BE:EB:14:AF:C7"
mosquitto_sub -h 192.168.0.245 -t 'home/MYomg/#' -v -u  openhabian -P openhabian 
mosquitto_sub -h 192.168.0.245 -t '#' -v -u  openhabian -P openhabian

TODO:
- nRF nun noch auf Fordermann bringen, so dass der auch die coded PHY verwendet
- Evtl. hier noch einen Webservice einbauen, wenn das Ding nicht initialisiert wurde
- ESP32 (plai) aus rückwärtskompatibilitätsgründen einbauen
- in OpenHAB eine whitelist hinterlegen, dei ca. alle 10min oder bei änderung neu 
   erstellt versendet wird

*/

#include "NimBLEDevice.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <Preferences.h>

#define DEBUG

static constexpr uint32_t scanTimeMs = 30 * 1000 *0; // 30 seconds scan time.

NimBLEScan* pBLEScan;

// WLAN- und MQTT-Konfigurationsdaten

// Default AP credentials
const char* apSSID = "SMG";

// Stored data
String gatewayName = "smg";
String ssid = "";
String password = "";
String mqttBrokerIP = "192.168.0.";
uint16_t mqttBrokerPort = 1883;
String mqttUser = "openhabian";
String mqttPassword = "";
String mqtt_topic = "home/omg/BTtoMQTT";
String mqtt_whitelist_topic = "home/omg/whitelist";
bool subscribeToWhitelist = true;
bool onlyWithServiceData = false;
bool stripServiceData = false;
uint16_t boot_number;

Preferences config;

WebServer server(80);

WiFiClient espClient;
PubSubClient MQTTclient(espClient);

class MyScanCallback: public NimBLEScanCallbacks {
  private:
    const NimBLEAdvertisedDevice* adv;

  void handle_request(const char* caller){
    std::string sd,ad;
    char json_string[200];
    char topic[30];
    char sevdat[66];

    NimBLEAddress adr = adv->getAddress();

    ad = adr.toString();
    std::transform(ad.begin(), ad.end(), ad.begin(), ::toupper);
#ifdef DEBUG
    Serial.printf("%s Advertised Device: %s \n", caller, adv->toString().c_str());
    Serial.printf("Address = %s\n",ad.c_str());
#endif

    bool send_data=false;
    if ((adv->getServiceDataCount()>0)&&(!stripServiceData)){
      sd=adv->getServiceData(0);
#ifdef DEBUG
      Serial.printf("Data length %d Data::",sd.size());
#endif
      for (int ii=0; ii<sd.size(); ii++){
#ifdef DEBUG
        Serial.printf("%02x ",sd.data()[ii]);
#endif
        uint8_t jj=sd.data()[ii]>>4;
        jj=(jj>9?jj+87:jj+48);
        sevdat[ii*2]  =jj;
        jj=sd.data()[ii]&0x0F;
        jj=(jj>9?jj+87:jj+48);
        sevdat[ii*2+1]=jj;
      }
      sevdat[sd.size()*2]=0;
      // Erzeuge string für die Daten in MQTT mit ServiceData
      sprintf(json_string, 
        "{\"id\":\"%s\",\"servicedata\":\"%s\",\"rssi\":%d,\"phy\":%d,\"gw\":\"%s\"}",
        ad.c_str(),sevdat,adv->getRSSI(), adv->getPrimaryPhy(),gatewayName.c_str());
      send_data=true;
    } else {
      if ((adv->getServiceDataCount()>0) || (!onlyWithServiceData)) {
        // Erzeuge string für die Daten in MQTT ohne ServiceData
        sprintf(json_string, 
          "{\"id\":\"%s\",\"rssi\":%d,\"phy\":%d,\"gw\":\"%s\"}",
          ad.c_str(),adv->getRSSI(), adv->getPrimaryPhy(),gatewayName.c_str());
        send_data=true;
      }
    }

    if (send_data) {
      // Lösche die ":" aus der Adresse für das Topic im MQTT
      ad.erase(std::remove(ad.begin(), ad.end(), ':'), ad.end());
      sprintf(topic,"%s/%s",mqtt_topic.c_str(),ad.c_str());
      // Nachricht an ein Topic senden
      bool rc = MQTTclient.publish(topic, json_string);
#ifdef DEBUG
      Serial.printf("\n");
      Serial.printf("%s\n",json_string);
      Serial.printf("%s\n",topic);
      if (rc) {
        Serial.println("Message sent successfully!");
      } else {
        Serial.println("Failed to send message.");
      }
      Serial.printf("=========================================================\n");
    }
#endif
  }

  void onDiscovered(const NimBLEAdvertisedDevice* advertisedDevice) override {
    adv=advertisedDevice;
    handle_request("OnDiscovered");
  }

  /**
  *  If active scanning the result here will have the scan response data.
  *  If not active scanning then this will be the same as onDiscovered.
  */
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    //adv=advertisedDevice;
    //handle_request("OnResult");
  }

  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    //printf("Scan ended reason = %d; restarting scan\n", reason);
    //NimBLEDevice::getScan()->start(scanTimeMs, false, true);
  }
} scanCallbacks;


void wifi_setup() {
  delay(10);
#ifdef DEBUG
  Serial.println();
  Serial.print("Wifi Connecting to ");
  Serial.println(ssid);
#endif

  WiFi.begin(ssid, password);
  //WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
#ifdef DEBUG
  Serial.println("\nWiFi connected");
#endif
}

/**
 * Löscht die gesamte Whitelist des Devices
 */
void whitelist_clear(){
  while(NimBLEDevice::getWhiteListCount()>0) {
    NimBLEAddress adr = NimBLEDevice::getWhiteListAddress(NimBLEDevice::getWhiteListCount()-1);
    NimBLEDevice::whiteListRemove(adr);
#ifdef DEBUG
    Serial.printf("Whitelist removed Addr: %s",adr.toString().c_str());
#endif
  }
  pBLEScan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
  pBLEScan->start(scanTimeMs, false, true);
}

/**
 * Fügt eine Adresse (als String) der whitelist zu
 */
void whitelist_add(std::string ad){
  NimBLEAddress adr(ad,0);
  bool res=NimBLEDevice::whiteListAdd(adr);
  if (!res){
    NimBLEAddress adr(ad,1);
    res=NimBLEDevice::whiteListAdd(adr);
  }
#ifdef DEBUG
  Serial.printf("Whitelist Add: %s, result: %d\n",ad.c_str(),res);
#endif
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
#ifdef DEBUG
    Serial.print("Message arrived on topic: ");
    Serial.println(topic);
#endif

    // Konvertieren der Payload in eine String
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
#ifdef DEBUG
    Serial.print("Received message: ");
    Serial.println(message);
#endif

    // Lösche die gesamte Whitelist
    whitelist_clear();

    // Trennen der Bluetooth-Adressen durch Komma
    // und füge die adressen der whitelist zu
#ifdef DEBUG    
    Serial.println("Bluetooth Addresses:");
#endif
    int start = 0;
    while (start < message.length()) {
        int end = message.indexOf(',', start);
        if (end == -1) end = message.length();
        std::string address = message.substring(start, end).c_str();
        whitelist_add(address);
#ifdef DEBUG
        Serial.println(address.c_str());
#endif
        start = end + 1;
    }
    pBLEScan->setFilterPolicy(BLE_HCI_SCAN_FILT_USE_WL_INITA);
    pBLEScan->start(scanTimeMs, false, true);
}


void mqtt_reconnect() {
    while (!MQTTclient.connected()) {
        //String id = String(random(100000,999999));
        //gatewayName = id;
        if (MQTTclient.connect(gatewayName.c_str(),mqttUser.c_str(),mqttPassword.c_str())) {
#ifdef DEBUG
            Serial.printf("MQTT-Broker Connected with id = %s!\n",gatewayName.c_str());
#endif
            if (subscribeToWhitelist){
              MQTTclient.subscribe(mqtt_whitelist_topic.c_str());
            }
        } else {
#ifdef DEBUG
            Serial.print("Failed, rc=");
            Serial.print(MQTTclient.state());
            Serial.println(". Retrying in 0.5 seconds...");
#endif
            delay(500);
        }
    }
}

/** 
 * Save the configuration to flash/ EEPROM
 */
void saveConfig() {
  config.begin("config",false);
  config.putString("gwn",gatewayName);
  config.putString("ssid",ssid);
  config.putString("pw",password);
  config.putString("mbip",mqttBrokerIP);
  config.putUShort("mbp",mqttBrokerPort);
  config.putString("mu",mqttUser);
  config.putString("mpw",mqttPassword);
  config.putString("mt",mqtt_topic);
  config.putBool("mwl",subscribeToWhitelist);
  config.putString("mwlt",mqtt_whitelist_topic);
  config.putBool("osd",onlyWithServiceData);
  config.putBool("ssd",stripServiceData);
  config.putUShort("bn",boot_number);
  config.end();
}

/** 
 * Load the configuration from flash/ EEPROM
 */
void loadConfig() {
  if (ssid.isEmpty()) {
    if (config.begin("config",true)){
      gatewayName=config.getString("gwn","");
      ssid=config.getString("ssid","");
      password=config.getString("pw","");
      mqttBrokerIP=config.getString("mbip","");
      mqttBrokerPort=config.getUShort("mbp",1883);
      mqttUser=config.getString("mu","");
      mqttPassword=config.getString("mpw","");
      mqtt_topic=config.getString("mt","");
      subscribeToWhitelist=config.getBool("mwl",false);
      mqtt_whitelist_topic=config.getString("mwlt","");
      boot_number=config.getUShort("bn",0);
      onlyWithServiceData=config.getBool("osd",false);
      stripServiceData=config.getBool("ssd",false);
    } else { 
      config.begin("config",false); // Nicht vorhanden: Kreiere das Namespace
    }
    config.end();
  }
#ifdef DEBUG
  Serial.printf("gatewayName = %s\n",gatewayName.c_str());
  Serial.printf("ssid        = %s\n",ssid.c_str());
  Serial.printf("password    = %s\n",password.c_str());
  Serial.printf("MQTTIP      = %s\n",mqttBrokerIP.c_str());
  Serial.printf("MQTTPort    = %d\n",mqttBrokerPort);
  Serial.printf("MQTTUser    = %s\n",mqttUser.c_str());
  Serial.printf("MQTTPW      = %s\n",mqttPassword.c_str());
  Serial.printf("MQTTtopic   = %s\n",mqtt_topic.c_str());
  Serial.printf("MQTTwltopic = %s\n",mqtt_whitelist_topic.c_str());
  Serial.printf("MQTTwluse   = %d\n",subscribeToWhitelist);
  Serial.printf("Only if SD  = %d\n",onlyWithServiceData);
  Serial.printf("Strip SD    = %d\n",stripServiceData);
#endif
}

void setupWebServer() {
#ifdef DEBUG
  Serial.println("Start Webserver");
#endif
  server.on("/", HTTP_GET, []() {
    String html = "<html><body>"
                  "<h1>ESP32 Configuration</h1>"
                  "<form action='/save' method='POST'>"
                  "<label>Gateway Name:</label><br>"
                  "<input type='text' name='gatewayName' value='"+gatewayName+"'><br>"
                  "<label>Wi-Fi SSID:</label><br>"
                  "<input type='text' name='ssid' value='"+ssid.c_str()+"'><br>"
                  "<label>Wi-Fi Password:</label><br>"
                  "<input type='password' name='password'><br>"
                  "<label>MQTT Broker IP:</label><br>"
                  "<input type='text' name='mqttBrokerIP' value='"+mqttBrokerIP.c_str()+"'><br>"
                  "<label>MQTT Broker Port:</label><br>"
                  "<input type='number' name='mqttBrokerPort' min='0' max='65535' value='1883'><br>"
                  "<label>MQTT User:</label><br>"
                  "<input type='text' name='mqttUser' value='"+mqttUser.c_str()+"'><br>"
                  "<label>MQTT Password:</label><br>"
                  "<input type='password' name='mqttPassword'><br>"
                  "<label>MQTT BLEtoMQTT-Topic:</label><br>"
                  "<input type='text' name='mqtttopic' value='"+mqtt_topic.c_str()+"'><br>"
                  "<label>Subscribe to Whitelist (experimental):</label><br>"
                  "<input type='checkbox' name='whitelist' value='1'><br>"
                  "<label>MQTT Whitelisttopic:</label><br>"
                  "<input type='text' name='mqttwhitelisttopic' value='"+mqtt_whitelist_topic.c_str()+"'><br>"
                  "<label>Forward only packets with servicedate:</label><br>"
                  "<input type='checkbox' name='onlywithservicedata' value='0'><br>"
                  "<label>Strip of all data, report only ID, RSSI, PHY, GW:</label><br>"
                  "<input type='checkbox' name='stripservicedata' value='0'><br>"
                  "<input type='submit' value='Save'>"
                  "</form></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    if (server.hasArg("gatewayName")) gatewayName = server.arg("gatewayName");
    if (server.hasArg("ssid")) ssid = server.arg("ssid");
    if (server.hasArg("password")) password = server.arg("password");
    if (server.hasArg("mqttBrokerIP")) mqttBrokerIP = server.arg("mqttBrokerIP");
    if (server.hasArg("mqttBrokerPort")) mqttBrokerPort = server.arg("mqttBrokerPort").toInt();
    if (server.hasArg("mqttUser")) mqttUser = server.arg("mqttUser");
    if (server.hasArg("mqtttopic")) mqtt_topic = server.arg("mqtttopic");
    if (server.hasArg("mqttPassword")) mqttPassword = server.arg("mqttPassword");
    subscribeToWhitelist = server.hasArg("whitelist");
    if (server.hasArg("mqttwhitelisttopic")) mqtt_whitelist_topic = server.arg("mqttwhitelisttopic");
    onlyWithServiceData = server.hasArg("onlywithservicedata");
    stripServiceData = server.hasArg("stripservicedata");

    boot_number=0;
    saveConfig();

    String html = String("<html><body>") +
                  "<h1>Configuration Saved</h1>" +
                  "<p>The device will restart now.</p>" +
                  "<p>Device sendet an MQTT-Topic: " + String(mqtt_topic) + "</p>" +
                  "<p>If whitelist was selected, it will listen for a whitelist "
                  " on MQTT-Topic: " + String(mqtt_whitelist_topic) + "</p>" +
                  "<p>After a reset the whitelist will be empty "
                  "i.e. all found BLE-devices are forwarded." +
                  "<br>" +
                  "<p>Configuration will be kept after restart so for new "
                  "config the device must be flashed anew</p>" +
                  "</body></html>";
    server.send(200, "text/html", html);
    delay(2000);
    ESP.restart();
  });
}

/////////////////////////////////////////////////////////////////////////
/////////////////////////// SET-UP //////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);

  loadConfig();
  boot_number++;
#ifdef DEBUG
  Serial.printf("Bootnumber : %d\n",boot_number);
#endif
  // Teste die Anzahl von Boots, wenn >5 lösche die Config und starte ganz neu
  if ((boot_number)>=5) {
    config.begin("config",true);
    config.clear();
    config.end();
    boot_number =0;
    ssid = "";
  } else {
    config.begin("config",false);
    config.putUShort("bn",boot_number);
#ifdef DEBUG
    Serial.printf("Schreibe neue Bootnummer %d\n",boot_number);
#endif
    config.end();
  }

  if (ssid.isEmpty()) {
    // Start as Access Point if SSID is not set
    WiFi.setSleep(false);
    WiFi.softAP(apSSID);
    WiFi.softAPConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
    delay(200);
    WiFi.softAP(apSSID, NULL,6);
    
#ifdef DEBUG
    IPAddress apIP = WiFi.softAPIP();
    Serial.println("Access Point started:");
    Serial.println(apIP);
#endif

    // Start web server for configuration
    setupWebServer();
    server.begin();
  } else {
    // WiFi
#ifdef DEBUG
    Serial.println("WiFi-Setup");
#endif
    wifi_setup();

    // MQTT
#ifdef DEBUG
    Serial.println("Set up MQTT Connection");
#endif
    MQTTclient.setServer(mqttBrokerIP.c_str(), mqttBrokerPort);
    //MQTTclient.setKeepAlive(15);
    //MQTTclient.setSocketTimeout(15);
    if (subscribeToWhitelist){
      MQTTclient.setCallback(mqtt_callback);
    }
    mqtt_reconnect();

    // Bluetooth
#ifdef DEBUG
    Serial.println("Scanning...");
#endif
    NimBLEDevice::init("");
    pBLEScan = NimBLEDevice::getScan(); //create new scan
    // Set the callback for when devices are discovered, no duplicates.
    //pBLEScan->setScanCallbacks (&scanCallbacks, false);  // no duplicates
    pBLEScan->setScanCallbacks(&scanCallbacks, true);  // with duplicates

    // Frühere Kombination, die auch einigermaßen funktioniert hat
    //pBLEScan->setActiveScan(true); // Set active scanning, this will get more data from the advertiser.
    //pBLEScan->setInterval(1000);
    //pBLEScan->setWindow(100);  // How long to scan during the interval; in milliseconds.

    // Experimentelle Konfiguration
    pBLEScan->setActiveScan(false); // braucht in meinem Szenario nur zu warten
    pBLEScan->setInterval(100); // How often the scan occurs / switches channels; in milliseconds,
    pBLEScan->setWindow(100);
    pBLEScan->setMaxResults(0); // do not store the scan results, use callback only.
    pBLEScan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
    pBLEScan->start(scanTimeMs, false, true);
  }
}

///////////////////////////////// MAIN Loop /////////////////////////////////////////
int ii=0;
const int del = 10;
void loop() {

  // Solange wie ssid nicht definiert ist: Bleibe in WiFi-AP-Modus
  if (ssid.isEmpty()) {
#ifdef DEBUG
    Serial.println("Before HandleClient");
#endif
    server.handleClient();
  } else {
    // If an error occurs that stops the scan, it will be restarted here.
    if(pBLEScan->isScanning() == false) {
        // Start scan with: duration = 0 seconds(forever), no scan end callback, not a continuation of a previous scan.
#ifdef DEBUG
        Serial.printf("Scan has stopped +++++++++++++++++++++++ Restarting\n");
#endif
        pBLEScan->start(scanTimeMs, false, true);
    }

    MQTTclient.loop();
#ifdef DEBUG1
    Serial.print("MQTT-Client State:");
    Serial.print(MQTTclient.state());
    Serial.println();
#endif
    if (!MQTTclient.connected()) {
      mqtt_reconnect();
    }

    //pBLEScan->setDuplicateFilter(true);

    // Mechanismus für Factory Reset: Wenn mehrere Resets in kurzer Folge, 
    // dann abspeichern, sonsot hochzählen, bis Zeit abgelaufen und reset
    if (boot_number > 1){
      ii++;
      if (ii*del > 5000) {
        ii=0;
        boot_number= 0;
        config.begin("config");
        config.putUShort("bn",boot_number);
        config.end();
      }
    }
#ifdef DEBUG1
    Serial.printf("Runde Nummer : %d, Boot Nummer : %d\n",ii,boot_number);
#endif
  }
  //delay(del);
}