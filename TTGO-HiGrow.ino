#include <algorithm>
#include <iostream>
#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT12.h>
#include <Adafruit_BME280.h>
#include <WiFiMulti.h>
#include "esp_wifi.h"
#include "secret.h"

#define WBP_DEBUG_OUTPUT      Serial
#define CORE_DEBUG_LEVEL=5
#define _WBP_LOGLEVEL_        4
#define _WBF_LOGLEVEL_        4
#define LED_BUILTIN       16         // GPIO 16 for the TTGO-HiGrow
#define CONFIG_BUTTON     35         // TODO: GPIO 35 is the wake button
#define FIREBASE_FUNCTION_URL "https://us-central1-weather-2a8a7.cloudfunctions.net/initializeStation"
#define FIREBASE_AUTH_DOMAIN "weather-2a8a7.firebaseapp.com"
#define FIREBASE_DATABASE_URL "https://weather-2a8a7.firebaseio.com"
#define FIREBASE_DATABASE_LOCATION "weather-2a8a7.firebaseio.com"
#define FIREBASE_STORAGE_BUCKET "weather-2a8a7.appspot.com"

#include "WeatherBirdProvisioning.h"
#include "WeatherBirdFirebase.h"

WeatherBirdFirebase* firebase;
FirebaseAuth firebaseAuth;
FirebaseConfig firebaseConfig;

WeatherBirdProvisioningManager* provisioningManager;

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
#define WATER_RELAY_PIN     2                   // GPIO2
#define MILLIS_WAIT_WATER   10000                // Millis to wait before changing the water relay

BH1750 lightMeter(0x23); //0x23
Adafruit_BME280 bmp;     //0x77
DHT12 dht12(DHT12_PIN, true);

bool bme_found = false;
/**
 * Station configuration information
 */
int minSoilThreshold = 10;  // Minimum threshold before turning on water
int maxSoilThreshold = 20;  // Max threshold before turning on water
bool waterEnabled = true;
const String minSoilPath = "/soilmin";
const String maxSoilPath = "/soilmax";
const String waterEnabledPath = "/water_enabled";

//void sleepHandler(Button2 &b)
//{
//    Serial.println("Enter Deepsleep ...");
//    esp_sleep_enable_ext1_wakeup(GPIO_SEL_35, ESP_EXT1_WAKEUP_ALL_LOW);
//    delay(1000);
//    esp_deep_sleep_start();
//}
/**
 * Starts the firebase service
 * Returns true if successful
 */
boolean startFirebase() {
  if (provisioningManager->getState() != STATE_CONFIGURED) {
    Serial.println(F("Can not start firebase, Station is not in a provisioned state"));
    return false;
  }
  Serial.println(F("Starting Firebase"));
  firebaseConfig.host = FIREBASE_DATABASE_LOCATION;
  firebaseConfig.api_key = FIREBASE_API_KEY;
  firebaseAuth.user.email = provisioningManager->getStationEmail();
  firebaseAuth.user.password = provisioningManager->getStationPassword();
  return firebase->begin(settingsStreamCallback);
}

/**
 * Callback for when the settings data changes
 */
void settingsStreamCallback(StreamData data) {
  if (data.dataPath() == minSoilPath) {
    if (data.dataType() == "int") {
      minSoilThreshold = data.intData();
      Serial.print(F("Soil min updated to "));
      Serial.println(minSoilThreshold);
    } else {
      Serial.println(F("Invalid data type for min soil path"));
    }
  } else if (data.dataPath() == maxSoilPath) {
    if (data.dataType() == "int") {
      maxSoilThreshold = data.intData();
      Serial.print(F("Soil max updated to "));
      Serial.println(maxSoilThreshold);
    } else {
      Serial.println(F("Invalid data type for max soil path"));
    }
  } else if (data.dataPath() == waterEnabledPath) {
    if (data.dataType() == "boolean") {
      waterEnabled = data.boolData();
      Serial.print(F("Water enabled updated to "));
      Serial.println(waterEnabled);
    } else {
      Serial.println(F("Invalid data type for water enabled path"));
    }
  } else if (data.dataPath() == "/") {
    if (data.dataType() == "json") {
      FirebaseJson &json = data.jsonObject();
      String jsonStr;
      json.toString(jsonStr, true);
      Serial.println(jsonStr);
      FirebaseJsonData jsonData;
      json.get(jsonData, minSoilPath);
      if (jsonData.typeNum == FirebaseJson::JSON_INT) {
        minSoilThreshold = jsonData.intValue;
        Serial.print(F("Soil min updated to "));
        Serial.println(minSoilThreshold);
      } else {
        Serial.println("Invalid json type for minValue");
      }
      json.get(jsonData, maxSoilPath);
      if (jsonData.typeNum == FirebaseJson::JSON_INT) {
        maxSoilThreshold = jsonData.intValue;
        Serial.print(F("Soil max updated to "));
        Serial.println(maxSoilThreshold);
      } else {
        Serial.println("Invalid json type for maxValue");
      }
      json.get(jsonData, waterEnabledPath);
      if (jsonData.typeNum == FirebaseJson::JSON_BOOL) {
        waterEnabled = jsonData.boolValue;
        Serial.print(F("Water enabled updated to "));
        Serial.println(waterEnabled);
      } else {
        Serial.println(F("Invalid data type for water enabled path"));
      }
    } else {
      Serial.println(F("Invalid data type for root soil path"));
    }
  } else {
    Serial.print("Unexpected path for update: ");
    Serial.println(data.dataPath());
  }
}
void setup()
{
    Serial.begin(115200);
    delay(200);
    esp_log_level_set("*", ESP_LOG_DEBUG);
    pinMode(WATER_RELAY_PIN, OUTPUT);
    digitalWrite(WATER_RELAY_PIN, HIGH);
    provisioningManager = new WeatherBirdProvisioningManager(FIREBASE_FUNCTION_URL, LED_BUILTIN, CONFIG_BUTTON);
    provisioningManager->begin();
    firebase = new WeatherBirdFirebase(&firebaseConfig, &firebaseAuth, provisioningManager->getFirebaseStationId());
    if (startFirebase()) {
      Serial.println("Firebase successfully started");
    }
    Wire.begin(I2C_SDA, I2C_SCL);

    dht12.begin();

    //! Sensor power control pin , use deteced must set high
    pinMode(POWER_CTRL, OUTPUT);
    digitalWrite(POWER_CTRL, 1);
    delay(1000);

    if (!bmp.begin()) {
      // Don't remove the bmp code - it messes up the lightMeter code for some reason ?!?
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

bool waterOn = false;
long millsSinceWaterChange = millis();
/**
 * Check to see if the water relay needs to be turned on or off
 */
void checkForWater(uint16_t soil) {
  if (millis() - millsSinceWaterChange > MILLIS_WAIT_WATER) {
    millsSinceWaterChange = millis();
    Serial.print(F("Soil="));
    Serial.print(soil);
    Serial.print(F(", waterOn="));
    Serial.print(waterOn);
    Serial.print(F(", min="));
    Serial.print(minSoilThreshold);
    Serial.print(F(", max="));
    Serial.println(maxSoilThreshold);
    //DEBUGGING
    Serial.print(F("Memory available: "));
    Serial.println(ESP.getFreeHeap());
    if (waterOn && (soil > maxSoilThreshold || !waterEnabled)) {
      Serial.println("Turning water off");
      digitalWrite(WATER_RELAY_PIN, HIGH);
      waterOn = false;
    } else if (!waterOn && soil < minSoilThreshold && waterEnabled) {
      Serial.println("Turning water on");
      digitalWrite(WATER_RELAY_PIN, LOW);
      waterOn = true;
    }
  }
}

#define FIREBASE_UPDATE_DELAY 300000L   // 5 minutes
void loop()
{
    static uint64_t timestamp;
//    button.loop();
//    useButton.loop();
    provisioningManager->loop();
    firebase->loop();
    uint16_t soil = readSoil();
    checkForWater(soil);
    if (millis() - timestamp > FIREBASE_UPDATE_DELAY  &&
        provisioningManager->getState() == STATE_CONFIGURED) {
        timestamp = millis();
        float lux = lightMeter.readLightLevel();
        firebase->updateSensorValue("lux", lux);
        float t12 = dht12.readTemperature();
        float h12 = dht12.readHumidity();
        if (!isnan(t12) && !isnan(h12) ) {
          firebase->updateSensorValue("t", t12);
          firebase->updateSensorValue("hum", h12);
        }
        firebase->updateSensorValue("soil", (float)soil);
        uint32_t salt = readSalt();
        firebase->updateSensorValue("salt", (float)salt);
        float bat = readBattery();
        firebase->updateSensorValue("battery", (float)bat);
    }
}
