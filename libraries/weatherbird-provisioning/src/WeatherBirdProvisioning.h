/**
 * Based on https://github.com/khoih-prog/ESPAsync_WiFiManager_Lite - MIT License
 * 
 * Provisioning library for the ESP32 WeatherBird
 * Configures the WiFi and Firebase connections
 * 
 * While configuring WiFi, connection is made in StationMode.  Once WiFi is configure
 * access should be made through the AccessPoint mode.
 * 
 * Station mode can be re-enabled for a period of time by long-pressing the provision button
 * 
 */

#ifndef WeatherBirdProvisioning_h
#define WeatherBirdProvisioning_h

#include <AceButton.h>
using namespace ace_button;
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include "FS.h"
#include <SPIFFS.h>
#include <DNSServer.h>
#include <memory>
#include <algorithm>
#include <esp_wifi.h>
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include "WeatherBird_Prov_Debug.h"
#include "webpages.h"

#define LED_OFF           LOW
#define LED_ON            HIGH
#define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())
#define HTTP_PORT         80
#define TIMEOUT_CONNECT_WIFI      30000
#define FORCE_CONFIG_INIT false    // Re-initializes the configuration and DOES NOT read the data from the file system
#define SECONDS_IN_ACCESS_POINT_MODE  3600  // 1 hour in access point mode

#define SSID_MAX_LEN      32
// WPA2 passwords can be up to 63 characters long.
#define PASS_MAX_LEN      64
#define MAX_OWNER_LEN     40
typedef struct
{
  char wifi_ssid[SSID_MAX_LEN];
  char wifi_pw  [PASS_MAX_LEN];
}  WiFi_Credentials;

#define NUM_WIFI_CREDENTIALS      2
#define WM_NO_CONFIG              "blank"
#define DNS_LOCAL_NAME            "weatherbird"
#define HTTP_PORT                 80

typedef struct Configuration
{
  WiFi_Credentials  WiFi_Creds [NUM_WIFI_CREDENTIALS];
  char firebase_password [PASS_MAX_LEN];
  char firebase_owner_id [MAX_OWNER_LEN];
  bool firebase_initialized;
  int  checkSum;
} WeatherBird_Configuration;

uint16_t CONFIG_DATA_SIZE = sizeof(WeatherBird_Configuration);

/////////////
// States
/////////////
#define STATE_NO_WIFI           0   // Wifi has not yet connected, must configure wifi through the access point web server
#define STATE_CONFIGURING_WIFI  1   // Web page for configuring wifi has been displayed, but data hasn't been entered
#define STATE_WIFI_NO_FIREBASE  2   // Wifi has been configured and connected, but firebase has not been configured
#define STATE_CONFIGURING_FB    3   // Firebase configuration page displayed, but not yet configured
#define STATE_CONFIG_FB_CANCELLED 4 // User cancelled out of firebase configuration
#define STATE_CONFIGURED        5   // Everything has been configured
#define STATE_RECONFIG_REQ      6   // A reconfiguration request has been made
#define STATE_FIREBASE_INIT_ERROR 7 // Error returned from initialize firebase

class WeatherBirdProvisioningManager: public IEventHandler {
  public:

  /**
   * LED pin to be lit when in configuration mode, provision button - pin for button when pressed turns on the station WIFI
   */
  WeatherBirdProvisioningManager(char* firebaseFunctionUrl, uint8_t ledPin, uint8_t provisionButtonPin) {
    WBP_LOGDEBUG3(F("LedPin: "), ledPin, F("Provision Button: "), provisionButtonPin);
    _ledPin = ledPin;
    _provisionButtonPin = provisionButtonPin;
    _firebaseFunctionUrl = firebaseFunctionUrl;
  }

  /**
   * Called to initialize the networking for the provision manager
   */
  void begin() {
    Serial.println("In begin");
    pinMode(_ledPin, OUTPUT);
    digitalWrite(_ledPin, LED_OFF);
    provisionButton.init(_provisionButtonPin);
    pinMode(_provisionButtonPin, INPUT_PULLUP);
    ButtonConfig* buttonConfig = provisionButton.getButtonConfig();
    buttonConfig->setIEventHandler(this);
    buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
    buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
    
    String _hostname = "ES32-" + String(ESP_getChipId(), HEX);
    _hostname.toUpperCase();
    setRFC952_hostname(_hostname.c_str());
    WBP_LOGINFO1(F("Hostname="), RFC952_hostname);
    getConfigData();
    isForcedConfigPortal = isForcedCP();

    enterStationMode();
    
    // Initialize firebase configured
    // Add the config networks
    bool foundWifiCred = false;
    for (uint16_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
      if (strcmp(weatherBirdConfig.WiFi_Creds[i].wifi_ssid, WM_NO_CONFIG) != 0 && strlen(weatherBirdConfig.WiFi_Creds[i].wifi_ssid) > 0) {
        WBP_LOGINFO1(F("Adding SSID: "), weatherBirdConfig.WiFi_Creds[i].wifi_ssid);
        wifiMulti.addAP(weatherBirdConfig.WiFi_Creds[i].wifi_ssid, weatherBirdConfig.WiFi_Creds[i].wifi_pw);
        foundWifiCred = true;
      }
    }
    if (foundWifiCred && connectMultiWiFi() == WL_CONNECTED) {
      WBP_LOGINFO(F("Connected to local WIFI"));
      if (weatherBirdConfig.firebase_initialized) {
        changeState(STATE_CONFIGURED);
      } else {
        changeState(STATE_WIFI_NO_FIREBASE);
      }
    } else {
      WBP_LOGERROR(F("Error: Unable to connect to local WIFI"));
      changeState(STATE_NO_WIFI);
      enterAccessPointMode();
    }

    WBP_LOGINFO1(F("Initial state: "), configurationState);
    startServer();
    if (MDNS.begin(RFC952_hostname)) {
      WBP_LOGINFO1(F("MDNS responder started on "),RFC952_hostname);
    }
    MDNS.addService("http", "tcp", 80);
  }

  /**
   * This should be called within the loop of the program - checks status of the network
   */
  void loop() {
    provisionButton.check();
    if (delayedStationMode) {
      if (millis() >= timeToSwitchToStation) {
        delayedStationMode = false;
        enterStationMode();
      }
    }
  }
  
  /**
   * Returns the generated station used to log into Firebase
  **/
  const char* getStationEmail() {
	const char * retval = firebaseUserEmail.c_str();
	return retval;
  }
  
  /**
   * Returns the station password used to authenticate the station in Firebase
  **/
  char* getStationPassword() {
	  return weatherBirdConfig.firebase_password;
  }
  
  /**
   * Returns the current state of the device (see STATE defines at the start of this file)
  **/
  uint16_t getState() {
    if (WiFi.status() != WL_CONNECTED && configurationState != STATE_NO_WIFI) {
      changeState(STATE_NO_WIFI);
    }
	return configurationState;
  }
 
  /**
   * Returns the ID of the station
  **/
  String getFirebaseStationId() {
	  return firebaseStationId;
  }

  /**
   * Handler for button presses
   */
  void handleEvent(AceButton* /* button */, uint8_t eventType, uint8_t buttonState) {
    WBP_LOGDEBUG3(F("Provision button state "), buttonState, F(", eventType "),eventType);
    if (eventType == AceButton::kEventLongPressed) {
      WBP_LOGINFO(F("Config button pressed"));
      enterAccessPointMode();
      changeState(STATE_CONFIGURING_WIFI);
      delayEnterStationMode(SECONDS_IN_ACCESS_POINT_MODE);
    }
  }

  private:
  unsigned long timeToSwitchToStation;
  bool delayedStationMode = false;
  uint8_t _ledPin;
  uint8_t _provisionButtonPin;
  AceButton provisionButton;
  char softAccessPointSSID[SSID_MAX_LEN];
  char softAccessPointPW[PASS_MAX_LEN];
  AsyncWebServer *server = NULL;
  WiFiMulti wifiMulti;
  String firebaseUserEmail = "ESP" + String(ESP_getChipId(), HEX) + "@sourceauditor.com";
  String firebaseStationId = "ESP" + String(ESP_getChipId(), HEX);
  char* _firebaseFunctionUrl;
  uint16_t configurationState = STATE_NO_WIFI;

  unsigned long configTimeout;
  
  bool isForcedConfigPortal   = false;
  WeatherBird_Configuration weatherBirdConfig;
#define  CONFIG_FILENAME                  ("/wm_config.dat")
#define  CONFIG_FILENAME_BACKUP           ("/wm_config.bak")

#define  CREDENTIALS_FILENAME             ("/wm_cred.dat")
#define  CREDENTIALS_FILENAME_BACKUP      ("/wm_cred.bak")

#define  CONFIG_PORTAL_FILENAME           ("/wm_cp.dat")
#define  CONFIG_PORTAL_FILENAME_BACKUP    ("/wm_cp.bak")
  
#define RFC952_HOSTNAME_MAXLEN      24
    
  char RFC952_hostname[RFC952_HOSTNAME_MAXLEN + 1];

  void changeState(uint16_t newState) {
    WBP_LOGDEBUG3(F("Changing state from:"),configurationState, F(" to:"), newState);
    configurationState = newState;
  }

/**
 * Enters access point mode to enable direct connection to the ESP32 for configuration
 */
  void enterAccessPointMode() {
    bool wasConnected = WiFi.status() == WL_CONNECTED;
    if (wasConnected) {
      WiFi.disconnect();
    }
    WiFi.mode(WIFI_MODE_APSTA);
    String soft_ap_ssid = "WEATHERBIRD" + String(ESP_getChipId(), HEX);
    String soft_ap_password = "WBPASS" + String(ESP_getChipId(), HEX);
    strcpy(softAccessPointSSID, soft_ap_ssid.c_str());
    strcpy(softAccessPointPW, soft_ap_password.c_str());
 #define MAX_WIFI_CHANNEL 11
    int channel = (millis() % MAX_WIFI_CHANNEL) + 1; 
    if (WiFi.softAP(softAccessPointSSID, softAccessPointPW, channel)) {
      WBP_LOGINFO1(F("Access Point IP="), WiFi.softAPIP());
    } else {
      WBP_LOGERROR(F("Error: Unable to start soft access point"));
    }
    if (wasConnected) {
      if (connectMultiWiFi() != WL_CONNECTED) {
        WBP_LOGERROR(F("Lost connection while switching to Access Point Mode"));
      }
    }
  }

/**
 * Enters station mode for general use
 */
  void enterStationMode() {
    bool wasConnected = WiFi.status() == WL_CONNECTED;
    if (wasConnected) {
      WiFi.disconnect();
    }
    WiFi.mode(WIFI_MODE_STA);
    if (wasConnected) {
      if (connectMultiWiFi() != WL_CONNECTED) {
        WBP_LOGERROR(F("Lost connection while switching to Station Mode"));
      }
    }
  }

  /**
   * Enter station mode after seconds
   * 
  */
  void delayEnterStationMode(ushort seconds) {
    WBP_LOGDEBUG1(F("Delay entering station mode for "),seconds);
    timeToSwitchToStation = millis() + 1000 * seconds;
    delayedStationMode = true;
  }
  /**
   * Sets the global hostname and returns the current host name
   */
  char* setRFC952_hostname(const char* iHostname) {
    memset(RFC952_hostname, 0, sizeof(RFC952_hostname));
    size_t len = ( RFC952_HOSTNAME_MAXLEN < strlen(iHostname) ) ? RFC952_HOSTNAME_MAXLEN : strlen(iHostname);
    size_t j = 0;
    for (size_t i = 0; i < len - 1; i++)
    {
      if ( isalnum(iHostname[i]) || iHostname[i] == '-' )
      {
        RFC952_hostname[j] = iHostname[i];
        j++;
      }
    }
    // no '-' as last char
    if ( isalnum(iHostname[len - 1]) || (iHostname[len - 1] != '-') )
      RFC952_hostname[j] = iHostname[len - 1];
    return RFC952_hostname;
  }

  /**
   * Connects to the public network using an external access point.
   */
  uint8_t connectMultiWiFi() {
#define WIFI_MULTI_CONNECT_WAITING_MS      3000L
    uint8_t status;
    WBP_LOGINFO(F("Connecting MultiWifi..."));
    int i = 0;
    status = wifiMulti.run();
    delay(WIFI_MULTI_CONNECT_WAITING_MS);

    while ( ( i++ < 10 ) && ( status != WL_CONNECTED ) ) {
      status = wifiMulti.run();

      if ( status == WL_CONNECTED ) {
        break;
      } else {
        delay(WIFI_MULTI_CONNECT_WAITING_MS);
      }
    }
    if ( status == WL_CONNECTED ) {
      WBP_LOGWARN1(F("WiFi connected after time: "), i);
      WBP_LOGWARN3(F("SSID="), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
      WBP_LOGWARN3(F("Channel="), WiFi.channel(), F(",IP="), WiFi.localIP() );
    } else {
      WBP_LOGERROR(F("WiFi not connected"));
    }
    return status;
  }

  ////////////////////////////
  // The following are handlers for the web server
  // Web page content is defined in webpages.h
  ////////////////////////////
  void handle_test(AsyncWebServerRequest * request) {
    if (ON_STA_FILTER(request)) {
      request->send(200, "text/plain", "Hello from STA");
      return;
 
    } else if (ON_AP_FILTER(request)) {
      request->send(200, "text/plain", "Hello from AP");
      return;
    }
 
    request->send(200, "text/plain", "Hello from undefined");
  }
  
  void handle_device_initializing(AsyncWebServerRequest * request) {
    Serial.println("Handling device initializing");
    request->send(200, "text/html", "TEMP");
  }
  
  void handle_NotFound(AsyncWebServerRequest * request) {
    WBP_LOGDEBUG1(F("Page not found: "), request->url());
    request->send(404, "text/plain", "Page not found");
  }
  
  void handle_user_loggedin(AsyncWebServerRequest * request) {
    AsyncWebParameter* useridParam = request->getParam("uid");
    if (useridParam) {
      String userid = useridParam->value();
      WBP_LOGDEBUG1(F("User logged in: "),userid);
      int len = userid.length() + 1;
      char userid_c[len];
      userid.toCharArray(userid_c, len);
      request->send(200, "text/plain", "Initializing WeatherBird station");
      if (initializeFirebaseStation(userid_c)) {
        WBP_LOGINFO1(F("Initialized firebird station with owner UID: "), userid);
        changeState(STATE_CONFIGURED);
      } else {
        WBP_LOGERROR1(F("Error initializing firebird station with owner UID: "), userid);
        changeState(STATE_FIREBASE_INIT_ERROR);
      }
    } else {
      // Assume cancelled
      changeState(STATE_CONFIG_FB_CANCELLED);
    }
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
      Serial.print(ch);
    }
    Serial.println();
    password[len] = 0;
  }

  /**
     Configures firebase by creating the station in the FireBase station
  */
  bool initializeFirebaseStation(char* owner) {
    HTTPClient http; // This is only needed in this scope
    http.begin(_firebaseFunctionUrl);
    http.addHeader("Content-Type", "application/json");
#define PW_LEN 12
    char firebaseUserPassword[PW_LEN + 1];
    generateRandomPassword(firebaseUserPassword, PW_LEN);
    int maxJsonLen = 100 + MAX_OWNER_LEN + sizeof(firebaseUserEmail) + sizeof(firebaseUserPassword) + sizeof(firebaseStationId);
    char json[maxJsonLen];
    snprintf_P(json, sizeof(json), PSTR("{\"firebaseUserEmail\":\"%s\",\"firebaseUserPassword\":\"%s\",\"firebaseStationId\":\"%s\",\"ownerId\":\"%s\"}"),
               firebaseUserEmail.c_str(), firebaseUserPassword, firebaseStationId.c_str(), owner);
    WBP_LOGDEBUG(json);
    http.setConnectTimeout(15000); // 15 second timeout
    int httpResponseCode = http.POST(json);
    String response = http.getString();
    WBP_LOGDEBUG3(F("Firebase config response code: "), httpResponseCode, F(" response: "), response);
    if (httpResponseCode == 200) {
      WBP_LOGDEBUG1(F("Successfully initialized Firebase: "), response);
      strcpy(weatherBirdConfig.firebase_password, firebaseUserPassword);
      strcpy(weatherBirdConfig.firebase_owner_id, owner);
      weatherBirdConfig.firebase_initialized = true;
      saveConfigData();
      http.end();
      return true;
    } else {
      WBP_LOGERROR3(F("Error initializing Firebase Station: "), httpResponseCode, F("; Response "), response);
      http.end();
      return false;
    }
  }
  
  
  void handle_cancel(AsyncWebServerRequest * request) {
    changeState(STATE_CONFIG_FB_CANCELLED);
    request->send(200, "text/html", cancel_page);
  }

  /**
   * Handles a configuration request for the Firebase page
   */
  void handle_config_firebase(AsyncWebServerRequest * request) {
    WBP_LOGDEBUG(F("Handling config"));
    if (configurationState == STATE_CONFIGURING_FB || configurationState == STATE_RECONFIG_REQ || configurationState == STATE_WIFI_NO_FIREBASE) {
      if (ON_AP_FILTER(request)) {
        respondWithConnectToInternet(request);
      } else {
        request->send(200, "text/html", deviceinit_page);
        changeState(STATE_CONFIGURING_FB);
        // If the user signs in successfully, the handle_user_loggedin will be called
      }
    } else if (configurationState == STATE_CONFIGURED) {
      // Already configured - 
      request->send(200, "text/html", "<strong>Weather Station has already been configured - Press the re-configure button if you would like to reset the owner</strong>");
    } else {
      request->send(200, "text/html", "<strong>Complete the configuration of WiFi before configuring Firebase</strong>");
    }
  }

  /**
   * Display the WiFi configuration page
   */
  void handle_config_wifi(AsyncWebServerRequest * request) {
    changeState(STATE_CONFIGURING_WIFI);
    WBP_LOGDEBUG(F("In handle config wifi"));
    String ssidRow;
    String slotRow;
    String buttonHtml;
    String webpage = String(CONFIG_WIFI_PAGE1);
    // add in the Wifi Scan results
    WBP_LOGDEBUG(F("Calling scan networks"));
    int numNetworks = WiFi.scanNetworks();
    WBP_LOGDEBUG1(F("Scanned networks: "),numNetworks);
    int rssis[numNetworks+1];
    int netids[numNetworks+1];
    // Simple insertion sort based on RSSI signal strength
    for (int i = 0; i < numNetworks; i++) {
      WBP_LOGDEBUG1(F("Sorting network: "),i);
      int rssi = WiFi.RSSI(i);
      int j = i;
      if (i > 0) {
        while (j >= 0 && rssis[j] < rssi) {  
            rssis[j + 1] = rssis[j];
            netids[j + 1] = netids[j];
            j = j - 1;  
        }
        j = j + 1; 
      }
      rssis[j] = rssi;
      netids[j] = i;
    }
    WBP_LOGDEBUG(F("Adding network rows"));
    for (int i = 0; i < numNetworks; i++) {
      String ssid = WiFi.SSID(netids[i]);
      if (ssid.length() > 0) {
        ssidRow = String(CONFIG_WIFI_PAGE_SSIDS);
        ssidRow.replace("{{S}}", ssid);
        ssidRow.replace("{{R}}", String(rssis[i]));
        if (WiFi.encryptionType(netids[i]) == WIFI_AUTH_OPEN) {
          ssidRow.replace("{{X}}", "Open");
        } else {
          ssidRow.replace("{{X}}", "Secured");
        }
        webpage += ssidRow;
      }
    }
    webpage += CONFIG_WIFI_PAGE2;
    // Add in the slots
    WBP_LOGDEBUG(F("Adding slots"));
    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
      slotRow = String(CONFIG_WIFI_PAGE_SLOTS);
      slotRow.replace("{{X}}", String(i));
      if (strcmp(weatherBirdConfig.WiFi_Creds[i].wifi_ssid, WM_NO_CONFIG) != 0 && strlen(weatherBirdConfig.WiFi_Creds[i].wifi_ssid) > 0) {
        slotRow.replace("{{S}}", weatherBirdConfig.WiFi_Creds[i].wifi_ssid);
      } else {
        slotRow.replace("{{S}}", "EMPTY");
      }
      webpage += slotRow;
    }
    webpage += String(CONFIG_WIFI_PAGE3);
    // Add the buttons
    WBP_LOGDEBUG(F("Adding buttons"));
    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
      buttonHtml = String(CONFIG_WIFI_PAGE_BUTTONS);
      buttonHtml.replace("{{X}}", String(i));
      webpage += String(buttonHtml);
    }
    webpage += CONFIG_WIFI_PAGE4;
    WBP_LOGDEBUG(F("Sending response"));
    request->send(200, "text/html", webpage);
  }

  String IPAddressToString(IPAddress _address) {
    String str = String(_address[0]);
    str += ".";
    str += String(_address[1]);
    str += ".";
    str += String(_address[2]);
    str += ".";
    str += String(_address[3]);
    return str;
  }


  /**
   * Respond with instructions on connecting to the internet
   */
  void respondWithConnectToInternet(AsyncWebServerRequest * request) {
    String responseStr = "<strong>Please connect to Wifi ";
    if (WiFi.status() == WL_CONNECTED) {
      responseStr += "\"";
      responseStr += WiFi.SSID();
      responseStr += "\" then navigate to <a href=http://";
      responseStr += IPAddressToString(WiFi.localIP());
      responseStr += ">http://";
      responseStr += IPAddressToString(WiFi.localIP());
      responseStr += "</a>";
    } else {
      responseStr += "with internet access";
    }
    responseStr += "</strong>";
    request->send(200, "text/html", responseStr.c_str());
  }

  /**
   * Root of the website request - response will depend on what is configured
   */
  void handle_root(AsyncWebServerRequest * request) {
    WBP_LOGDEBUG(F("In handle root"));
    if (WiFi.status() != WL_CONNECTED || isForcedConfigPortal || configurationState == STATE_CONFIGURING_WIFI || configurationState == STATE_NO_WIFI) {
      handle_config_wifi(request);
    } else if (configurationState == STATE_CONFIGURING_FB || configurationState == STATE_WIFI_NO_FIREBASE || configurationState == STATE_FIREBASE_INIT_ERROR) {
      if (ON_STA_FILTER(request)) {
        handle_config_firebase(request);
      } else {
       respondWithConnectToInternet(request);
      }
    } else if (ON_STA_FILTER(request)) {
      request->redirect("http://weather.sourceauditor.com");
    } else {
      respondWithConnectToInternet(request);
    }
  }

  /**
   * Response to configuration requests from the website
   */
  void handle_config_network_response(AsyncWebServerRequest *request, JsonVariant &json) {
    WBP_LOGINFO(F("Handling wifi config response"));
    const JsonObject& responseJson = json.as<JsonObject>();
    if (responseJson.containsKey("exit") && responseJson["exit"]) {
      WBP_LOGINFO(F("Exit received for wifi config"));
      changeState(STATE_WIFI_NO_FIREBASE);
      request->redirect("/");
      delayEnterStationMode(1);
      return;
    }
    if (!responseJson.containsKey("slot")) {
      WBP_LOGERROR(F("Error - Missing slot in Configure"));
      request->send(422, "text/plain", "Missing require SSID");
      return;
    }
    if (!responseJson.containsKey("ssid")) {
      WBP_LOGERROR(F("Error - Missing SSID in Configure"));
      request->send(422, "text/plain", "Missing require SSID");
      return;
    }
    int slot = responseJson["slot"];
    if (slot < 0 || slot > NUM_WIFI_CREDENTIALS-1) {
      WBP_LOGERROR1(F("Error - Invalid slot number"),slot);
      request->send(422, "text/plain", "Invalid slot number");
      return;
    }
    const char* ssid = responseJson["ssid"];
    
    // Update configuration and file
    if (responseJson.containsKey("password")) {
      if (strlen(responseJson["password"]) < sizeof(weatherBirdConfig.WiFi_Creds[slot].wifi_pw) - 1) {
        strcpy(weatherBirdConfig.WiFi_Creds[slot].wifi_pw, responseJson["password"]);
      } else {
        strncpy(weatherBirdConfig.WiFi_Creds[slot].wifi_pw, responseJson["password"], sizeof(weatherBirdConfig.WiFi_Creds[0].wifi_pw) - 1);
      }
    }

    if (strlen(ssid) < sizeof(weatherBirdConfig.WiFi_Creds[slot].wifi_ssid) - 1) {
      strcpy(weatherBirdConfig.WiFi_Creds[slot].wifi_ssid, ssid);
    } else {
      strncpy(weatherBirdConfig.WiFi_Creds[slot].wifi_ssid, ssid, sizeof(weatherBirdConfig.WiFi_Creds[0].wifi_ssid) - 1);
    }
    saveConfigData();
    WBP_LOGINFO1(F("Adding SSID: "), weatherBirdConfig.WiFi_Creds[slot].wifi_ssid);

    // Respond success
    AsyncWebServerResponse *response = request->beginResponse(204, "text/plain", "Added SSID");
    response->addHeader("X-Connection-Status","Success");
    request->send(response);
    
    if (strcmp(ssid, WM_NO_CONFIG) != 0 && WiFi.status() != WL_CONNECTED) {
      // slot is not set to empty and we're not connected to the internet
      changeState(STATE_CONFIGURING_WIFI);
      
      // Try to connect to the new ssid
      if (responseJson.containsKey("password")) {
        const char* password = responseJson["password"];
        WiFi.begin(ssid, password);
      } else {
        WiFi.begin(ssid);
      }
      int retries = 0;
      while (retries++ < 5) {
        if (WiFi.status() != WL_CONNECTED) {
          delay(500);
        }
      }
      if (WiFi.status() == WL_CONNECTED) {
        if (weatherBirdConfig.firebase_initialized) {
          changeState(STATE_CONFIGURED);
        } else {
          changeState(STATE_WIFI_NO_FIREBASE);
        }
        WBP_LOGINFO1(F("Connected to: "), ssid);
        wifiMulti.addAP(weatherBirdConfig.WiFi_Creds[slot].wifi_ssid, weatherBirdConfig.WiFi_Creds[slot].wifi_pw);
      } else {
        WBP_LOGERROR3(F("Failled to connect to: "), ssid, F(" Password: "), weatherBirdConfig.WiFi_Creds[slot].wifi_pw);
      }
    }
  }

  void handle_json_status_request(AsyncWebServerRequest * request) {
    WBP_LOGINFO(F("Status request received"));
    AsyncJsonResponse * response = new AsyncJsonResponse();
    response->addHeader("Server","ESP Async Web Server");
    const JsonObject& root = response->getRoot();
    root["id"] = "ESP" + String(ESP_getChipId(), HEX);
    root["state"] = configurationState;
    if (WiFi.status() == WL_CONNECTED) {
      root["ssid"] = WiFi.SSID();
      root["localIp"] = IPAddressToString(WiFi.localIP());
      WBP_LOGDEBUG(F("Status connected"));
    } else {
      WBP_LOGDEBUG(F("Status not connected"));
    }
    const JsonObject& jsonConfig = root.createNestedObject("config");
    if (jsonConfig) {
      jsonConfig["firebase_owner_id"] = weatherBirdConfig.firebase_owner_id;
      jsonConfig["firebase_initialized"] = weatherBirdConfig.firebase_initialized;
      const JsonArray& jsonWifiSSIDs = jsonConfig.createNestedArray("WiFi_Creds");
      if (jsonWifiSSIDs) {
        for (int i = 0; i <  NUM_WIFI_CREDENTIALS; i ++) {
          const JsonObject& WifiInfo = jsonWifiSSIDs.createNestedObject();
          if (WifiInfo) {
            WifiInfo["wifi_ssid"] = weatherBirdConfig.WiFi_Creds[i].wifi_ssid;
            WifiInfo["wifi_pw"] = weatherBirdConfig.WiFi_Creds[i].wifi_pw;
          } else {
            root["error"] = "Unable to allocate WiFi object";
          }
        }
      } else {
        root["error"] = "Unable to allocate WiFi Creds Array";
      }
    } else {
      root["error"] = "Unable to allocate config";
    }
    // Add configuration file information
    response->setLength();
    request->send(response);
    WBP_LOGINFO(F("Status response sent"));
  }

  void startServer() {
    server = new AsyncWebServer(HTTP_PORT);
    server->onNotFound([this](AsyncWebServerRequest * request)  { handle_NotFound(request); });
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest * request)  { handle_root(request); });
    server->on("/status.json", HTTP_GET, [this](AsyncWebServerRequest * request)  { handle_json_status_request(request); });
    server->on("/deviceinitializing.html", HTTP_GET, [this](AsyncWebServerRequest * request)  { handle_device_initializing(request); });
    server->on("/userloggedin.html", HTTP_GET, [this](AsyncWebServerRequest * request)  { handle_user_loggedin(request); });
    server->on("/cancel.html", HTTP_GET, [this](AsyncWebServerRequest * request)  { handle_cancel(request); });
    server->on("/test", HTTP_GET, [this](AsyncWebServerRequest * request)  { handle_test(request); });
    AsyncCallbackJsonWebHandler *config_response_handler = new AsyncCallbackJsonWebHandler("/confignetwork", [this](AsyncWebServerRequest *request, JsonVariant &json) { handle_config_network_response(request, json); });
    server->addHandler(config_response_handler);
    server->begin();
  }
  
  int calcChecksum() {
    int checkSum = 0;
    for (uint16_t index = 0; index < (sizeof(weatherBirdConfig) - sizeof(weatherBirdConfig.checkSum)); index++)
    {
      checkSum += * ( ( (byte*) &weatherBirdConfig ) + index);
    }
    return checkSum;
  }

  /**
   * Loads the configuration data from the file
   */
  void loadConfigData() {
    File file = SPIFFS.open(CONFIG_FILENAME, "r");
    WBP_LOGINFO(F("LoadCfgFile "));

    if (!file) {
      WBP_LOGINFO(F("failed"));

      // Trying open redundant config file
      file = SPIFFS.open(CONFIG_FILENAME_BACKUP, "r");
      WBP_LOGINFO(F("LoadBkUpCfgFile "));

      if (!file) {
        WBP_LOGINFO(F("failed"));
        return;
      }
    }

    file.readBytes((char *) &weatherBirdConfig, sizeof(weatherBirdConfig));

    WBP_LOGINFO(F("OK"));
    file.close();
  }

  /**
   * Save the configuration data to the configuration file
   */
  void saveConfigData() {
    File file = SPIFFS.open(CONFIG_FILENAME, "w");
    WBP_LOGINFO(F("SaveCfgFile "));

    int calChecksum = calcChecksum();
    weatherBirdConfig.checkSum = calChecksum;
    WBP_LOGINFO1(F("WCSum=0x"), String(calChecksum, HEX));

    if (file) {
      file.write((uint8_t*) &weatherBirdConfig, sizeof(weatherBirdConfig));
      file.close();
      WBP_LOGINFO(F("OK"));
    } else {
      WBP_LOGINFO(F("failed"));
    }

    // Trying open redundant Auth file
    file = SPIFFS.open(CONFIG_FILENAME_BACKUP, "w");
    WBP_LOGINFO(F("SaveBkUpCfgFile "));

    if (file) {
      file.write((uint8_t *) &weatherBirdConfig, sizeof(weatherBirdConfig));
      file.close();
      WBP_LOGINFO(F("OK"));
    } else {
      WBP_LOGINFO(F("failed"));
    }
  }

  /**
   * Displays the configuration data to the console
   */
  void displayConfigData(WeatherBird_Configuration configData) {
    WBP_LOGERROR3(F(",SSID="), configData.WiFi_Creds[0].wifi_ssid,
                 F(",PW="),   configData.WiFi_Creds[0].wifi_pw);
    WBP_LOGERROR3(F("SSID1="), configData.WiFi_Creds[1].wifi_ssid, F(",PW1="),  configData.WiFi_Creds[1].wifi_pw);
    WBP_LOGERROR1(F("Firebase Owner ID: "), configData.firebase_owner_id);
    WBP_LOGERROR1(F("Firebase initialized: "), configData.firebase_initialized);
    WBP_LOGERROR1(F("Firebase password: "),configData.firebase_password);
  }

  /**
   * Initialize the configuration data in memory
   */
  void initializeConfig() {
    memset(&weatherBirdConfig, 0, sizeof(weatherBirdConfig));
    strcpy(weatherBirdConfig.WiFi_Creds[0].wifi_ssid,   WM_NO_CONFIG);
    strcpy(weatherBirdConfig.WiFi_Creds[0].wifi_pw,     WM_NO_CONFIG);
    strcpy(weatherBirdConfig.WiFi_Creds[1].wifi_ssid,   WM_NO_CONFIG);
    strcpy(weatherBirdConfig.WiFi_Creds[1].wifi_pw,     WM_NO_CONFIG);
    strcpy(weatherBirdConfig.firebase_password,     WM_NO_CONFIG);
    strcpy(weatherBirdConfig.firebase_owner_id,     WM_NO_CONFIG);
    weatherBirdConfig.firebase_initialized = false;
    weatherBirdConfig.checkSum = 0;
  }

  /**
   * Get the configuration data from the files or initialize
   * Returns true if the configuration data was restored, false if initialized
   */
  void getConfigData() {
    int calChecksum;
    if (FORCE_CONFIG_INIT) {
      initializeConfig();
      return;
    }
    if (!SPIFFS.begin(true)) {
      WBP_LOGERROR(F("SPIFFS/LittleFS failed! Formatting."));      
      if (!SPIFFS.begin()) {
        WBP_LOGERROR(F("SPIFFS failed!. Please use LittleFS or EEPROM."));
        initializeConfig();
        return;
      }
    }

    if ( SPIFFS.exists(CONFIG_FILENAME) || SPIFFS.exists(CONFIG_FILENAME_BACKUP) ) {
      // if config file exists, load
      loadConfigData();
      
      WBP_LOGINFO(F("======= Start Stored Config Data ======="));
      displayConfigData(weatherBirdConfig);

      calChecksum = calcChecksum();

      WBP_LOGINFO3(F("CCSum=0x"), String(calChecksum, HEX),
                 F(",RCSum=0x"), String(weatherBirdConfig.checkSum, HEX));
    } else {
      // Initialize empty config data
      initializeConfig();
      return;
    }

    if (calChecksum != weatherBirdConfig.checkSum) {         
      // Including Credentials CSum
      WBP_LOGINFO1(F("InitCfgFile,sz="), sizeof(weatherBirdConfig));
      // doesn't have any configuration        
      initializeConfig();
      saveConfigData();
      return;
    } else {
      displayConfigData(weatherBirdConfig);
    }
  }

  const uint32_t FORCED_CONFIG_PORTAL_FLAG_DATA       = 0xDEADBEEF;
    
#define FORCED_CONFIG_PORTAL_FLAG_DATA_SIZE     4

  /**
   * Saves teh force control panel flag to the file
   */
  void saveForcedCP(uint32_t value) {
    File file = SPIFFS.open(CONFIG_PORTAL_FILENAME, "w");
    
    WBP_LOGINFO(F("SaveCPFile "));

    if (file) {
      file.write((uint8_t*) &value, sizeof(value));
      file.close();
      WBP_LOGINFO(F("OK"));
    } else {
      WBP_LOGINFO(F("failed"));
    }

    // Trying open redundant CP file
    file = SPIFFS.open(CONFIG_PORTAL_FILENAME_BACKUP, "w");
    
    WBP_LOGINFO(F("SaveBkUpCPFile "));

    if (file) {
      file.write((uint8_t *) &value, sizeof(value));
      file.close();
      WBP_LOGINFO(F("OK"));
    } else {
      WBP_LOGINFO(F("failed"));
    }
  }
  
  /**
   * Set configuration panel to force
   */
  void setForcedCP() {
    uint32_t readForcedConfigPortalFlag = FORCED_CONFIG_PORTAL_FLAG_DATA;

    WBP_LOGDEBUG(F("setForcedCP"));

    saveForcedCP(readForcedConfigPortalFlag);
  }
  
  /**
   * Clear the forced configuration portal
   */
  void clearForcedCP() {
    uint32_t readForcedConfigPortalFlag = 0;
    WBP_LOGDEBUG(F("clearForcedCP"));
    saveForcedCP(readForcedConfigPortalFlag);
  }

  /**
   * returns true if the configuration portal should be forced to open
   */
  bool isForcedCP() {
    uint32_t readForcedConfigPortalFlag;
    WBP_LOGDEBUG(F("Check if isForcedCP"));
    File file = SPIFFS.open(CONFIG_PORTAL_FILENAME, "r");
    WBP_LOGINFO(F("LoadCPFile "));
    if (!file) {
      WBP_LOGINFO(F("failed"));

      // Trying open redundant config file
      file = SPIFFS.open(CONFIG_PORTAL_FILENAME_BACKUP, "r");
      WBP_LOGINFO(F("LoadBkUpCPFile "));

      if (!file) {
        WBP_LOGINFO(F("failed"));
        return false;
      }
     }

    file.readBytes((char *) &readForcedConfigPortalFlag, sizeof(readForcedConfigPortalFlag));

    WBP_LOGINFO(F("OK"));
    file.close();
    return (readForcedConfigPortalFlag == FORCED_CONFIG_PORTAL_FLAG_DATA);
  }
};

#endif
