/*
   Grow bird ESP32 client implementation

   Basic flow:
   Initial Startup (or configure button) ->
      Configure WIFI using ESP Wifi Manager - most of the code copieid from ESP WiFi Manager ConfigOnSwitch example file, MIT License https://github.com/khoih-prog/ESP_WiFiManager
      Connect to WiFi
      Check to see if the WeatherStation has been initialized OR has trouble logging in
      If not initialized OR can not login to Firebase:
        Start a webserver with a Firebase login dialog
        User logs in successfully
        Generate Firebase weather station name from "ESP" + the chipId to make it unique
        Generate the Firebase user email used for Authentication from the chip ID "ESP"+chipId+"@sourceauditor.com"
        Firebase function is called to register the WeatherBird for the logged in UserID
        The Firebase function will Initialize the Firebase database if it has not been initialized
        If the station has not been initialized AND the owner has the station as one of it's station, the password is reset
        Set the firebaseStationConfigured to true and attempt to login again
   Setup the display
   Setup sensors
   Begin measuring and reporting
   License: TBD - Proprietary for now
   Includes code from TJpg_decode libary example files
   Includes code from TFT_eSPI examples
   Follows code from TTGO-HiGrow
*/

// General
#define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())
String chipId = String(ESP_getChipId(), HEX);

// Time
#include <ezTime.h>

// Firebase
#include <FirebaseESP32.h>
FirebaseData firebaseData;
FirebaseAuth firebaseAuth;
FirebaseConfig firebaseConfig;
#define FIREBASE_API_KEY "AIzaSyBoXs2I_0ciXdz8BkArQ2jB7nryhfyod78"
#define FIREBASE_AUTH_DOMAIN "weather-2a8a7.firebaseapp.com"
#define FIREBASE_DATABASE_URL "https://weather-2a8a7.firebaseio.com"
#define FIREBASE_DATABASE_LOCATION "weather-2a8a7.firebaseio.com"
#define FIREBASE_STORAGE_BUCKET "weather-2a8a7.appspot.com"
String firebaseUserEmail = "ESP" + chipId + "@sourceauditor.com";
String firebaseStationId = "ESP" + chipId;
#define FIREBASE_XFER_MAX_DAYS 1
#define FIREBASE_MAX_NUM_SENSORS 50
#define FIREBASE_MAX_RETRY 1
#define FIREBASE_MAX_ERROR_QUEUE 30
#define INITIALIZE_FIREBASE_STATION_ENDPOINT "https://us-central1-weather-2a8a7.cloudfunctions.net/initializeStation"
#define MAX_OWNER_LEN           40
char owner[MAX_OWNER_LEN]     = "";

bool firebaseStationConfigured = false;

/* The helper function to get the token status string */
String getTokenStatus(struct token_info_t info);

#include <algorithm>
#include <Button2.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT12.h>
#include <Adafruit_BME280.h>

bool bme_found = false;

// WIFI provisioning - from ESP WiFi Manager ConfigOnSwitch example file, MIT License https://github.com/khoih-prog/ESP_WiFiManager
// Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define _WIFIMGR_LOGLEVEL_    3
#define DEBUG   true
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#include <SPIFFS.h>
FS* filesystem =      &SPIFFS;
#define FileFS        SPIFFS
#define FS_Name       "SPIFFS"
/* Trigger for inititating config mode is Pin D3 and also flash button on NodeMCU
   Flash button is convenient to use but if it is pressed it will stuff up the serial port device driver
   until the computer is rebooted on windows machines.
*/
const int CONFIG_BUTTON_PIN1 = 35;
// SSID and PW for Config Portal
const char* ssid = "WEATHERBIRD";
String password = "WBPASS01234";
const char* host = "weatherbird";
#define CONFIG_URL    "http://192.168.100.1"

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;
#define FORMAT_FILESYSTEM         false // Setting to true will force a format of the file system

#define MIN_AP_PASSWORD_SIZE    8

#define SSID_MAX_LEN            32
#define PASS_MAX_LEN            64

typedef struct {
  char wifi_ssid[SSID_MAX_LEN];
  char wifi_pw  [PASS_MAX_LEN];
}  WiFi_Credentials;

typedef struct {
  String wifi_ssid;
  String wifi_pw;
}  WiFi_Credentials_String;

#define NUM_WIFI_CREDENTIALS      2

typedef struct {
  WiFi_Credentials  WiFi_Creds [NUM_WIFI_CREDENTIALS];
  char firebase_password[PASS_MAX_LEN];
  char firebase_owner_id[MAX_OWNER_LEN];
  bool firebase_initialized;
} WM_Config;

WM_Config         WM_config;

#define  CONFIG_FILENAME              F("/wifi_cred.dat")
//////

// Indicates whether ESP has WiFi credentials saved from previous session
bool initialConfig = false;

// Use false if you don't like to display Available Pages in Information Page of Config Portal
// Comment out or use true to display Available Pages in Information Page of Config Portal
// Must be placed before #include <ESP_WiFiManager.h>
#define USE_AVAILABLE_PAGES     false

// Use false to disable NTP config. Advisable when using Cellphone, Tablet to access Config Portal.
// See Issue 23: On Android phone ConfigPortal is unresponsive (https://github.com/khoih-prog/ESP_WiFiManager/issues/23)
#define USE_ESP_WIFIMANAGER_NTP     false

// Use true to enable CloudFlare NTP service. System can hang if you don't have Internet access while accessing CloudFlare
// See Issue #21: CloudFlare link in the default portal (https://github.com/khoih-prog/ESP_WiFiManager/issues/21)
#define USE_CLOUDFLARE_NTP          false

#define USING_CORS_FEATURE          true
//////

#warning Using DHCP IP
IPAddress stationIP   = IPAddress(0, 0, 0, 0);
IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
IPAddress netMask     = IPAddress(255, 255, 255, 0);

IPAddress dns1IP      = gatewayIP;
IPAddress dns2IP      = IPAddress(8, 8, 8, 8);

IPAddress APStaticIP  = IPAddress(192, 168, 100, 1);
IPAddress APStaticGW  = IPAddress(192, 168, 100, 1);
IPAddress APStaticSN  = IPAddress(255, 255, 255, 0);

#include <ESP_WiFiManager.h>              //https://github.com/khoih-prog/ESP_WiFiManager

// Function Prototypes
uint8_t connectMultiWiFi(void);
WiFi_AP_IPConfig  WM_AP_IPconfig;
WiFi_STA_IPConfig WM_STA_IPconfig;

void initAPIPConfigStruct(WiFi_AP_IPConfig &in_WM_AP_IPconfig) {
  in_WM_AP_IPconfig._ap_static_ip   = APStaticIP;
  in_WM_AP_IPconfig._ap_static_gw   = APStaticGW;
  in_WM_AP_IPconfig._ap_static_sn   = APStaticSN;
}

void initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig) {
  in_WM_STA_IPconfig._sta_static_ip   = stationIP;
  in_WM_STA_IPconfig._sta_static_gw   = gatewayIP;
  in_WM_STA_IPconfig._sta_static_sn   = netMask;
  in_WM_STA_IPconfig._sta_static_dns1 = dns1IP;
  in_WM_STA_IPconfig._sta_static_dns2 = dns2IP;
}

void displayIPConfigStruct(WiFi_STA_IPConfig in_WM_STA_IPconfig) {
  LOGERROR3(F("stationIP ="), in_WM_STA_IPconfig._sta_static_ip, ", gatewayIP =", in_WM_STA_IPconfig._sta_static_gw);
  LOGERROR1(F("netMask ="), in_WM_STA_IPconfig._sta_static_sn);
  LOGERROR3(F("dns1IP ="), in_WM_STA_IPconfig._sta_static_dns1, ", dns2IP =", in_WM_STA_IPconfig._sta_static_dns2);
}

void configWiFi(WiFi_STA_IPConfig in_WM_STA_IPconfig) {
  Serial.println("Configuring WIFI!!!");
  // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
  WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn, in_WM_STA_IPconfig._sta_static_dns1, in_WM_STA_IPconfig._sta_static_dns2);
}

///////////////////////////////////////////

uint8_t connectMultiWiFi() {
  // For ESP32, this better be 0 to shorten the connect time
#define WIFI_MULTI_1ST_CONNECT_WAITING_MS       0
#define WIFI_MULTI_CONNECT_WAITING_MS           100L
  uint8_t status;

  LOGERROR(F("ConnectMultiWiFi with :"));

  if ( (Router_SSID != "") && (Router_Pass != "") ) {
    LOGERROR3(F("* Flash-stored Router_SSID = "), Router_SSID, F(", Router_Pass = "), Router_Pass );
  }

  for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
    // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
    if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) ) {
      LOGERROR3(F("* Additional SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
    }
  }

  LOGERROR(F("Connecting MultiWifi..."));

  WiFi.mode(WIFI_STA);

  int i = 0;
  status = wifiMulti.run();
  delay(WIFI_MULTI_1ST_CONNECT_WAITING_MS);

  while ( ( i++ < 20 ) && ( status != WL_CONNECTED ) ) {
    status = wifiMulti.run();

    if ( status == WL_CONNECTED )
      break;
    else
      delay(WIFI_MULTI_CONNECT_WAITING_MS);
  }

  if ( status == WL_CONNECTED ) {
    LOGERROR1(F("WiFi connected after time: "), i);
    LOGERROR3(F("SSID:"), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
    LOGERROR3(F("Channel:"), WiFi.channel(), F(",IP address:"), WiFi.localIP() );
  } else {
    LOGERROR(F("WiFi not connected"));
  }

  return status;
}

void check_WiFi(void) {
  if ( (WiFi.status() != WL_CONNECTED) ) {
    Serial.println("\nWiFi lost. Call connectMultiWiFi in loop");
    connectMultiWiFi();
  }
}

void loadConfigData() {
  File file = FileFS.open(CONFIG_FILENAME, "r");
  LOGERROR(F("LoadWiFiCfgFile "));

  memset(&WM_config,       0, sizeof(WM_config));

  // New in v1.4.0
  memset(&WM_STA_IPconfig, 0, sizeof(WM_STA_IPconfig));
  //////

  if (file) {
    file.readBytes((char *) &WM_config,   sizeof(WM_config));

    // New in v1.4.0
    file.readBytes((char *) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    //////

    file.close();
    LOGERROR(F("OK"));

    // New in v1.4.0
    displayIPConfigStruct(WM_STA_IPconfig);
    //////
    Serial.print("Firebase owner id: ");
    Serial.println(WM_config.firebase_owner_id);
  } else {
    LOGERROR(F("failed"));
  }
}

void saveConfigData() {
  File file = FileFS.open(CONFIG_FILENAME, "w");
  LOGERROR(F("SaveWiFiCfgFile "));

  if (file) {
    file.write((uint8_t*) &WM_config,   sizeof(WM_config));

    // New in v1.4.0
    file.write((uint8_t*) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    //////

    file.close();
    LOGERROR(F("OK"));
  } else {
    LOGERROR(F("failed"));
  }
}

// Web server for configuring the Firebase data
#include <WebServer.h>
#include "webpages.h"
WebServer server(80);
bool webFirebaseCancelled = false;
bool webFirebaseConfigured = false;

#define I2C_SDA             25
#define I2C_SCL             26
#define DHT12_PIN           16
#define BAT_ADC             33
#define SALT_PIN            34
#define SOIL_PIN            32
#define BOOT_PIN            0
#define POWER_CTRL          4
#define USER_BUTTON         35
#define DS18B20_PIN         21                  //18b20 data pin


BH1750 lightMeter(0x23); //0x23
Adafruit_BME280 bmp;     //0x77
DHT12 dht12(DHT12_PIN, true);
Button2 button(BOOT_PIN);
Button2 useButton(USER_BUTTON);

int firebase_update_counter = 0;


uint32_t readSalt()
{
    uint8_t samples = 120;
    uint32_t humi = 0;
    uint16_t array[120];

    for (int i = 0; i < samples; i++) {
        array[i] = analogRead(SALT_PIN);
        delay(2);
    }
    std::sort(array, array + samples);
    for (int i = 0; i < samples; i++) {
        if (i == 0 || i == samples - 1)continue;
        humi += array[i];
    }
    humi /= samples - 2;
    return humi;
}

uint16_t readSoil()
{
    uint16_t soil = analogRead(SOIL_PIN);
    return map(soil, 0, 4095, 100, 0);
}

float readBattery()
{
    int vref = 1100;
    uint16_t volt = analogRead(BAT_ADC);
    float battery_voltage = ((float)volt / 4095.0) * 2.0 * 3.3 * (vref);
    return battery_voltage;
}

/*
 * Update the value to firebase
 */
void update_value(String value_name, float val) {
    if (isnan(val)) {
      Serial.print("NAN Value for ");
      Serial.println(value_name);
      return;
    }
    String datetime = UTC.dateTime("Y-m-d\\TH:i:s\\Z");
    String last_update_value = "/stations/" + firebaseStationId + "/last_update/" + value_name + "/value";
    String last_update_datetime = "/stations/" + firebaseStationId + "/last_update/" + value_name + "/timestamp";
    String day_key = datetime.substring(0,10) + "T00:00:00Z";
    String dialy_sensor_data_val = "/stations/" + firebaseStationId + "/daily_data/" + day_key + "/sensor_data/" + value_name + "/" + datetime;
    if (!Firebase.setFloat(firebaseData, last_update_value, val) || 
        !Firebase.setString(firebaseData, last_update_datetime, datetime) ||
        !Firebase.setFloat(firebaseData, dialy_sensor_data_val, val)) {
      Serial.print("Failure storing ");
      Serial.print(value_name);
      Serial.print(" for value ");
      Serial.println(val);
      Serial.println(firebaseData.errorReason());
    }
}

void readSensors(bool updateFirebase) {
    float tempC = dht12.readTemperature();
    Serial.print("Temp: ");
    Serial.println(tempC);
    float humidity = dht12.readHumidity();
    Serial.print("Humidity: ");
    Serial.println(humidity);
    float lux = lightMeter.readLightLevel();
    uint16_t soil = readSoil();
    uint32_t salt = readSalt();
    float bat = readBattery();

    if (updateFirebase) {
      // store in Firebase
      update_value("t", tempC);
      update_value("hum", humidity);
      update_value("lux", lux);
      update_value("soil", float(soil));
      update_value("salt", float(salt));
    }
}
void check_status(void) {
  static ulong checkstatus_timeout  = 0;
  static ulong checkwifi_timeout    = 0;

  static ulong current_millis;

#define WIFICHECK_INTERVAL    1000L
#define HEARTBEAT_INTERVAL    10000L
#define FIREBASE_UPDATE_DELAY 300000L

  current_millis = millis();

  // Check WiFi every WIFICHECK_INTERVAL (1) seconds.
  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0)) {
    check_WiFi();
    checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
  }

  // Check sensor readings every 10 seconds
  if ((current_millis > checkstatus_timeout) || (checkstatus_timeout == 0)) {
    checkstatus_timeout = current_millis + HEARTBEAT_INTERVAL;
    if (firebase_update_counter <= 0) {
      // store in Firebase
      readSensors(true);
      firebase_update_counter = FIREBASE_UPDATE_DELAY / HEARTBEAT_INTERVAL;
    } else {
      readSensors(false);
      firebase_update_counter--;
    }
  }
}

// Display the Wifi config instructions
void displayWifiConfig() {
  Serial.println("Connect to Network: ");
  Serial.println(ssid);
  Serial.print("PW: ");
  Serial.println(password.c_str());
  Serial.println("then browse one of: ");
  Serial.println(CONFIG_URL);
  Serial.print("http://");
  Serial.print(host);
  Serial.println(".local");
}

/**
   Server the web pages to handle configuration page
*/
void handle_config_firebase() {
  Serial.println("Handling config");
  server.send(200, "text/html", deviceinit_page);
  // If the user signs in successfully, the handle_user_loggedin will be called
}

void handle_device_initializing() {
  Serial.println("Handling device initializing");
  server.send(200, "text/html", "TEMP");
}

void handle_NotFound() {
  server.send(404, "text/plain", "Page not found");
}

/**
   Generates a random password and stores the result in password
   Generates random ascill characters for the entire length
*/
void generateRandomPassword(char* password, int len) {
  int i = 0;
  while (i < len) {
    byte randomUpperLowerNum = random(0, 2);
    char ch;
    if (randomUpperLowerNum == 0) {
      byte rv = random(0, 26);
      ch = 'a' + rv;
    }
    if (randomUpperLowerNum == 1) {
      byte rv = random(0, 26);
      ch = 'A' + rv;
    }
    if (randomUpperLowerNum == 2) {
      byte rv = random(0, 9);
      ch = '0' + rv;
    }
    password[i++] = ch;
  }
  password[len] = 0;
}

/**
   Configures firebase by creating the station in the FireBase station
*/
bool initializeFirebaseStation(char* owner) {
  HTTPClient http; // This is only needed in this scope
  http.begin("https://us-central1-weather-2a8a7.cloudfunctions.net/initializeStation");
  http.addHeader("Content-Type", "application/json");
#define PW_LEN 12
  char firebaseUserPassword[PW_LEN + 1];
  generateRandomPassword(firebaseUserPassword, PW_LEN);
  int maxJsonLen = 100 + MAX_OWNER_LEN + sizeof(firebaseUserEmail) + sizeof(firebaseUserPassword) + sizeof(firebaseStationId);
  char json[maxJsonLen];
  snprintf_P(json, sizeof(json), PSTR("{\"firebaseUserEmail\":\"%s\",\"firebaseUserPassword\":\"%s\",\"firebaseStationId\":\"%s\",\"ownerId\":\"%s\"}"),
             firebaseUserEmail.c_str(), firebaseUserPassword, firebaseStationId.c_str(), owner);
  Serial.println(json);
  http.setConnectTimeout(15000); // 15 second timeout
  int httpResponseCode = http.POST(json);
  String response = http.getString();
  Serial.print("Firebase config response code: ");
  Serial.print(httpResponseCode);
  Serial.print(" response: ");
  Serial.println(response);
  if (httpResponseCode == 200) {
    Serial.print("Successfully initialized Firebase: ");
    Serial.println(response);
    strcpy(WM_config.firebase_password, firebaseUserPassword);
    strcpy(WM_config.firebase_owner_id, owner);
    WM_config.firebase_initialized = true;
    saveConfigData();
    http.end();
    return true;
  } else {
    Serial.print("Error initializing Firebase Station: ");
    Serial.print(httpResponseCode);
    Serial.println(response);
    http.end();
    return false;
  }
}

void handle_user_loggedin() {
  String userid = server.arg("uid");
  if (userid) {
    int len = userid.length() + 1;
    char userid_c[len];
    userid.toCharArray(userid_c, len);
    if (initializeFirebaseStation(userid_c)) {
      Serial.println("Initialized firebird station with owner UID: " + userid);
      server.send(200, "text/plain", "Successfully initialized FireBird station");
      webFirebaseCancelled = false;
      webFirebaseConfigured = true;
    } else {
      Serial.println("Error initializing firebird station with owner UID: " + userid);
      server.send(400, "text/plain", "Error initializing FireBird Station");
    }
  } else {
    // Assume cancelled
    webFirebaseCancelled = true;
  }
}

void handle_cancel() {
  webFirebaseCancelled = true;
  server.send(200, "text/html", cancel_page);
}

/**
   Displays instructions on the starting with msg then starts webserver to collect Firebase related configuration information
   Returns true if successful, false otherwise
*/
bool webConfigureFirebase(char* msg) {
  Serial.println(msg);
  Serial.println();
  Serial.println(" Connect to: ");
  Serial.println(WiFi.SSID());
  Serial.println("then browse either: ");
  Serial.print(host);
  Serial.println(".local");
  Serial.print("http://");
  Serial.println(WiFi.localIP());
  webFirebaseConfigured = false;
  webFirebaseCancelled = false;
  server.on("/", handle_config_firebase);
  server.onNotFound(handle_NotFound);
  server.on("/deviceinitializing.html", handle_device_initializing);
  server.on("/userloggedin.html", handle_user_loggedin);
  server.on("/cancel.html", handle_cancel);
  if (MDNS.begin(host)) {
    Serial.println("MDNS responder started");
  }
  server.begin();
  MDNS.addService("http", "tcp", 80);
  // Loop until the configuration has completed or cancelled
  while (!webFirebaseConfigured && !webFirebaseCancelled) {
    server.handleClient();
  }
  server.stop();
  return webFirebaseConfigured;
}

/**
   Starts the Firebase Database with the configured autentication
   Returns true if successful
*/
bool startFirebase() {
  firebaseConfig.host = FIREBASE_DATABASE_LOCATION;
  firebaseConfig.api_key = FIREBASE_API_KEY;
  firebaseAuth.user.email = firebaseUserEmail.c_str();
  firebaseAuth.user.password = WM_config.firebase_password;
  Firebase.reconnectWiFi(true);
  Firebase.setMaxRetry(firebaseData, FIREBASE_MAX_RETRY);
  Firebase.setMaxErrorQueue(firebaseData, FIREBASE_MAX_ERROR_QUEUE);
  firebaseData.setResponseSize(1024); //minimum size is 400 bytes
  Firebase.begin(&firebaseConfig, &firebaseAuth);
  struct token_info_t info = Firebase.authTokenInfo();
  int retrycount = 0;
  while (retrycount < 10 && info.status != token_status_ready) {
    delay(1000);
    Serial.printf("Waiting for Firebase Authentication. Status: %s\n", getTokenStatus(info).c_str());
    Serial.printf("Token error: %s\n\n", info.error.message.c_str());
    info = Firebase.authTokenInfo();
    retrycount++;
  }
  if (retrycount >= 10) {
    Serial.println("Unable to authenticate to Firebase");
    Serial.printf("Token error: %s\n\n", info.error.message.c_str());
    return false;
  } else {
    Serial.println("Successfully authenticated to Firebase");
    return true;
  }
}

void displayWifiStatus() {
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("connected. Local IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" Error connecting to WIFI");
  }
}

void displayErrorMsg(char* msg) {
  Serial.println(msg);
}

/**
   Check to see if Firebase is configured.  If not, enable the configuration portal
   Returns true if successfully configured, false if cancelled or failed
*/
bool checkConfigFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (firebaseStationConfigured) {
    return true;
  }
  if (!WM_config.firebase_initialized) {
    if (!webConfigureFirebase("Config WeatherBird")) {
      displayErrorMsg("Config cancelled");
      return false;
    }
  }
  while (!startFirebase()) {
    if (!webConfigureFirebase("Error connecting - please reconfigure")) {
      displayErrorMsg("Unable to configure");
      return false;
    }
  }
  firebaseStationConfigured = true;
  Serial.println(firebaseStationId);
  Serial.println(" connected");
}


void sleepHandler(Button2 &b)
{
    Serial.println("Enter Deepsleep ...");
    esp_sleep_enable_ext1_wakeup(GPIO_SEL_35, ESP_EXT1_WAKEUP_ALL_LOW);
    delay(1000);
    esp_deep_sleep_start();
}

void configHandler(Button2 &b)
{
    Serial.println("Config button pressed");
    bool firebaseInitialized = WM_config.firebase_initialized;   // Save this so that it is stored correctly on save config
    Serial.println("\nConfiguration portal requested.");

    //Local intialization. Once its business is done, there is no need to keep it around
    ESP_WiFiManager ESP_wifiManager(host);
    char resetOwner[15];
    ESP_WMParameter resetOwnerParam("resetOwner", "Reset Station Owner", resetOwner, 15, "placeholder=\"NotReset\" type=\"checkbox\"");
    ESP_wifiManager.addParameter(&resetOwnerParam);
    ESP_wifiManager.setMinimumSignalQuality(-1);
    ESP_wifiManager.setConfigPortalChannel(0);

#if USING_CORS_FEATURE
    ESP_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif
    //Check if there is stored WiFi router/password credentials.
    //If not found, device will remain in configuration mode until switched off via webserver.
    Serial.print("Opening configuration portal. ");
    Router_SSID = ESP_wifiManager.WiFi_SSID();
    Router_Pass = ESP_wifiManager.WiFi_Pass();

    // From v1.1.0, Don't permit NULL password
    if ( (Router_SSID != "") && (Router_Pass != "") ) {
      ESP_wifiManager.setConfigPortalTimeout(120); //If no access point name has been previously entered disable timeout.
      Serial.println("Got stored Credentials. Timeout 120s");
    }
    else
      Serial.println("No stored Credentials. No timeout");
    ESP_wifiManager.setAPStaticIPConfig(WM_AP_IPconfig);
    //Starts an access point
    //and goes into a blocking loop awaiting configuration
    displayWifiConfig();

    if (!ESP_wifiManager.startConfigPortal((const char *) ssid, password.c_str())) {
      Serial.println("Not connected to WiFi but continuing anyway.");
    } else {
      Serial.println("WiFi connected...yeey :)");
      Serial.println(WiFi.SSID());
    }

    // Only clear then save data if CP entered and with new valid Credentials
    // No CP => stored getSSID() = ""
    if ( String(ESP_wifiManager.getSSID(0)) != "" && String(ESP_wifiManager.getSSID(1)) != "" ) {
      // Stored  for later usage, from v1.1.0, but clear first
      memset(&WM_config, 0, sizeof(WM_config));

      for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        String tempSSID = ESP_wifiManager.getSSID(i);
        String tempPW   = ESP_wifiManager.getPW(i);

        if (strlen(tempSSID.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1) {
          strcpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
        } else {
          strncpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);
        }

        if (strlen(tempPW.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1) {
          strcpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str());
        } else {
          strncpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);
        }

        // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
        if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) ) {
          LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
          wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
        }
      }
      ESP_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig);
      displayIPConfigStruct(WM_STA_IPconfig);
      displayWifiStatus();
      WM_config.firebase_initialized = firebaseInitialized;
      saveConfigData();
    }
    Serial.print("Reset owner len: ");
    Serial.print(strlen(resetOwnerParam.getValue()));
    Serial.print(" value: '");
    Serial.print(resetOwnerParam.getValue());
    Serial.println("'");
    //TODO: reconfigure the wifi if checked
    if (false) {
      webConfigureFirebase("Owner Reset Requst");
    }
    if (checkConfigFirebase()) {
      displayWifiStatus();
    } else {
      displayErrorMsg("Unable to connect to FireBase");
    }
}

void setup()
{
    Serial.begin(115200);
    Wire.begin(I2C_SDA, I2C_SCL);
    button.setLongClickHandler(configHandler);
    useButton.setLongClickHandler(sleepHandler);
    
    if (FORMAT_FILESYSTEM) {
      Serial.println(F("Forced Formatting."));
      FileFS.format();
    }
    if (!FileFS.begin(true)) {
      Serial.print(FS_Name);
      Serial.println(F(" failed! AutoFormatting."));
    }
    unsigned long startedAt = millis();
    initAPIPConfigStruct(WM_AP_IPconfig);
    initSTAIPConfigStruct(WM_STA_IPconfig);
    //Local intialization. Once its business is done, there is no need to keep it around
    ESP_WiFiManager ESP_wifiManager(host);
  
    ESP_wifiManager.setDebugOutput(DEBUG);
  
    // Use only to erase stored WiFi Credentials
    //resetSettings();
    //ESP_wifiManager.resetSettings();
    ESP_wifiManager.setAPStaticIPConfig(WM_AP_IPconfig);
    ESP_wifiManager.setMinimumSignalQuality(-1);
    // Set config portal channel, default = 1. Use 0 => random channel from 1-13
    ESP_wifiManager.setConfigPortalChannel(0);
  #if USING_CORS_FEATURE
    ESP_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
  #endif
    // We can't use WiFi.SSID() in ESP32as it's only valid after connected.
    // SSID and Password stored in ESP32 wifi_ap_record_t and wifi_config_t are also cleared in reboot
    // Have to create a new function to store in EEPROM/SPIFFS for this purpose
    Router_SSID = ESP_wifiManager.WiFi_SSID();
    Router_Pass = ESP_wifiManager.WiFi_Pass();
    if ( (Router_SSID != "") && (Router_Pass != "") ) {
      LOGERROR3(F("* Add SSID = "), Router_SSID, F(", PW = "), Router_Pass);
      wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());
  
      ESP_wifiManager.setConfigPortalTimeout(120); //If no access point name has been previously entered disable timeout.
      Serial.println("Got stored Credentials. Timeout 120s for Config Portal");
    } else {
      Serial.println("Open Config Portal without Timeout: No stored Credentials.");
      initialConfig = true;
    }
  
    if (initialConfig) {
      Serial.println("Starting configuration portal.");
  
      //sets timeout in seconds until configuration portal gets turned off.
      //If not specified device will remain in configuration mode until
      //switched off via webserver or device is restarted.
      //ESP_wifiManager.setConfigPortalTimeout(600);
  
      // Starts an access point
      displayWifiConfig();
      if (!ESP_wifiManager.startConfigPortal(ssid, password.c_str())) {
        Serial.println("Not connected to WiFi but continuing anyway.");
      } else {
        Serial.println("WiFi connected...yeey :)");
        Serial.println(WiFi.SSID());
      }
  
      // Stored  for later usage, from v1.1.0, but clear first
      memset(&WM_config, 0, sizeof(WM_config));
  
      for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        String tempSSID = ESP_wifiManager.getSSID(i);
        String tempPW   = ESP_wifiManager.getPW(i);
  
        if (strlen(tempSSID.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1)
          strcpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
        else
          strncpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);
  
        if (strlen(tempPW.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1)
          strcpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str());
        else
          strncpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);
  
        // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
        if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
        {
          LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
          wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
        }
      }
      WM_config.firebase_initialized = false;
      ESP_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig);
      displayIPConfigStruct(WM_STA_IPconfig);
  
      saveConfigData();
    }
    startedAt = millis();
  
    if (!initialConfig) {
      // Load stored data, the addAP ready for MultiWiFi reconnection
      loadConfigData();
  
      for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
        if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) ) {
          LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
          wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
        }
      }
  
      if ( WiFi.status() != WL_CONNECTED ) {
        Serial.println("ConnectMultiWiFi in setup");
        connectMultiWiFi();
      }
    }
  
    Serial.print("After waiting ");
    Serial.print((float) (millis() - startedAt) / 1000L);
    Serial.print(" secs more in setup(), connection result is ");
  
    displayWifiStatus();
      
    waitForSync();    // Sync the time to NTP
    Serial.println("UTC: " + UTC.dateTime());
    
    if (checkConfigFirebase()) {
      Serial.println("Firebase up and running");
    } else {
      Serial.println("Firebase not initialized");
    }
    dht12.begin();

    //! Sensor power control pin , use deteced must set high
    pinMode(POWER_CTRL, OUTPUT);
    digitalWrite(POWER_CTRL, 1);
    delay(1000);
    //NOTE: Even if the BMP is not present, removing this call will cause the lightMeter begin to fail for some unknown reason
    if (!bmp.begin()) {
        Serial.println(F("Could not find a valid BMP280 sensor, check wiring!"));
        bme_found = false;
    } else {
        bme_found = true;
    }
    if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
        Serial.println(F("BH1750 Advanced begin"));
    } else {
        Serial.println(F("Error initialising BH1750"));
    }
}

void loop()
{
    static uint64_t timestamp;
    button.loop();
    useButton.loop();
    events();  // This is for the timer facility
    check_status();
}


/* The helper function to get the token status string - copied from Firebase-ESP8266 license MIT */
String getTokenStatus(struct token_info_t info) {
  switch (info.status) {
    case token_status_uninitialized:
      return "uninitialized";

    case token_status_on_signing:
      return "on signing";

    case token_status_on_request:
      return "on request";

    case token_status_on_refresh:
      return "on refreshing";

    case token_status_ready:
      return "ready";

    case token_status_error:
      return "error";

    default:
      break;  
  }
  return "uninitialized";
}
