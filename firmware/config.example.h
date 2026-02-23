// config.example.h — Copy to config.h and fill in your values
// config.h is gitignored to protect credentials

#ifndef CONFIG_H
#define CONFIG_H

#define CFG_WIFI_SSID "YOUR_WIFI_SSID"
#define CFG_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define LATITUDE 36.97
#define LONGITUDE -122.01
#define TIMEZONE "America/Los_Angeles"
// POSIX TZ string for configTzTime() — must match TIMEZONE above
// Common US coastal POSIX timezone strings:
//   Pacific:  "PST8PDT,M3.2.0,M11.1.0"
//   Mountain: "MST7MDT,M3.2.0,M11.1.0"
//   Central:  "CST6CDT,M3.2.0,M11.1.0"
//   Eastern:  "EST5EDT,M3.2.0,M11.1.0"
//   Hawaii:   "HST10"
#define TIMEZONE_POSIX "PST8PDT,M3.2.0,M11.1.0"
#define TIDE_STATION "9413450"
#define BUOY_STATION "46236"

#endif
