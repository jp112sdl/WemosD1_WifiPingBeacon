#include <ESP8266WiFi.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <FS.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

//don't touch
bool shouldSaveConfig        = false;
String configJsonFile        = "config.json";
bool wifiManagerDebugOutput  = true;
char ip[15]      = "0.0.0.0";
char netmask[15] = "0.0.0.0";
char gw[15]      = "0.0.0.0";

int configKey = 13; //D8
int configLED = 0; //D0

int loopCount = 0;

void setup() {
  Serial.begin(9600);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(configKey,   INPUT_PULLUP); 
  pinMode(configLED, OUTPUT);
  digitalWrite(configLED, HIGH);
  
  loadSystemConfig();
  doWifiConnect();
  printWifiStatus();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    loopCount++;
    if (loopCount == 500) {
      loopCount = 0;
      digitalWrite(LED_BUILTIN, LOW);
      delay(30);
      digitalWrite(LED_BUILTIN, HIGH);
    }
  } else {
    doWifiConnect();
  }
  delay(10);
}

bool doWifiConnect() {
  String _ssid = WiFi.SSID();
  String _psk = WiFi.psk();

  const char* ipStr = ip; byte ipBytes[4]; parseBytes(ipStr, '.', ipBytes, 4, 10);
  const char* netmaskStr = netmask; byte netmaskBytes[4]; parseBytes(netmaskStr, '.', netmaskBytes, 4, 10);
  const char* gwStr = gw; byte gwBytes[4]; parseBytes(gwStr, '.', gwBytes, 4, 10);

  Serial.println("Taster: " + String(digitalRead(configKey)));
  if (_ssid != "" && _psk != "" && digitalRead(configKey) != LOW) {
    Serial.println("ConfigKey nicht gedrückt, SSID ("+_ssid+") und PSK vorhanden");
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.config(IPAddress(ipBytes[0], ipBytes[1], ipBytes[2], ipBytes[3]), IPAddress(gwBytes[0], gwBytes[1], gwBytes[2], gwBytes[3]), IPAddress(netmaskBytes[0], netmaskBytes[1], netmaskBytes[2], netmaskBytes[3]));
    WiFi.begin(_ssid.c_str(), _psk.c_str());
    int waitCounter = 0;
    digitalWrite(LED_BUILTIN, LOW);
    while (WiFi.status() != WL_CONNECTED) {
      waitCounter++;
      if (waitCounter == 30) {
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println("Wifi connect nicht erfolgreich, starte ESP neu");
        ESP.restart();
      }
      delay(500);
    }
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("Wifi Connected (classic)");
    return true;
  }

  if (_ssid == "" || _psk == "" || digitalRead(configKey) == LOW) {
    WiFiManager wifiManager;
    digitalWrite(configLED, LOW);

    wifiManager.resetSettings();
    wifiManager.setDebugOutput(wifiManagerDebugOutput);
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    WiFiManagerParameter custom_ip("custom_ip", "IP-Adresse", "", 15);
    WiFiManagerParameter custom_netmask("custom_netmask", "Netzmaske", "", 15);
    WiFiManagerParameter custom_gw("custom_gw", "Gateway", "", 15);
    WiFiManagerParameter custom_text("<br/><br>Statische IP (wenn leer, dann DHCP):");
    wifiManager.addParameter(&custom_text);
    wifiManager.addParameter(&custom_ip);
    wifiManager.addParameter(&custom_netmask);
    wifiManager.addParameter(&custom_gw);

    wifiManager.setSTAStaticIPConfig(IPAddress(ipBytes[0], ipBytes[1], ipBytes[2], ipBytes[3]), IPAddress(gwBytes[0], gwBytes[1], gwBytes[2], gwBytes[3]), IPAddress(netmaskBytes[0], netmaskBytes[1], netmaskBytes[2], netmaskBytes[3]));

    String Hostname = "WifiPingBeacon-" + WiFi.macAddress();
    char a[] = "";
    Hostname.toCharArray(a, 30);

    if (!wifiManager.startConfigPortal(a)) {
      Serial.println("failed to connect and hit timeout");
      delay(1000);
      ESP.restart();
    }


    Serial.println("Wifi Connected (wifimanager)");
    Serial.println("CUSTOM IP: " + String(ip) + " Netmask: " + String(netmask) + " GW: " + String(gw));
    if (shouldSaveConfig) {
      SPIFFS.begin();
      Serial.println("saving config");
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      if (String(custom_ip.getValue()).length() > 5) {
        Serial.println("Custom IP Address is set!");
        strcpy(ip, custom_ip.getValue());
        strcpy(netmask, custom_netmask.getValue());
        strcpy(gw, custom_gw.getValue());
      } else {
        strcpy(ip,      "0.0.0.0");
        strcpy(netmask, "0.0.0.0");
        strcpy(gw,      "0.0.0.0");
      }
      json["ip"] = ip;
      json["netmask"] = netmask;
      json["gw"] = gw;
      SPIFFS.remove("/" + configJsonFile);
      File configFile = SPIFFS.open("/" + configJsonFile, "w");
      if (!configFile) {
        Serial.println("failed to open config file for writing");
      }

      json.printTo(Serial);
      json.printTo(configFile);
      configFile.close();

      SPIFFS.end();
      delay(100);
      ESP.restart();
    }

    return true;
  }
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("AP-Modus ist aktiv!");
  //Ausgabe, dass der AP Modus aktiv ist
}

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void parseBytes(const char* str, char sep, byte* bytes, int maxBytes, int base) {
  for (int i = 0; i < maxBytes; i++) {
    bytes[i] = strtoul(str, NULL, base);  // Convert byte
    str = strchr(str, sep);               // Find next separator
    if (str == NULL || *str == '\0') {
      break;                            // No more separators, exit
    }
    str++;                                // Point to next character after separator
  }
}

bool loadSystemConfig() {
  Serial.println("mounting FS...");
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/" + configJsonFile)) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/" + configJsonFile, "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(ip,      json["ip"]);
          strcpy(netmask, json["netmask"]);
          strcpy(gw,      json["gw"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
      return true;
    } else {
      Serial.println("/" + configJsonFile + " not found.");
      return false;
    }
    SPIFFS.end();
  } else {
    Serial.println("failed to mount FS");
    return false;
  }
}

void printWifiStatus() {
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

