#define WBP_DEBUG_OUTPUT      Serial

#define _WBP_LOGLEVEL_        4
#define TIMEOUT_RECONNECT_WIFI                    10000L
#define RESET_IF_CONFIG_TIMEOUT                   true
#define CONFIG_TIMEOUT_RETRYTIMES_BEFORE_RESET    5
#define LED_BUILTIN       16         // GPIO 16 for the TTGO-HiGrow
#define CONFIG_BUTTON     35         // TODO: GPIO 35 is the wake button
// Config Timeout 120s (default 60s)
#define CONFIG_TIMEOUT                            120000L
#define FIREBASE_FUNCTION_URL "https://us-central1-weather-2a8a7.cloudfunctions.net/initializeStation"

#include "WeatherBirdProvisioning.h"

void heartBeatPrint()
{
  static int num = 1;

  if (WiFi.status() == WL_CONNECTED)
    Serial.print(F("H"));        // H means connected to WiFi
  else
    Serial.print(F("F"));        // F means not connected to WiFi

  if (num == 80)
  {
    Serial.println();
    num = 1;
  }
  else if (num++ % 10 == 0)
  {
    Serial.print(F(" "));
  }
}

WeatherBirdProvisioningManager* provisioningManager;

void check_status()
{
  static unsigned long checkstatus_timeout = 0;

  //KH
#define HEARTBEAT_INTERVAL    20000L
  // Print hearbeat every HEARTBEAT_INTERVAL (20) seconds.
  if ((millis() > checkstatus_timeout) || (checkstatus_timeout == 0))
  {
    heartBeatPrint();
    checkstatus_timeout = millis() + HEARTBEAT_INTERVAL;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  delay(200);
  provisioningManager = new WeatherBirdProvisioningManager(FIREBASE_FUNCTION_URL, LED_BUILTIN, CONFIG_BUTTON);
  provisioningManager->begin();
}

void loop() {
  provisioningManager->loop();
  check_status();
}