/**
 * Library that manages connections to the WeatherBird Firebase Realtime Database
 * Depends on Firebase ESP32 Client https://github.com/mobizt/Firebase-ESP32
 * Depends on ezTime
 * TODO: Add structure to this comment
 */
#ifndef WeatherBirdFirebase_h
#define WeatherBirdFirebase_h
#include "WeatherBirdFirebase_debug.h"
// Time
#include <ezTime.h>

// Firebase
#include <FirebaseESP32.h>

#define FIREBASE_XFER_MAX_DAYS 1
#define FIREBASE_MAX_NUM_SENSORS 50
#define FIREBASE_MAX_RETRY 1
#define FIREBASE_MAX_ERROR_QUEUE 30

class WeatherBirdFirebase {
  public:
  
  WeatherBirdFirebase(FirebaseConfig *config, FirebaseAuth *auth, String stationId) {
    firebaseAuth = auth;
    firebaseConfig = config;
    firebaseStationId = stationId;
  };

  /**
   * Start the Firebase Connection
   * config - Firebase configuration (see FirebaseESP32.h)
   * auth - Authentication information for logging into Firebase.  Username and password for authentication
   *        can be obtained from the FirebaseProvisioning library getStationEmail and getStationPassword
   * returns true if successful, false if there was an error
   */
  bool begin() {
    if (WiFi.status() != WL_CONNECTED) {
      WBF_LOGERROR(F("Can not begin - not connected to WiFi"));
      _running = false;
      return false;
    }

    WBF_LOGDEBUG1(F("Starting firebase - email: "), firebaseAuth->user.email.c_str());
    WBF_LOGDEBUG1(F("email password: "), firebaseAuth->user.password.c_str());
    WBF_LOGDEBUG1(F("host: "), firebaseConfig->host.c_str());
    WBF_LOGDEBUG1(F("Station ID: "), firebaseStationId.c_str());
    
    Firebase.begin(firebaseConfig, firebaseAuth);
    Firebase.reconnectWiFi(true);
    Firebase.setMaxRetry(firebaseData, FIREBASE_MAX_RETRY);
    Firebase.setMaxErrorQueue(firebaseData, FIREBASE_MAX_ERROR_QUEUE);
    firebaseData.setResponseSize(1024); //minimum size is 400 bytes
    struct token_info_t info = Firebase.authTokenInfo();
    int retrycount = 0;
    while (retrycount < 10 && info.status != token_status_ready) {
      delay(3000);
      WBF_LOGDEBUG1(F("Waiting for Firebase Authentication. Status: "), getTokenStatus(info).c_str());
      WBF_LOGERROR1(F("Token error: "), info.error.message.c_str());
      info = Firebase.authTokenInfo();
      retrycount++;
    }
    if (retrycount >= 10) {
      WBF_LOGERROR(F("Unable to authenticate to Firebase"));
      WBF_LOGERROR1(F("Token error: %s\n\n"), info.error.message.c_str());
      _lastError = info.error.message;
      _running = false;
    } else {
      WBF_LOGINFO(F("Successfully authenticated to Firebase"));
      _running = true;
      waitForSync();    // Sync the time to NTP
      WBF_LOGINFO1(F("UTC: "), UTC.dateTime());
      _lastError = "";
    }
    return _running;
  }

  /**
   * Ends the connection to Firebase and frees all resources
   */
  void end() {
    Firebase.end(firebaseData);
    _running = false;
    WBF_LOGINFO(F("Ending Firebase Connection"));
  }

  bool updateSensorValue(String sensorName, float value) {
    String datetime = UTC.dateTime("Y-m-d\\TH:i:s\\Z");
    String last_update_value = "/stations/" + firebaseStationId + "/last_update/" + sensorName + "/value";
    WBF_LOGDEBUG(last_update_value);
    String last_update_datetime = "/stations/" + firebaseStationId + "/last_update/" + sensorName + "/timestamp";
    WBF_LOGDEBUG(last_update_datetime);
    String day_key = datetime.substring(0,10) + "T00:00:00Z";
    String daily_sensor_data_val = "/stations/" + firebaseStationId + "/daily_data/" + day_key + "/sensor_data/" + sensorName + "/" + datetime;
    WBF_LOGDEBUG(daily_sensor_data_val);
    if (!Firebase.setFloat(firebaseData, last_update_value, value) || 
        !Firebase.setString(firebaseData, last_update_datetime, datetime) ||
        !Firebase.setFloat(firebaseData, daily_sensor_data_val, value)) {
      _lastError = firebaseData.errorReason().c_str();
      WBF_LOGERROR3(F("Failure storing value for sensor "),sensorName,F(" Error: "),  firebaseData.errorReason());
    } else {
      WBF_LOGDEBUG3(F("Updated value for sensor "),sensorName,F("; value="),value);
    }
  }

  private:
  bool _running = false;  // begin was successfully called
  FirebaseData firebaseData;
  FirebaseAuth *firebaseAuth;
  FirebaseConfig *firebaseConfig;
  String firebaseStationId;
  std::string _lastError;

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
};

#endif
