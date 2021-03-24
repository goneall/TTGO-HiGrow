/****************************************************************************************************************************
  WeatherBird_Debug.h
  For ESP32 boards
  Debug macros for the WeatherBirdProvisioning

  Based on ESPAsync_WiFiManager_Lite (https://github.com/khoih-prog/ESPAsync_WiFiManager_Lite) is a library 
  Built by Khoi Hoang https://github.com/khoih-prog/ESPAsync_WiFiManager_Lite
  Licensed under MIT license
  
 *****************************************************************************************************************************/

#ifndef WeatherBird_Prov_Debug_h
#define WeatherBird_Prov_Debug_h

#ifdef WBP_DEBUG_OUTPUT
  #define DBG_PORT_WB       WBP_DEBUG_OUTPUT
#else
  #define DBG_PORT_WB       Serial
#endif

// Change _WBP_LOGLEVEL_ to set tracing and logging verbosity
// 0: DISABLED: no logging
// 1: ERROR: errors
// 2: WARN: errors and warnings
// 3: INFO: errors, warnings and informational (default)
// 4: DEBUG: errors, warnings, informational and debug

#ifndef _WBP_LOGLEVEL_
  #define _WBP_LOGLEVEL_       0
#endif

const char WBP_MARK[] = "[WBP] ";

#define WBP_PRINT_MARK   DBG_PORT_WB.print(WBP_MARK)

#define WBP_PRINT        DBG_PORT_WB.print
#define WBP_PRINTLN      DBG_PORT_WB.println


///////////////////////////////////////////

#define WBP_LOGERROR0(x)     if(_WBP_LOGLEVEL_>0) { WBP_PRINT(x); }
#define WBP_LOGERROR(x)      if(_WBP_LOGLEVEL_>0) { WBP_PRINT_MARK; WBP_PRINTLN(x); }
#define WBP_LOGERROR1(x,y)   if(_WBP_LOGLEVEL_>0) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINTLN(y); }
#define WBP_LOGERROR2(x,y,z) if(_WBP_LOGLEVEL_>0) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINTLN(z); }
#define WBP_LOGERROR3(x,y,z,w) if(_WBP_LOGLEVEL_>0) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINT(z); WBP_PRINTLN(w); }
#define WBP_LOGERROR5(x,y,z,w,xx,yy) if(_WBP_LOGLEVEL_>0) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINT(z); WBP_PRINT(w); WBP_PRINT(xx); WBP_PRINTLN(yy); }

///////////////////////////////////////////

#define WBP_LOGWARN0(x)     if(_WBP_LOGLEVEL_>1) { WBP_PRINT(x); }
#define WBP_LOGWARN(x)      if(_WBP_LOGLEVEL_>1) { WBP_PRINT_MARK; WBP_PRINTLN(x); }
#define WBP_LOGWARN1(x,y)   if(_WBP_LOGLEVEL_>1) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINTLN(y); }
#define WBP_LOGWARN2(x,y,z) if(_WBP_LOGLEVEL_>1) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINTLN(z); }
#define WBP_LOGWARN3(x,y,z,w) if(_WBP_LOGLEVEL_>1) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINT(z); WBP_PRINTLN(w); }
#define WBP_LOGWARN5(x,y,z,w,xx,yy) if(_WBP_LOGLEVEL_>1) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINT(z); WBP_PRINT(w); WBP_PRINT(xx); WBP_PRINTLN(yy); }

///////////////////////////////////////////

#define WBP_LOGINFO0(x)     if(_WBP_LOGLEVEL_>2) { WBP_PRINT(x); }
#define WBP_LOGINFO(x)      if(_WBP_LOGLEVEL_>2) { WBP_PRINT_MARK; WBP_PRINTLN(x); }
#define WBP_LOGINFO1(x,y)   if(_WBP_LOGLEVEL_>2) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINTLN(y); }
#define WBP_LOGINFO2(x,y,z) if(_WBP_LOGLEVEL_>3) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINTLN(z); }
#define WBP_LOGINFO3(x,y,z,w) if(_WBP_LOGLEVEL_>3) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINT(z); WBP_PRINTLN(w); }
#define WBP_LOGINFO5(x,y,z,w,xx,yy) if(_WBP_LOGLEVEL_>2) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINT(z); WBP_PRINT(w); WBP_PRINT(xx); WBP_PRINTLN(yy); }

///////////////////////////////////////////

#define WBP_LOGDEBUG0(x)     if(_WBP_LOGLEVEL_>3) { WBP_PRINT(x); }
#define WBP_LOGDEBUG(x)      if(_WBP_LOGLEVEL_>3) { WBP_PRINT_MARK; WBP_PRINTLN(x); }
#define WBP_LOGDEBUG1(x,y)   if(_WBP_LOGLEVEL_>3) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINTLN(y); }
#define WBP_LOGDEBUG2(x,y,z) if(_WBP_LOGLEVEL_>3) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINTLN(z); }
#define WBP_LOGDEBUG3(x,y,z,w) if(_WBP_LOGLEVEL_>3) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINT(z); WBP_PRINTLN(w); }
#define WBP_LOGDEBUG4(x,y,z,w, xx) if(_WBP_LOGLEVEL_>3) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINT(z); WBP_PRINT(w); WBP_PRINTLN(xx); }
#define WBP_LOGDEBUG5(x,y,z,w,xx,yy) if(_WBP_LOGLEVEL_>3) { WBP_PRINT_MARK; WBP_PRINT(x); WBP_PRINT(y); WBP_PRINT(z); WBP_PRINT(w); WBP_PRINT(xx); WBP_PRINTLN(yy); }

///////////////////////////////////////////

#endif    //WeatherBird_Prov_Debug_h
