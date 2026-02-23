// ==============================================================================
// ||                 COASTAL WEATHER DASHBOARD - VERSION 1.0                   ||
// ||                                                                          ||
// ||  Weather, tide & surf dashboard on 7-color e-ink display                 ||
// ||                                                                          ||
// ||  DATA SOURCES:                                                           ||
// ||  - Weather: Open-Meteo API (free, no key required)                       ||
// ||  - Tides:   NOAA CO-OPS API (station-based predictions)                  ||
// ||  - Surf:    NOAA NDBC buoy data (real-time wave observations)            ||
// ||                                                                          ||
// ||  SMART SLEEP SCHEDULE:                                                   ||
// ||  Wakes at 6am, 9am, 12pm, 3pm, 6pm, 9pm for fresh data                 ||
// ||  Sleeps overnight until 6am                                              ||
// ||                                                                          ||
// ||  CONFIGURATION:                                                          ||
// ||  All settings in config.h (WiFi, coordinates, station IDs)               ||
// ||  Copy config.example.h to config.h and fill in your values               ||
// ||                                                                          ||
// ||  ARDUINO IDE SETTINGS:                                                   ||
// ||  >> Board: ESP32S3 Dev Module                                            ||
// ||  >> Flash Mode: DIO (NOT OPI!)                                           ||
// ||  >> USB CDC On Boot: "Disabled"                                          ||
// ||  >> USB Mode: "USB-OTG (TinyUSB)"                                        ||
// ||                                                                          ||
// ||  Hardware: Waveshare ESP32-S3-PhotoPainter                               ||
// ||  ESP32 Board Version: 2.0.17                                             ||
// ==============================================================================


// =============================================================================
// SECTION 1: LIBRARY INCLUDES
// =============================================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <SPI.h>
#include <GxEPD2_7C.h>

// Custom fonts
#include <Fonts/fonnts_com_Maison_Neue_Bold9pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Bold12pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Bold15pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Bold18pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Bold24pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Light9pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Light15pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Light18pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Book9pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Book12pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Book15pt7b.h>
#include <Fonts/fonnts_com_Maison_Neue_Book18pt7b.h>

#include <Wire.h>
#include "XPowersLib.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_task_wdt.h"
#include "esp_int_wdt.h"
#include <nvs_flash.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Preferences.h>

// USB Composite Device - Makes board identify as "Coastal Weather" to PCs
// CDC (serial) + HID (prevents aggressive USB power management)
#include "USB.h"
#include "USBHID.h"
#include "USBCDC.h"

// In USB-OTG (TinyUSB) mode with "USB CDC On Boot: Disabled",
// the core does NOT auto-create a USBSerial object.
// We declare our own USBCDC instance here. All serial communication
// in this sketch uses USBSerial so it goes through the USB COM port.
USBCDC USBSerial;

// Weather icon data and user configuration
#include "weather_icons.h"
#include "config.h"


// =============================================================================
// SECTION 2: HARDWARE PIN DEFINITIONS
// =============================================================================

#define PMU_SDA 47
#define PMU_SCL 48
#define PMU_IRQ 21

static const int EPD_BUSY = 13;
static const int EPD_RST  = 12;
static const int EPD_DC   = 8;
static const int EPD_CS   = 9;
static const int EPD_SCK  = 10;
static const int EPD_MOSI = 11;
static const int EPD_MISO = -1;

static const int ACT_LED_PIN = 42;
static const int BOOT_BUTTON_PIN = 0;
static const int USER_BUTTON_PIN = 4;  // KEY button
static const int PWR_BUTTON_PIN = 5;


// =============================================================================
// SECTION 3: CONFIGURATION CONSTANTS
// =============================================================================

static const float LOW_BATTERY_THRESHOLD = 15.0;
static const int W = 800;
static const int H = 480;
static const int WATCHDOG_TIMEOUT_SECONDS = 120;
static const int CRASH_LOOP_THRESHOLD = 3;
static const int FACTORY_RESET_HOLD_MS = 5000;

// Default sleep durations (can be overridden by user settings)
#define SLEEP_DURATION_UNCONFIGURED_US (7 * 24 * 60 * 60 * 1000000ULL)  // 1 week
#define DISPLAY_REFRESH_UNCONFIGURED_MS (5 * 60 * 1000UL)              // 5 minutes

#define uS_TO_S_FACTOR 1000000ULL


// =============================================================================
// SECTION 4: RTC MEMORY VARIABLES (survive deep sleep)
// =============================================================================

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool wasManualWake = false;
RTC_DATA_ATTR time_t lastWeatherFetchEpoch = 0;
RTC_DATA_ATTR time_t lastNtpSyncEpoch = 0;
RTC_DATA_ATTR int rapidBootCount = 0;

// Current conditions cache
RTC_DATA_ATTR float cachedTemp = 0;
RTC_DATA_ATTR int cachedWeatherCode = -1;
RTC_DATA_ATTR float cachedWindSpeed = 0;
RTC_DATA_ATTR int cachedHumidity = 0;
RTC_DATA_ATTR float cachedHighTemp = 0;
RTC_DATA_ATTR float cachedLowTemp = 0;
RTC_DATA_ATTR int cachedRainProb = 0;
RTC_DATA_ATTR float cachedUvIndex = 0;

// 7-day forecast cache
RTC_DATA_ATTR int forecastCode[7] = {0};
RTC_DATA_ATTR float forecastHigh[7] = {0};
RTC_DATA_ATTR float forecastLow[7] = {0};
RTC_DATA_ATTR int forecastDayOfWeek[7] = {0};

// Tide cache (up to 6 events)
RTC_DATA_ATTR int tideCount = 0;
RTC_DATA_ATTR float tideHeight[6] = {0};
RTC_DATA_ATTR char tideType[6] = {0};  // 'H' or 'L'
RTC_DATA_ATTR int tideHour[6] = {0};
RTC_DATA_ATTR int tideMinute[6] = {0};

// Surf cache
RTC_DATA_ATTR float surfWaveHeight = 0;
RTC_DATA_ATTR float surfWavePeriod = 0;


// =============================================================================
// SECTION 5: GLOBAL VARIABLES
// =============================================================================

XPowersAXP2101 PMU;
Preferences preferences;
String serialInputBuffer = "";

GxEPD2_7C<GxEPD2_730c_GDEP073E01, GxEPD2_730c_GDEP073E01::HEIGHT> display(
  GxEPD2_730c_GDEP073E01(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// ===== USB COMPOSITE DEVICE =====
// Minimal HID report descriptor - presents as a "vendor defined" HID device
// This is intentionally a no-op: it never sends reports, it just exists
// so Windows treats the whole USB composite as a HID device and refuses
// to power-manage it aggressively (Windows never suspends keyboards/mice/HID)
static const uint8_t weatherHidReportDescriptor[] = {
  0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
  0x09, 0x01,        // Usage (Vendor Usage 1)
  0xA1, 0x01,        // Collection (Application)
  0x09, 0x01,        //   Usage (Vendor Usage 1)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x75, 0x08,        //   Report Size (8)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x02,        //   Input (Data, Variable, Absolute)
  0xC0               // End Collection
};

// HID device instance
USBHID weatherhid;
bool usbCompositeStarted = false;

// ===== WIFI CREDENTIALS (loaded from config.h) =====
String WIFI_SSID = CFG_WIFI_SSID;
String WIFI_PASS = CFG_WIFI_PASSWORD;

// Runtime state
bool isUsbConnected = false;
bool wasUsbConnected = false;
RTC_DATA_ATTR float batteryPercentage = 0.0;
float batteryVoltage = 0.0;
bool lowBatteryMode = false;
bool isCharging = false;

// Update timestamp (survives deep sleep so cached-data draws show last known time)
RTC_DATA_ATTR char cachedUpdateTime[20] = {0};
String lastUpdateTime = "";
unsigned long lastDisplayRefresh = 0;
uint16_t C_HEADER = GxEPD_RED;
uint16_t C_TEXT_DIM = GxEPD_RED;
bool inSafeMode = false;

// Setup state - determined by checking if WiFi credentials exist
bool isConfigured = false;

// Sleep control flag - set by serial commands to trigger sleep after command completes
bool sleepRequested = false;


// =============================================================================
// SECTION 6: WEATHER ICON MAPPING
// =============================================================================

enum WeatherIcon {
  ICON_CLEAR, ICON_PARTLY_CLOUDY, ICON_CLOUDY, ICON_FOG,
  ICON_DRIZZLE, ICON_RAIN, ICON_SNOW, ICON_THUNDERSTORM
};

WeatherIcon getWeatherIcon(int wmoCode) {
  // WMO Weather interpretation codes
  if (wmoCode == 0) return ICON_CLEAR;
  if (wmoCode == 1 || wmoCode == 2) return ICON_PARTLY_CLOUDY;
  if (wmoCode == 3) return ICON_CLOUDY;
  if (wmoCode >= 45 && wmoCode <= 48) return ICON_FOG;
  if (wmoCode >= 51 && wmoCode <= 55) return ICON_DRIZZLE;
  if (wmoCode >= 56 && wmoCode <= 57) return ICON_DRIZZLE;  // Freezing drizzle
  if (wmoCode >= 61 && wmoCode <= 65) return ICON_RAIN;
  if (wmoCode >= 66 && wmoCode <= 67) return ICON_RAIN;     // Freezing rain
  if (wmoCode >= 71 && wmoCode <= 77) return ICON_SNOW;
  if (wmoCode >= 80 && wmoCode <= 82) return ICON_RAIN;     // Rain showers
  if (wmoCode >= 85 && wmoCode <= 86) return ICON_SNOW;     // Snow showers
  if (wmoCode >= 95 && wmoCode <= 99) return ICON_THUNDERSTORM;
  return ICON_CLEAR;  // Default
}

const char* getWeatherDescription(int wmoCode) {
  switch(wmoCode) {
    case 0: return "Clear Sky";
    case 1: return "Mostly Clear";
    case 2: return "Partly Cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Foggy";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing Drizzle";
    case 61: return "Light Rain";
    case 63: return "Rain";
    case 65: return "Heavy Rain";
    case 66: case 67: return "Freezing Rain";
    case 71: return "Light Snow";
    case 73: return "Snow";
    case 75: return "Heavy Snow";
    case 77: return "Snow Grains";
    case 80: case 81: case 82: return "Rain Showers";
    case 85: case 86: return "Snow Showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm + Hail";
    default: return "Unknown";
  }
}

// Icon pointer arrays - these reference the PROGMEM arrays from weather_icons.h
// The naming matches what png_to_4bpp.py generates
const uint8_t* largeIcons[] = {
  icon_clear_large,
  icon_partly_cloudy_large,
  icon_cloudy_large,
  icon_fog_large,
  icon_drizzle_large,
  icon_rain_large,
  icon_snow_large,
  icon_thunderstorm_large
};

const uint8_t* smallIcons[] = {
  icon_clear_small,
  icon_partly_cloudy_small,
  icon_cloudy_small,
  icon_fog_small,
  icon_drizzle_small,
  icon_rain_small,
  icon_snow_small,
  icon_thunderstorm_small
};


// =============================================================================
// SECTION 7: CONFIGURATION MANAGEMENT
// =============================================================================

void loadConfiguration() {
  USBSerial.println("=== Loading Configuration ===");
  isConfigured = (strlen(CFG_WIFI_SSID) > 0);
  USBSerial.print("  Configured: "); USBSerial.println(isConfigured ? "YES" : "NO");
  USBSerial.print("  WiFi SSID: "); USBSerial.println(WIFI_SSID);
  USBSerial.print("  Location: "); USBSerial.print(LATITUDE); USBSerial.print(", "); USBSerial.println(LONGITUDE);
}


// =============================================================================
// SECTION 8: WATCHDOG & STABILITY FUNCTIONS
// =============================================================================

void initWatchdog() {
  esp_task_wdt_init(WATCHDOG_TIMEOUT_SECONDS, true);
  esp_task_wdt_add(NULL);
  USBSerial.print("[OK] Watchdog enabled: ");
  USBSerial.print(WATCHDOG_TIMEOUT_SECONDS);
  USBSerial.println("s timeout");
}

void feedWatchdog() {
  esp_task_wdt_reset();
}

bool checkCrashLoop() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
    rapidBootCount++;
    if (rapidBootCount >= 5) {
      USBSerial.println("WARNING: CRASH LOOP DETECTED!");
      return true;
    }
  } else {
    rapidBootCount = 0;
  }
  return false;
}

void resetCrashCounter() {
  rapidBootCount = 0;
}

bool checkFactoryReset() {
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    USBSerial.println("BOOT button held - checking for factory reset...");

    for (int i = 0; i < 5; i++) {
      digitalWrite(ACT_LED_PIN, HIGH);
      delay(100);
      digitalWrite(ACT_LED_PIN, LOW);
      delay(100);
      feedWatchdog();
    }

    unsigned long startTime = millis();

    while (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      if (millis() - startTime >= FACTORY_RESET_HOLD_MS) {
        USBSerial.println("FACTORY RESET TRIGGERED!");

        for (int i = 0; i < 10; i++) {
          digitalWrite(ACT_LED_PIN, HIGH);
          delay(50);
          digitalWrite(ACT_LED_PIN, LOW);
          delay(50);
        }

        // Erase all configuration
        preferences.begin("config", false);
        preferences.clear();
        preferences.end();

        nvs_flash_erase();
        nvs_flash_init();

        bootCount = 0;
        rapidBootCount = 0;
        lastWeatherFetchEpoch = 0;

        USBSerial.println("[OK] Factory reset complete - restarting...");
        delay(500);
        ESP.restart();
        return true;
      }

      digitalWrite(ACT_LED_PIN, (millis() / 200) % 2);
      delay(10);
      feedWatchdog();
    }
  }
  return false;
}

void enterSafeMode() {
  USBSerial.println("*** SAFE MODE ***");
  inSafeMode = true;

  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.init(115200, true, 2, false);
  display.setRotation(2);

  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(0, 0, W, 100, GxEPD_RED);
    display.setFont(&fonnts_com_Maison_Neue_Bold24pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(200, 65);
    display.print("SAFE MODE");

    display.setFont(&fonnts_com_Maison_Neue_Light18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(50, 200);
    display.print("Hold BOOT button 5 seconds to reset");
  } while (display.nextPage());

  while (true) {
    feedWatchdog();
    checkFactoryReset();
    delay(100);
    digitalWrite(ACT_LED_PIN, (millis() / 1000) % 2);
  }
}


// =============================================================================
// SECTION 9: PMU FUNCTIONS
// =============================================================================

void pmu_irq_init() {
  pinMode(PMU_IRQ, INPUT_PULLUP);

  // Verify PMU IRQ pin state
  int irqState = digitalRead(PMU_IRQ);
  USBSerial.print("PMU IRQ pin (GPIO 21) state: ");
  USBSerial.println(irqState ? "HIGH" : "LOW");

  // Configure PMU to generate IRQ on USB events
  if (PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL)) {
    PMU.clearIrqStatus();

    // Enable USB insertion/removal interrupts
    PMU.enableIRQ(XPOWERS_AXP2101_VBUS_INSERT_IRQ);
    PMU.enableIRQ(XPOWERS_AXP2101_VBUS_REMOVE_IRQ);

    USBSerial.println("  PMU USB interrupts enabled");
  } else {
    USBSerial.println("  PMU interrupt setup failed");
  }
}

void pmu_configure_awake() {
  feedWatchdog();

  PMU.disableSleep();
  PMU.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_2000MA);
  PMU.disableVbusVoltageMeasure();

  PMU.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
  PMU.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
  PMU.setChargingLedMode(XPOWERS_CHG_LED_OFF);
  PMU.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V1);
  PMU.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
  PMU.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);

  PMU.setDC1Voltage(3300);
  PMU.setALDO1Voltage(3300); PMU.enableALDO1();
  PMU.setALDO2Voltage(3300); PMU.enableALDO2();
  PMU.setALDO3Voltage(3300); PMU.enableALDO3();
  PMU.setALDO4Voltage(3300); PMU.enableALDO4();

  PMU.enableBattVoltageMeasure();
  PMU.enableBattDetection();
  PMU.clearIrqStatus();

  feedWatchdog();
}

void pmu_prepare_for_esp32_sleep() {
  feedWatchdog();

  USBSerial.println("Preparing PMU for deep sleep...");

  // Clear any pending interrupts
  PMU.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  PMU.clearIrqStatus();

  // CRITICAL: Enable USB insertion interrupt to wake from sleep
  PMU.enableIRQ(XPOWERS_AXP2101_VBUS_INSERT_IRQ);  // Wake on USB plug-in
  PMU.enableIRQ(XPOWERS_AXP2101_VBUS_REMOVE_IRQ);  // Detect USB removal

  // Disable non-essential power rails during sleep
  // E-ink retains image electrostatically — no power needed
  // pmu_configure_awake() re-enables these on next boot
  PMU.disableSleep();
  PMU.disableALDO3();  // Display doesn't need power to hold image
  PMU.disableALDO4();  // Peripherals not needed during sleep

  USBSerial.println("[OK] PMU ready - will wake on USB connection");
}

void printBatteryStatus() {
  feedWatchdog();

  // Read current PMU values (PMU already initialized at boot)
  isUsbConnected = PMU.isVbusIn();
  batteryVoltage = PMU.getBattVoltage() / 1000.0;

  // Check charging status - isCharging returns true when actively charging
  // When battery is full and USB connected, isCharging will be false
  isCharging = PMU.isCharging();

  // Battery percentage based on voltage
  // Charge target is 4.1V, so 4.05V+ = 100% when on USB
  // On battery: 4.2V = 100%, 3.3V = 0%
  if (isUsbConnected) {
    // When USB connected, battery reads slightly lower due to load
    if (batteryVoltage >= 4.05) batteryPercentage = 100;
    else if (batteryVoltage >= 3.95) batteryPercentage = 90;
    else if (batteryVoltage >= 3.85) batteryPercentage = 75;
    else if (batteryVoltage >= 3.75) batteryPercentage = 55;
    else if (batteryVoltage >= 3.65) batteryPercentage = 35;
    else if (batteryVoltage >= 3.55) batteryPercentage = 20;
    else if (batteryVoltage >= 3.45) batteryPercentage = 10;
    else batteryPercentage = 5;

    // CRITICAL FIX: If battery shows <20% on USB, PMU reading is unreliable
    // This happens on some PCs/users - IGNORE IT
    if (batteryPercentage < 20) {
      USBSerial.print(" [PMU UNRELIABLE - ignoring false low reading] ");
      batteryPercentage = 50;  // Force reasonable charge level
    }
  } else {
    // On battery only
    if (batteryVoltage >= 4.10) batteryPercentage = 100;
    else if (batteryVoltage >= 4.00) batteryPercentage = 90;
    else if (batteryVoltage >= 3.90) batteryPercentage = 75;
    else if (batteryVoltage >= 3.80) batteryPercentage = 55;
    else if (batteryVoltage >= 3.70) batteryPercentage = 40;
    else if (batteryVoltage >= 3.60) batteryPercentage = 25;
    else if (batteryVoltage >= 3.50) batteryPercentage = 15;
    else batteryPercentage = 5;
  }

  // CRITICAL FIX: NEVER enable low battery mode when USB connected
  // USB = charging = always use RED colors (normal mode)
  if (isUsbConnected || USBSerial) {
    lowBatteryMode = false;  // Force normal mode when any USB connection exists
  } else {
    lowBatteryMode = (batteryPercentage < LOW_BATTERY_THRESHOLD);
  }

  // Print status
  USBSerial.print("Battery: ");
  USBSerial.print(batteryPercentage);
  USBSerial.print("% (");
  USBSerial.print(batteryVoltage, 2);
  USBSerial.print("V)");

  if (isUsbConnected) {
    if (isCharging) {
      USBSerial.print(" [CHARGING]");
    } else if (batteryPercentage >= 95) {
      USBSerial.print(" [CHARGED]");
    } else {
      USBSerial.print(" [USB]");
    }
  }
  USBSerial.println();
}


// =============================================================================
// SECTION 10: USB COMPOSITE DEVICE INITIALIZATION
// =============================================================================

// Custom HID device class - just provides the descriptor, never sends data
class WeatherDummyHID : public USBHIDDevice {
public:
  WeatherDummyHID() {}

  uint16_t _onGetDescriptor(uint8_t* buffer) {
    memcpy(buffer, weatherHidReportDescriptor, sizeof(weatherHidReportDescriptor));
    return sizeof(weatherHidReportDescriptor);
  }

  // We never send reports - this device just exists for USB identity
  void _onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) {
    // Intentionally empty - we don't process HID output
  }
};

WeatherDummyHID weatherDummyDevice;

void initUSBComposite() {
  // Set custom USB device identity - this is what PCs see in Device Manager
  USB.productName("Coastal Weather");
  USB.manufacturerName("Coastal Weather");

  // Use Espressif's VID (0x303A) - safe for hobbyist/small projects
  // Custom PID to distinguish from generic ESP32 devices
  USB.VID(0x303A);
  USB.PID(0x8002);  // Custom PID for weather dashboard

  // Initialize the dummy HID device - this makes it a composite device
  // Windows will see: CDC (serial port) + HID (vendor device)
  // The HID interface prevents Windows from power-managing the device
  weatherhid.addDevice(&weatherDummyDevice, sizeof(weatherHidReportDescriptor));
  weatherhid.begin();

  // Initialize CDC serial BEFORE starting the USB stack
  // The PC will try to read descriptors immediately on USB.begin(),
  // so all interfaces (CDC + HID) must be ready first.
  // If CDC isn't initialized, the descriptor response is malformed
  // and Windows reports "Device Descriptor Request Failed".
  USBSerial.begin();
  USBSerial.setTxTimeoutMs(0);  // Don't block if no PC is listening (battery mode)

  // Start the USB stack - this makes the device visible to the PC
  // All interfaces are now registered and ready for enumeration
  USB.begin();

  usbCompositeStarted = true;
}


// =============================================================================
// SECTION 11: DISPLAY LOW-LEVEL FUNCTIONS
// =============================================================================

void epd_wait_busy() {
  unsigned long startTime = millis();
  unsigned long lastYield = millis();

  while (digitalRead(EPD_BUSY) == LOW) {
    delay(10);
    if (millis() - lastYield >= 100) {
      feedWatchdog();
      yield();
      lastYield = millis();
    }
    if (millis() - startTime > 60000) {
      USBSerial.println("Display timeout!");
      return;
    }
  }
  yield();
  feedWatchdog();
}

void epd_hardware_reset() {
  digitalWrite(EPD_RST, HIGH);
  delay(50);
  digitalWrite(EPD_RST, LOW);
  delay(20);
  digitalWrite(EPD_RST, HIGH);
  delay(50);
  epd_wait_busy();
}

void epd_deep_init() {
  feedWatchdog();
  epd_hardware_reset();
  delay(80);
  epd_wait_busy();
}

void blinkLED(int times) {
  for(int i = 0; i < times; i++) {
    digitalWrite(ACT_LED_PIN, HIGH);
    delay(100);
    digitalWrite(ACT_LED_PIN, LOW);
    delay(100);
  }
}


// =============================================================================
// SECTION 12: DRAW BITMAP 4BPP FUNCTION
// =============================================================================

void drawBitmap4bpp(int x, int y, const uint8_t* logoData, uint16_t w, uint16_t h) {
  // Draw a 4bpp image from PROGMEM
  // Colors: 0=BLACK, 1=WHITE, 4=RED

  for (uint16_t row = 0; row < h; row++) {
    for (uint16_t col = 0; col < w; col += 2) {
      uint16_t byteIndex = (row * w + col) / 2;
      uint8_t packedByte = pgm_read_byte(&logoData[byteIndex]);

      uint8_t pixel1 = (packedByte >> 4) & 0x0F;
      uint8_t pixel2 = packedByte & 0x0F;

      uint16_t color1, color2;

      // Map to e-ink colors
      switch(pixel1) {
        case 0: color1 = GxEPD_BLACK; break;
        case 4: color1 = GxEPD_RED; break;
        default: color1 = GxEPD_WHITE; break;
      }
      switch(pixel2) {
        case 0: color2 = GxEPD_BLACK; break;
        case 4: color2 = GxEPD_RED; break;
        default: color2 = GxEPD_WHITE; break;
      }

      // Only draw non-white pixels for transparency effect
      if (color1 != GxEPD_WHITE) {
        display.drawPixel(x + col, y + row, color1);
      }
      if (col + 1 < w && color2 != GxEPD_WHITE) {
        display.drawPixel(x + col + 1, y + row, color2);
      }
    }
  }
}


// =============================================================================
// SECTION 13: SETUP SCREEN (shown when not configured)
// =============================================================================

void drawSetupScreen() {
  USBSerial.println("=== Drawing Setup Screen ===");
  feedWatchdog();

  // Power stabilization before display refresh
  USBSerial.println("Stabilizing power...");
  delay(2000);
  feedWatchdog();

  // Re-read PMU to ensure stable readings
  PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL);
  delay(500);
  feedWatchdog();

  esp_task_wdt_delete(NULL);

  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Header bar - "SC WEATHER SETUP"
    display.fillRect(0, 0, W, 80, GxEPD_RED);
    display.setFont(&fonnts_com_Maison_Neue_Bold24pt7b);
    display.setTextColor(GxEPD_WHITE);

    int16_t tx, ty;
    uint16_t tw, th;
    String headerText = "SC WEATHER SETUP";
    display.getTextBounds(headerText, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((W - tw) / 2, 55);
    display.print(headerText);

    // Instructions
    int textStartY = 220;
    int lineSpacing = 45;

    // Line 1 - BOLD
    display.setFont(&fonnts_com_Maison_Neue_Bold18pt7b);
    display.setTextColor(GxEPD_BLACK);

    String line1 = "Configure WiFi in config.h";
    display.getTextBounds(line1, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((W - tw) / 2, textStartY);
    display.print(line1);

    // Line 2 - not bold
    display.setFont(&fonnts_com_Maison_Neue_Light15pt7b);

    String line2 = "Then upload firmware";
    display.getTextBounds(line2, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((W - tw) / 2, textStartY + lineSpacing);
    display.print(line2);

    // Line 3 - not bold
    String line3 = "See config.example.h for reference";
    display.getTextBounds(line3, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor((W - tw) / 2, textStartY + lineSpacing * 2);
    display.print(line3);

  } while (display.nextPage());

  // Wait for display to finish
  epd_wait_busy();

  // Post-refresh stabilization - reinit PMU
  delay(500);
  PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL);
  pmu_configure_awake();
  delay(500);

  esp_task_wdt_add(NULL);
  feedWatchdog();

  USBSerial.println("Setup screen complete!");
}


// =============================================================================
// SECTION 14: WIFI & TIME FUNCTIONS
// =============================================================================

void connectWiFi() {
  if (WIFI_SSID.length() == 0) {
    USBSerial.println("No WiFi credentials - skipping connection");
    return;
  }

  // WiFi requires 80MHz+, ensure full speed
  setCpuFrequencyMhz(240);

  USBSerial.print("Connecting to WiFi: ");
  USBSerial.println(WIFI_SSID);

  // Brief power stabilization (AXP2101 rails settle in <100ms)
  delay(500);
  feedWatchdog();

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    handleSerialCommands();
    delay(500);
    USBSerial.print(".");
    attempts++;
    feedWatchdog();
  }

  if (WiFi.status() == WL_CONNECTED) {
    USBSerial.println(" Connected!");
    USBSerial.print("IP: ");
    USBSerial.println(WiFi.localIP());
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
  } else {
    USBSerial.println(" FAILED!");
  }
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Drop CPU to 80MHz for display drawing — ~1/3 power draw
  setCpuFrequencyMhz(80);
  USBSerial.println("[PWR] CPU scaled to 80MHz for display phase");
}

void initTime() {
  // Skip NTP sync if we synced recently (saves WiFi time)
  time_t now;
  time(&now);
  if (lastNtpSyncEpoch > 0 && (now - lastNtpSyncEpoch) < 43200) {
    USBSerial.println("NTP sync skipped (synced within 12h)");
    return;
  }

  USBSerial.println("Syncing time with NTP...");
  configTzTime(TIMEZONE_POSIX, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    delay(500);
    attempts++;
    feedWatchdog();
  }

  if (getLocalTime(&timeinfo)) {
    USBSerial.print("Time synced: ");
    USBSerial.println(&timeinfo, "%Y-%m-%d %H:%M:%S %Z");
    time(&lastNtpSyncEpoch);
  }
}


// =============================================================================
// SECTION 15: WEATHER DATA FETCHING (Open-Meteo API)
// =============================================================================

bool fetchWeatherData() {
  USBSerial.println("=== Fetching Weather Data ===");
  feedWatchdog();

  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude=" + String(LATITUDE, 6);
  url += "&longitude=" + String(LONGITUDE, 6);
  url += "&current=temperature_2m,weather_code,wind_speed_10m,relative_humidity_2m";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,uv_index_max,precipitation_probability_max";
  url += "&temperature_unit=fahrenheit";
  url += "&wind_speed_unit=mph";
  url += "&timezone=" + String(TIMEZONE);
  url += "&forecast_days=7";

  http.begin(url);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    USBSerial.print("[FAIL] Weather API: HTTP ");
    USBSerial.println(httpCode);
    http.end();
    return false;
  }

  // Use filter to reduce memory usage
  JsonDocument filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["wind_speed_10m"] = true;
  filter["current"]["relative_humidity_2m"] = true;
  filter["daily"]["weather_code"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;
  filter["daily"]["uv_index_max"] = true;
  filter["daily"]["precipitation_probability_max"] = true;
  filter["daily"]["time"] = true;

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

  if (error) {
    USBSerial.print("[FAIL] JSON parse: ");
    USBSerial.println(error.c_str());
    return false;
  }

  // Parse current conditions
  cachedTemp = doc["current"]["temperature_2m"] | 0.0f;
  cachedWeatherCode = doc["current"]["weather_code"] | 0;
  cachedWindSpeed = doc["current"]["wind_speed_10m"] | 0.0f;
  cachedHumidity = doc["current"]["relative_humidity_2m"] | 0;

  // Parse daily forecast
  JsonArray dailyCodes = doc["daily"]["weather_code"];
  JsonArray dailyMax = doc["daily"]["temperature_2m_max"];
  JsonArray dailyMin = doc["daily"]["temperature_2m_min"];
  JsonArray dailyUv = doc["daily"]["uv_index_max"];
  JsonArray dailyRain = doc["daily"]["precipitation_probability_max"];
  JsonArray dailyTime = doc["daily"]["time"];

  // Today's high/low and UV/rain
  cachedHighTemp = dailyMax[0] | 0.0f;
  cachedLowTemp = dailyMin[0] | 0.0f;
  cachedUvIndex = dailyUv[0] | 0.0f;
  cachedRainProb = dailyRain[0] | 0;

  // 7-day forecast
  for (int i = 0; i < 7 && i < (int)dailyCodes.size(); i++) {
    forecastCode[i] = dailyCodes[i] | 0;
    forecastHigh[i] = dailyMax[i] | 0.0f;
    forecastLow[i] = dailyMin[i] | 0.0f;

    // Parse day of week from date string "2026-02-21"
    String dateStr = dailyTime[i].as<String>();
    if (dateStr.length() >= 10) {
      struct tm t = {0};
      t.tm_year = dateStr.substring(0, 4).toInt() - 1900;
      t.tm_mon = dateStr.substring(5, 7).toInt() - 1;
      t.tm_mday = dateStr.substring(8, 10).toInt();
      mktime(&t);
      forecastDayOfWeek[i] = t.tm_wday;  // 0=Sun, 1=Mon, etc.
    }
  }

  USBSerial.println("[OK] Weather data parsed");
  USBSerial.print("  Temp: "); USBSerial.print(cachedTemp); USBSerial.println("F");
  USBSerial.print("  Code: "); USBSerial.println(cachedWeatherCode);
  USBSerial.print("  Wind: "); USBSerial.print(cachedWindSpeed); USBSerial.println(" mph");
  USBSerial.print("  H/L: "); USBSerial.print(cachedHighTemp); USBSerial.print("/"); USBSerial.println(cachedLowTemp);

  return true;
}


// =============================================================================
// SECTION 16: TIDE DATA FETCHING (NOAA CO-OPS API)
// =============================================================================

bool fetchTideData() {
  USBSerial.println("=== Fetching Tide Data ===");
  feedWatchdog();

  // Get today's date for the API request
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    USBSerial.println("[FAIL] Cannot get time for tide request");
    return false;
  }

  char dateStr[9];
  strftime(dateStr, sizeof(dateStr), "%Y%m%d", &timeinfo);

  // Calculate tomorrow's date
  time_t tomorrow = mktime(&timeinfo) + 86400;
  struct tm tmTomorrow;
  localtime_r(&tomorrow, &tmTomorrow);
  char endDateStr[9];
  strftime(endDateStr, sizeof(endDateStr), "%Y%m%d", &tmTomorrow);

  HTTPClient http;
  String url = "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter";
  url += "?begin_date=" + String(dateStr);
  url += "&end_date=" + String(endDateStr);
  url += "&station=" + String(TIDE_STATION);
  url += "&product=predictions&datum=MLLW&time_zone=lst_ldt";
  url += "&interval=hilo&units=english&format=json";

  http.begin(url);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    USBSerial.print("[FAIL] Tide API: HTTP ");
    USBSerial.println(httpCode);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();

  if (error) {
    USBSerial.print("[FAIL] Tide JSON: ");
    USBSerial.println(error.c_str());
    return false;
  }

  JsonArray predictions = doc["predictions"];
  tideCount = 0;

  for (JsonObject pred : predictions) {
    if (tideCount >= 6) break;

    String t = pred["t"].as<String>();     // "2026-02-21 05:42"
    String type = pred["type"].as<String>(); // "H" or "L"
    float v = pred["v"].as<float>();

    tideType[tideCount] = type[0];
    tideHeight[tideCount] = v;

    // Parse time "YYYY-MM-DD HH:MM"
    if (t.length() >= 16) {
      tideHour[tideCount] = t.substring(11, 13).toInt();
      tideMinute[tideCount] = t.substring(14, 16).toInt();
    }

    tideCount++;
  }

  USBSerial.print("[OK] Tide data: ");
  USBSerial.print(tideCount);
  USBSerial.println(" events");

  return true;
}


// =============================================================================
// SECTION 17: SURF DATA FETCHING (NOAA NDBC buoy - plain text, not JSON!)
// =============================================================================

bool fetchSurfData() {
  USBSerial.println("=== Fetching Surf Data ===");
  feedWatchdog();

  HTTPClient http;
  String url = "https://www.ndbc.noaa.gov/data/realtime2/" + String(BUOY_STATION) + ".txt";

  http.begin(url);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    USBSerial.print("[FAIL] Buoy API: HTTP ");
    USBSerial.print(httpCode);
    USBSerial.print(" - ");
    USBSerial.println(url);
    http.end();
    return false;
  }

  // NDBC files are ~200KB — far too large for ESP32 RAM.
  // Read only the first 3 lines from the stream (header, units, latest data).
  WiFiClient* stream = http.getStreamPtr();
  String lines[3];
  int lineIdx = 0;

  while (lineIdx < 3 && stream->connected()) {
    if (stream->available()) {
      lines[lineIdx] = stream->readStringUntil('\n');
      lines[lineIdx].trim();
      lineIdx++;
    }
    feedWatchdog();
  }
  http.end();

  if (lineIdx < 3 || lines[2].length() == 0) {
    USBSerial.println("[FAIL] Buoy data format error");
    return false;
  }

  String dataLine = lines[2];

  // Parse space-separated values - WVHT is field index 8, DPD is field index 9
  int fieldIndex = 0;
  int startPos = 0;
  String wvhtStr = "", dpdStr = "";

  for (int i = 0; i <= (int)dataLine.length(); i++) {
    if (i == (int)dataLine.length() || dataLine[i] == ' ') {
      if (i > startPos) {
        String field = dataLine.substring(startPos, i);
        if (fieldIndex == 8) wvhtStr = field;
        if (fieldIndex == 9) dpdStr = field;
        fieldIndex++;
      }
      startPos = i + 1;
    }
  }

  // WVHT is in meters, convert to feet. Handle missing data markers (99.00, MM)
  if (wvhtStr.length() > 0 && wvhtStr != "MM" && wvhtStr.toFloat() < 90.0) {
    surfWaveHeight = wvhtStr.toFloat() * 3.28084;  // meters to feet
  } else {
    surfWaveHeight = 0;
  }

  if (dpdStr.length() > 0 && dpdStr != "MM" && dpdStr.toFloat() < 90.0) {
    surfWavePeriod = dpdStr.toFloat();
  } else {
    surfWavePeriod = 0;
  }

  USBSerial.print("[OK] Surf: ");
  USBSerial.print(surfWaveHeight, 1);
  USBSerial.print("ft @ ");
  USBSerial.print(surfWavePeriod, 0);
  USBSerial.println("s");

  return true;
}


// =============================================================================
// SECTION 18: SMART SLEEP SCHEDULER
// =============================================================================

int calculateNextWakeMinutes() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return 180;  // Default 3 hours

  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  int currentTotalMinutes = currentHour * 60 + currentMinute;

  // Schedule: 6:00, 9:00, 12:00, 15:00, 18:00, 21:00
  int scheduleMinutes[] = {360, 540, 720, 900, 1080, 1260};
  int numSlots = 6;

  for (int i = 0; i < numSlots; i++) {
    if (scheduleMinutes[i] > currentTotalMinutes) {
      int minutesUntil = scheduleMinutes[i] - currentTotalMinutes;
      USBSerial.print("Next wake at ");
      USBSerial.print(scheduleMinutes[i] / 60);
      USBSerial.print(":00 (");
      USBSerial.print(minutesUntil);
      USBSerial.println(" min)");
      return max(10, minutesUntil);  // Minimum 10 minutes
    }
  }

  // Past 21:00 - sleep until 6:00 AM next day
  int minutesUntilMorning = (24 * 60 - currentTotalMinutes) + 360;
  USBSerial.print("After 9PM - sleeping until 6AM (");
  USBSerial.print(minutesUntilMorning);
  USBSerial.println(" min)");
  return minutesUntilMorning;
}


// =============================================================================
// SECTION 19: BATTERY INDICATOR & WEATHER DASHBOARD RENDERING
// =============================================================================

void drawBatteryIndicator(int rightEdge, int y) {
  // Calculate percentage text width for right-alignment
  display.setFont(&fonnts_com_Maison_Neue_Light9pt7b);
  char pctStr[6];
  snprintf(pctStr, sizeof(pctStr), "%d%%", (int)batteryPercentage);
  int16_t tx, ty;
  uint16_t tw, th;
  display.getTextBounds(pctStr, 0, 0, &tx, &ty, &tw, &th);

  int pctWidth = tw;
  int chargingWidth = isCharging ? 15 : 0;
  // Layout: [battery 36px][nub 3px][gap 5px][pct text][gap 4px][bolt]
  int totalWidth = 36 + 3 + 5 + pctWidth + chargingWidth;
  int x = rightEdge - totalWidth;

  // Battery body outline (36x16)
  display.drawRect(x, y, 36, 16, GxEPD_WHITE);

  // Battery nub on right edge (3x8, centered vertically)
  display.fillRect(x + 36, y + 4, 3, 8, GxEPD_WHITE);

  // Fill level proportional to batteryPercentage (0-100% -> 0-32px)
  int fillWidth = (int)(batteryPercentage / 100.0 * 32);
  if (fillWidth > 32) fillWidth = 32;
  if (fillWidth > 0) {
    display.fillRect(x + 2, y + 2, fillWidth, 12, GxEPD_WHITE);
  }

  // Percentage text to the right of the icon
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(x + 44, y + 13);
  display.print(pctStr);

  // Charging lightning bolt
  if (isCharging) {
    int bx = x + 44 + pctWidth + 4;
    int by = y + 1;
    // Two triangles forming a zigzag bolt shape (~7x13px)
    display.fillTriangle(bx + 4, by, bx, by + 6, bx + 5, by + 6, GxEPD_WHITE);
    display.fillTriangle(bx + 1, by + 7, bx + 6, by + 7, bx + 2, by + 13, GxEPD_WHITE);
  }
}

// =============================================================================
// TIDE CURVE & SURF BOX DRAWING FUNCTIONS
// =============================================================================

void drawTideCurve() {
  // Layout constants
  const int PLOT_LEFT = 60;
  const int PLOT_RIGHT = 580;
  const int CURVE_Y_TOP = 286;
  const int CURVE_Y_BOTTOM = 344;
  const int CURVE_HEIGHT = CURVE_Y_BOTTOM - CURVE_Y_TOP;

  // "TIDES" label
  display.setFont(&fonnts_com_Maison_Neue_Bold9pt7b);
  display.setTextColor(C_TEXT_DIM);
  display.setCursor(20, 280);
  display.print("TIDES");

  // Guard: need at least 2 tides for a curve
  if (tideCount < 2) {
    display.setFont(&fonnts_com_Maison_Neue_Book12pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 320);
    display.print("No tide data");
    return;
  }

  // Convert tide times to minutes-since-midnight, fix midnight crossings
  int tideMinutes[6];
  for (int i = 0; i < tideCount; i++) {
    tideMinutes[i] = tideHour[i] * 60 + tideMinute[i];
    if (i > 0 && tideMinutes[i] < tideMinutes[i - 1]) {
      tideMinutes[i] += 1440;  // Next day
    }
  }

  // Find height range with 10% padding
  float minH = tideHeight[0], maxH = tideHeight[0];
  for (int i = 1; i < tideCount; i++) {
    if (tideHeight[i] < minH) minH = tideHeight[i];
    if (tideHeight[i] > maxH) maxH = tideHeight[i];
  }
  float range = maxH - minH;
  if (range < 0.1) range = 1.0;  // Prevent division by zero
  float padding = range * 0.10;
  float padMin = minH - padding;
  float padRange = range + 2 * padding;

  // Map time to X pixel
  int minMin = tideMinutes[0];
  int maxMin = tideMinutes[tideCount - 1];
  int timeSpan = maxMin - minMin;
  if (timeSpan < 1) timeSpan = 1;

  // Draw curve segment by segment (cosine interpolation)
  for (int seg = 0; seg < tideCount - 1; seg++) {
    int x0 = PLOT_LEFT + (int)((long)(tideMinutes[seg] - minMin) * (PLOT_RIGHT - PLOT_LEFT) / timeSpan);
    int x1 = PLOT_LEFT + (int)((long)(tideMinutes[seg + 1] - minMin) * (PLOT_RIGHT - PLOT_LEFT) / timeSpan);
    float h0 = tideHeight[seg];
    float h1 = tideHeight[seg + 1];

    int prevY = -1;
    for (int px = x0; px <= x1; px++) {
      float t = (x1 == x0) ? 0.5 : (float)(px - x0) / (float)(x1 - x0);
      float cosT = (1.0 - cos(t * PI)) / 2.0;
      float h = h0 + (h1 - h0) * cosT;
      int y = CURVE_Y_BOTTOM - (int)((h - padMin) / padRange * CURVE_HEIGHT);

      // Draw 3px thick line
      display.drawPixel(px, y - 1, GxEPD_BLACK);
      display.drawPixel(px, y, GxEPD_BLACK);
      display.drawPixel(px, y + 1, GxEPD_BLACK);

      // Fill gaps for steep sections
      if (prevY >= 0 && abs(y - prevY) > 2) {
        int yStart = min(y, prevY);
        int yEnd = max(y, prevY);
        for (int fy = yStart; fy <= yEnd; fy++) {
          display.drawPixel(px, fy, GxEPD_BLACK);
        }
      }
      prevY = y;
    }
    feedWatchdog();
  }

  // Draw dots and labels at each tide point
  int16_t tx, ty;
  uint16_t tw, th;

  for (int i = 0; i < tideCount; i++) {
    int dotX = PLOT_LEFT + (int)((long)(tideMinutes[i] - minMin) * (PLOT_RIGHT - PLOT_LEFT) / timeSpan);
    int dotY = CURVE_Y_BOTTOM - (int)((tideHeight[i] - padMin) / padRange * CURVE_HEIGHT);

    // Dot: RED for high, BLACK for low
    uint16_t dotColor = (tideType[i] == 'H') ? GxEPD_RED : GxEPD_BLACK;
    display.fillCircle(dotX, dotY, 4, dotColor);

    // Format height string
    char htStr[10];
    snprintf(htStr, sizeof(htStr), "%.1fft", tideHeight[i]);

    // Format time string
    char tmStr[10];
    int dispHour = tideHour[i];
    const char* ampm = (dispHour >= 12) ? "p" : "a";
    if (dispHour > 12) dispHour -= 12;
    if (dispHour == 0) dispHour = 12;
    snprintf(tmStr, sizeof(tmStr), "%d:%02d%s", dispHour, tideMinute[i], ampm);

    display.setFont(&fonnts_com_Maison_Neue_Book9pt7b);

    if (tideType[i] == 'H') {
      // High tide: labels ABOVE the dot
      display.setTextColor(GxEPD_RED);
      display.getTextBounds(htStr, 0, 0, &tx, &ty, &tw, &th);
      int labelX = dotX - tw / 2;
      if (labelX < 75) labelX = 75;
      if (labelX + (int)tw > 625) labelX = 625 - tw;
      display.setCursor(labelX, max(dotY - 24, 278));
      display.print(htStr);

      display.setTextColor(GxEPD_BLACK);
      display.getTextBounds(tmStr, 0, 0, &tx, &ty, &tw, &th);
      labelX = dotX - tw / 2;
      if (labelX < 75) labelX = 75;
      if (labelX + (int)tw > 625) labelX = 625 - tw;
      display.setCursor(labelX, max(dotY - 8, 294));
      display.print(tmStr);
    } else {
      // Low tide: labels BELOW the dot
      display.setTextColor(GxEPD_BLACK);
      display.getTextBounds(htStr, 0, 0, &tx, &ty, &tw, &th);
      int labelX = dotX - tw / 2;
      if (labelX < 75) labelX = 75;
      if (labelX + (int)tw > 625) labelX = 625 - tw;
      display.setCursor(labelX, min(dotY + 20, 358));
      display.print(htStr);

      display.getTextBounds(tmStr, 0, 0, &tx, &ty, &tw, &th);
      labelX = dotX - tw / 2;
      if (labelX < 75) labelX = 75;
      if (labelX + (int)tw > 625) labelX = 625 - tw;
      display.setCursor(labelX, min(dotY + 36, 368));
      display.print(tmStr);
    }
  }
}

void drawSurfBox() {
  // Vertical divider
  display.drawLine(634, 265, 634, 365, GxEPD_BLACK);

  // "SURF" label
  display.setFont(&fonnts_com_Maison_Neue_Bold9pt7b);
  display.setTextColor(C_TEXT_DIM);
  display.setCursor(660, 280);
  display.print("SURF");

  int16_t tx, ty;
  uint16_t tw, th;

  if (surfWaveHeight > 0) {
    // Wave height (centered in surf box)
    display.setFont(&fonnts_com_Maison_Neue_Bold18pt7b);
    display.setTextColor(GxEPD_BLACK);
    char waveStr[15];
    snprintf(waveStr, sizeof(waveStr), "%.0f-%.0fft",
             surfWaveHeight * 0.7, surfWaveHeight);
    display.getTextBounds(waveStr, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor(717 - tw / 2, 315);
    display.print(waveStr);

    // Period
    display.setFont(&fonnts_com_Maison_Neue_Book12pt7b);
    char perStr[12];
    snprintf(perStr, sizeof(perStr), "@ %.0fs", surfWavePeriod);
    display.getTextBounds(perStr, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor(717 - tw / 2, 345);
    display.print(perStr);
  } else {
    display.setFont(&fonnts_com_Maison_Neue_Book12pt7b);
    display.setTextColor(GxEPD_BLACK);
    String noData = "No data";
    display.getTextBounds(noData, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor(717 - tw / 2, 320);
    display.print(noData);
  }
}

void drawWeatherDashboard() {
  USBSerial.println("=== Drawing Weather Dashboard ===");
  feedWatchdog();

  // Brief stabilization for I2C
  delay(200);
  feedWatchdog();

  delay(100);

  // Check power stability
  float currentVoltage = PMU.getBattVoltage() / 1000.0;
  bool usbOK = PMU.isVbusIn();

  if (usbOK && currentVoltage < 3.5) {
    delay(2000);
    feedWatchdog();
    currentVoltage = PMU.getBattVoltage() / 1000.0;
    if (currentVoltage < 3.3) {
      USBSerial.println("Power unstable - skipping refresh");
      return;
    }
  }

  // Full display reinitialization
  SPI.end();
  delay(100);
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  epd_deep_init();
  display.init(115200, true, 2, false);
  display.setRotation(2);
  delay(200);
  feedWatchdog();

  // Colors
  C_HEADER = lowBatteryMode ? GxEPD_BLUE : GxEPD_RED;
  C_TEXT_DIM = lowBatteryMode ? GxEPD_BLUE : GxEPD_RED;

  esp_task_wdt_delete(NULL);

  display.setFullWindow();
  display.firstPage();

  do {
    feedWatchdog();
    display.fillScreen(GxEPD_WHITE);

    int16_t tx, ty;
    uint16_t tw, th;

    // ===== HEADER BAR (Y=0 to Y=70) =====
    display.fillRect(0, 0, W, 70, C_HEADER);

    // Title - left aligned
    display.setFont(&fonnts_com_Maison_Neue_Bold24pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(20, 48);
    display.print("COASTAL WEATHER");

    // Battery indicator - right aligned, upper row (~y=22, visual center ~y=30)
    drawBatteryIndicator(W - 20, 22);

    // Update time - right aligned, lower row
    display.setFont(&fonnts_com_Maison_Neue_Light9pt7b);
    display.setTextColor(GxEPD_WHITE);
    String updateStr = "Updated: " + lastUpdateTime;
    display.getTextBounds(updateStr, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor(W - tw - 20, 58);
    display.print(updateStr);

    // ===== CURRENT WEATHER (Y=70 to Y=260) =====
    int weatherY = 80;
    int iconX = 30;
    int iconY = weatherY + 15;

    // Draw large weather icon (120x120)
    WeatherIcon icon = getWeatherIcon(cachedWeatherCode);
    drawBitmap4bpp(iconX, iconY, largeIcons[icon], 120, 120);

    // Temperature - big and bold
    int textX = 175;
    display.setFont(&fonnts_com_Maison_Neue_Bold24pt7b);
    display.setTextColor(GxEPD_BLACK);

    // Color red if extreme temp
    if (cachedTemp > 85 || cachedTemp < 45) {
      display.setTextColor(GxEPD_RED);
    }

    char tempStr[10];
    snprintf(tempStr, sizeof(tempStr), "%.0fF", cachedTemp);
    display.setCursor(textX, weatherY + 45);
    display.print(tempStr);

    // Weather description
    display.setFont(&fonnts_com_Maison_Neue_Book18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(textX, weatherY + 82);
    display.print(getWeatherDescription(cachedWeatherCode));

    // High / Low
    display.setFont(&fonnts_com_Maison_Neue_Bold15pt7b);
    display.setTextColor(GxEPD_BLACK);
    char hlStr[20];
    snprintf(hlStr, sizeof(hlStr), "H: %.0f  L: %.0f", cachedHighTemp, cachedLowTemp);
    display.setCursor(textX, weatherY + 115);
    display.print(hlStr);

    // Stats line: rain%, wind, UV
    display.setFont(&fonnts_com_Maison_Neue_Book12pt7b);
    int statsY = weatherY + 148;

    // Rain probability
    if (cachedRainProb > 60) {
      display.setTextColor(GxEPD_RED);
    } else {
      display.setTextColor(GxEPD_BLACK);
    }
    char rainStr[15];
    snprintf(rainStr, sizeof(rainStr), "%d%% rain", cachedRainProb);
    display.setCursor(textX, statsY);
    display.print(rainStr);

    // Wind
    display.setTextColor(GxEPD_BLACK);
    char windStr[15];
    snprintf(windStr, sizeof(windStr), "%.0f mph wind", cachedWindSpeed);
    display.setCursor(textX + 140, statsY);
    display.print(windStr);

    // UV index
    display.setFont(&fonnts_com_Maison_Neue_Book12pt7b);
    int uvX = textX + 320;
    if (cachedUvIndex >= 6) {
      display.setTextColor(GxEPD_RED);
    } else {
      display.setTextColor(GxEPD_BLACK);
    }
    char uvStr[20];
    snprintf(uvStr, sizeof(uvStr), "UV: %.0f", cachedUvIndex);
    if (cachedUvIndex >= 6) {
      strcat(uvStr, " High!");
    }
    display.setCursor(uvX, statsY);
    display.print(uvStr);

    // ===== DIVIDER LINE =====
    display.drawLine(0, 260, W, 260, GxEPD_BLACK);

    // ===== TIDES & SURF (Y=260 to Y=370) =====
    drawTideCurve();
    drawSurfBox();

    // ===== DIVIDER LINE =====
    display.drawLine(0, 370, W, 370, GxEPD_BLACK);

    // ===== 7-DAY FORECAST (Y=370 to Y=480) =====
    const char* dayNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    int forecastStartY = 375;
    int colWidth = W / 7;

    for (int i = 0; i < 7; i++) {
      int colX = i * colWidth;
      int centerX = colX + colWidth / 2;

      // Day name
      display.setFont(&fonnts_com_Maison_Neue_Bold9pt7b);
      display.setTextColor(GxEPD_BLACK);
      display.getTextBounds(dayNames[forecastDayOfWeek[i]], 0, 0, &tx, &ty, &tw, &th);
      display.setCursor(centerX - tw / 2, forecastStartY + 15);
      display.print(dayNames[forecastDayOfWeek[i]]);

      // Small icon (40x40)
      WeatherIcon fIcon = getWeatherIcon(forecastCode[i]);
      drawBitmap4bpp(centerX - 20, forecastStartY + 20, smallIcons[fIcon], 40, 40);

      // High/Low temps
      display.setFont(&fonnts_com_Maison_Neue_Book12pt7b);
      char tempBuf[15];

      // High in red if extreme
      if (forecastHigh[i] > 85 || forecastHigh[i] < 45) {
        display.setTextColor(GxEPD_RED);
      } else {
        display.setTextColor(GxEPD_BLACK);
      }
      snprintf(tempBuf, sizeof(tempBuf), "%.0f/%.0f", forecastHigh[i], forecastLow[i]);
      display.getTextBounds(tempBuf, 0, 0, &tx, &ty, &tw, &th);
      display.setCursor(centerX - tw / 2, forecastStartY + 72);
      display.print(tempBuf);
    }

    // Vertical dividers between forecast days
    for (int i = 1; i < 7; i++) {
      display.drawLine(i * colWidth, 370, i * colWidth, H, GxEPD_BLACK);
    }

  } while (display.nextPage());

  epd_wait_busy();
  esp_task_wdt_add(NULL);
  feedWatchdog();
  delay(200);
  USBSerial.flush();

  USBSerial.println("Weather dashboard update complete!");
}


// =============================================================================
// SECTION 20: UPDATE ORCHESTRATOR
// =============================================================================

void updateWeatherAndDisplay() {
  USBSerial.println("\n========== WEATHER UPDATE ==========");
  feedWatchdog();

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    initTime();
    feedWatchdog();

    // Fetch all data - each independent, failure in one doesn't block others
    bool weatherOk = fetchWeatherData();
    feedWatchdog();

    bool tideOk = fetchTideData();
    feedWatchdog();

    bool surfOk = fetchSurfData();
    feedWatchdog();

    disconnectWiFi();

    // Update timestamp
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timeStr[20];
      strftime(timeStr, sizeof(timeStr), "%l:%M %p", &timeinfo);
      lastUpdateTime = String(timeStr);
      lastUpdateTime.trim();
      strncpy(cachedUpdateTime, lastUpdateTime.c_str(), sizeof(cachedUpdateTime) - 1);
      lastWeatherFetchEpoch = mktime(&timeinfo);
    }

    USBSerial.print("Results - Weather: "); USBSerial.print(weatherOk ? "OK" : "FAIL");
    USBSerial.print(" | Tides: "); USBSerial.print(tideOk ? "OK" : "FAIL");
    USBSerial.print(" | Surf: "); USBSerial.println(surfOk ? "OK" : "FAIL");

  } else {
    USBSerial.println("[WARN] WiFi failed - using cached data");
    disconnectWiFi();

    // Restore last known timestamp from RTC, mark as stale
    if (lastUpdateTime.length() == 0 && cachedUpdateTime[0] != '\0') {
      lastUpdateTime = String(cachedUpdateTime);
    }
    lastUpdateTime = lastUpdateTime + " (stale)";
  }

  printBatteryStatus();

  USBSerial.println("Drawing dashboard...");
  feedWatchdog();

  drawWeatherDashboard();
  resetCrashCounter();

  USBSerial.println("========== UPDATE COMPLETE ==========\n");
}


// =============================================================================
// SECTION 21: SERIAL COMMAND HANDLER
// =============================================================================

void handleSerialCommands() {
  while (USBSerial.available()) {
    char c = USBSerial.read();

    if (c == '\n' || c == '\r') {
      if (serialInputBuffer.length() > 0) {
        processSerialCommand(serialInputBuffer);
        serialInputBuffer = "";
      }
    } else {
      // Prevent buffer overflow
      if (serialInputBuffer.length() < 2048) {
        serialInputBuffer += c;
      }
    }
  }
}

void processSerialCommand(String command) {
  command.trim();

  USBSerial.print("CMD: ");
  USBSerial.println(command);

  if (command == "PING") {
    USBSerial.println("PONG");
    USBSerial.println("COASTAL_WEATHER_V1");
  }
  else if (command == "RESTART") {
    USBSerial.println("OK");
    delay(500);
    ESP.restart();
  }
  else if (command == "GO_SLEEP") {
    USBSerial.println("Sleep requested");
    USBSerial.println("OK");
    delay(100);
    sleepRequested = true;
  }
  else if (command == "REFRESH") {
    USBSerial.println("Manual weather refresh...");
    updateWeatherAndDisplay();
    USBSerial.println("REFRESH_DONE");
  }
  else if (command == "STATUS") {
    USBSerial.println("=== Cached Weather Data ===");
    USBSerial.print("Temp: "); USBSerial.print(cachedTemp); USBSerial.println("F");
    USBSerial.print("Code: "); USBSerial.print(cachedWeatherCode);
    USBSerial.print(" ("); USBSerial.print(getWeatherDescription(cachedWeatherCode)); USBSerial.println(")");
    USBSerial.print("Wind: "); USBSerial.print(cachedWindSpeed); USBSerial.println(" mph");
    USBSerial.print("Humidity: "); USBSerial.print(cachedHumidity); USBSerial.println("%");
    USBSerial.print("H/L: "); USBSerial.print(cachedHighTemp); USBSerial.print("/"); USBSerial.println(cachedLowTemp);
    USBSerial.print("Rain: "); USBSerial.print(cachedRainProb); USBSerial.println("%");
    USBSerial.print("UV: "); USBSerial.println(cachedUvIndex);
    USBSerial.print("Tides: "); USBSerial.print(tideCount); USBSerial.println(" events");
    USBSerial.print("Surf: "); USBSerial.print(surfWaveHeight, 1); USBSerial.print("ft @ ");
    USBSerial.print(surfWavePeriod, 0); USBSerial.println("s");
    USBSerial.print("Updated: "); USBSerial.println(lastUpdateTime);
    USBSerial.println("OK");
  }
  else if (command.length() > 0) {
    USBSerial.print("Unknown: ");
    USBSerial.println(command);
    USBSerial.println("ERROR");
  }
}


// =============================================================================
// SECTION 22: SLEEP & WAKE
// =============================================================================

// RTC flag to prevent repeated setup screen draws on brownout reboot
RTC_DATA_ATTR bool setupScreenDrawn = false;

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  wasManualWake = false;

  USBSerial.println("\n========== WAKE UP REASON ==========");

  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      USBSerial.println("Wakeup: USB connected! (GPIO 21 - PMU IRQ)");
      // USB wake is NOT manual wake - just stay idle for commands
      wasManualWake = false;

      // Verify USB is actually connected
      delay(500);  // Give PMU time to stabilize
      if (PMU.isVbusIn()) {
        USBSerial.println("  USB confirmed connected");
        USBSerial.println("  -> Staying idle, waiting for serial commands");
      } else {
        USBSerial.println("  USB not detected (false wake?)");
      }
      break;

    case ESP_SLEEP_WAKEUP_EXT1:
      USBSerial.println("Wakeup: BOOT button pressed");
      wasManualWake = true;  // Only button press is manual wake
      USBSerial.println("  -> Will fetch fresh weather data");
      break;

    case ESP_SLEEP_WAKEUP_TIMER:
      USBSerial.println("Wakeup: Timer expired (scheduled refresh)");
      USBSerial.println("  -> Will fetch fresh weather data");
      break;

    case ESP_SLEEP_WAKEUP_UNDEFINED:
      USBSerial.println("Wakeup: Power-on or Reset button");
      break;

    default:
      USBSerial.print("Wakeup: Unknown reason (");
      USBSerial.print(wakeup_reason);
      USBSerial.println(")");
      break;
  }

  USBSerial.println("====================================\n");
}

void go_to_deep_sleep() {
  USBSerial.println("\n========== ENTERING DEEP SLEEP ==========");

  // Check USB status before sleeping
  bool usbNowConnected = PMU.isVbusIn();
  USBSerial.print("USB status before sleep: ");
  USBSerial.println(usbNowConnected ? "CONNECTED" : "DISCONNECTED");

  if (usbNowConnected) {
    USBSerial.println("WARNING: USB is connected - should not sleep!");
    USBSerial.println("Returning to loop() instead of sleeping...");
    return;  // Don't sleep if USB is connected!
  }

  // Calculate smart sleep duration
  int sleepMinutes = calculateNextWakeMinutes();
  uint64_t sleepDuration;

  if (!isConfigured) {
    sleepDuration = SLEEP_DURATION_UNCONFIGURED_US;
    USBSerial.println("Not configured - sleeping 1 week");
  } else {
    sleepDuration = (uint64_t)sleepMinutes * 60ULL * 1000000ULL;
    USBSerial.print("Smart sleep: ");
    USBSerial.print(sleepMinutes);
    USBSerial.println(" minutes");
  }

  feedWatchdog();
  disconnectWiFi();

  // Configure wake sources
  USBSerial.println("Configuring wake sources:");
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  // 1. Timer wake (for scheduled refresh)
  esp_sleep_enable_timer_wakeup(sleepDuration);
  USBSerial.print("  Timer: ");
  USBSerial.print(sleepMinutes);
  USBSerial.println(" minutes");

  // 2. BOOT button wake (GPIO 0)
  const uint64_t button_mask = 1ULL << GPIO_NUM_0;
  esp_sleep_enable_ext1_wakeup(button_mask, ESP_EXT1_WAKEUP_ANY_LOW);
  USBSerial.println("  BOOT button (GPIO 0)");

  // 3. USB connection wake (GPIO 21 - PMU IRQ)
  // PMU will trigger IRQ on GPIO 21 when USB is plugged in
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 0);  // Wake on LOW (IRQ active low)
  USBSerial.println("  USB connection (GPIO 21 - PMU IRQ)");

  // Configure PMU to generate IRQ on USB insertion
  pmu_prepare_for_esp32_sleep();

  USBSerial.println("Going to sleep NOW...");
  USBSerial.println("Wake triggers: Timer | BOOT button | USB plug-in");
  USBSerial.flush();

  digitalWrite(ACT_LED_PIN, LOW);
  esp_task_wdt_delete(NULL);

  delay(100);  // Give serial time to flush

  esp_deep_sleep_start();
  // Never returns
}


// =============================================================================
// SECTION 23: ARDUINO SETUP
// =============================================================================

void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Initialize PMU FIRST - before anything else
  // This prevents the PMU from doing anything weird with VBUS during boot
  Wire.begin(PMU_SDA, PMU_SCL);
  delay(50);  // Brief I2C stabilization

  if (PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL)) {
    // Immediately accept USB power and stop PMU from interfering
    PMU.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_2000MA);
    PMU.disableVbusVoltageMeasure();
    PMU.disableSleep();
  }

  // Initialize USB composite device (CDC + HID)
  // USBSerial.begin() and USB.begin() both happen inside this function
  // in the correct order so the PC can enumerate successfully
  initUSBComposite();

  // Stabilize after USB enumeration
  delay(2000);

  // Load configuration FIRST (only once at boot)
  loadConfiguration();

  // Initialize pins
  pinMode(ACT_LED_PIN, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_BUSY, INPUT);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_CS, OUTPUT);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(ACT_LED_PIN, LOW);
  digitalWrite(EPD_RST, HIGH);
  digitalWrite(EPD_DC, LOW);
  digitalWrite(EPD_CS, HIGH);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  USBSerial.println("\n======================================================================");
  USBSerial.println("              SC WEATHER V1.0 - USB Composite Device");
  USBSerial.println("              Board identifies as: Coastal Weather (CDC+HID)");
  USBSerial.println("======================================================================");
  USBSerial.print("Configured: "); USBSerial.println(isConfigured ? "YES" : "NO");
  USBSerial.print("Boot count: "); USBSerial.println(bootCount);
  USBSerial.print("USB Composite: "); USBSerial.println(usbCompositeStarted ? "ACTIVE" : "FAILED");

  initWatchdog();

  USBSerial.println("Checking factory reset...");
  if (checkFactoryReset()) {
    setupScreenDrawn = false;
    // Never returns
  }
  USBSerial.println("[OK] No factory reset");

  USBSerial.println("Checking crash loop...");
  if (checkCrashLoop()) {
    enterSafeMode();
  }
  USBSerial.println("[OK] No crash loop");

  USBSerial.println("Initializing PMU IRQ...");
  pmu_irq_init();
  USBSerial.println("[OK] PMU IRQ ready");

  // Verify PMU
  delay(900); // Let I2C settle before talking to PMU
  USBSerial.println("Verifying PMU...");
  if (!PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL)) {
    USBSerial.println("PMU init failed!");
  } else {
    USBSerial.println("[OK] PMU verified");
  }

  print_wakeup_reason();

  USBSerial.println("Configuring PMU for awake state...");
  pmu_configure_awake();
  USBSerial.println("[OK] PMU configured");

  // Delay for PMU stabilization
  USBSerial.println("PMU stabilization (2s)...");
  delay(2000);
  feedWatchdog();
  printBatteryStatus();

  // Check USB connection status EARLY
  bool usbConnected = PMU.isVbusIn();
  USBSerial.print("USB Status: ");
  USBSerial.println(usbConnected ? "CONNECTED" : "DISCONNECTED");

  // Initialize display
  USBSerial.println("Initializing display...");
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  epd_deep_init();
  display.init(115200, true, 2, false);
  display.setRotation(2);
  USBSerial.println("[OK] Display ready");

  // ===== MAIN LOGIC =====

  // Determine wake reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool isUsbWake = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);
  bool isTimerWake = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER);
  bool isButtonWake = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1);
  bool isPowerOn = (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED);

  USBSerial.print("Configured: "); USBSerial.println(isConfigured ? "YES" : "NO");
  USBSerial.print("Wake Type: ");
  if (isUsbWake) USBSerial.println("USB");
  else if (isTimerWake) USBSerial.println("TIMER");
  else if (isButtonWake) USBSerial.println("BUTTON");
  else if (isPowerOn) USBSerial.println("POWER-ON");
  else USBSerial.println("UNKNOWN");

  if (!isConfigured) {
    // ===== NOT CONFIGURED - SETUP MODE =====
    USBSerial.println("\n>>> SETUP REQUIRED <<<");

    // Draw setup screen only if:
    // 1. Never drawn before (setupScreenDrawn = false), OR
    // 2. Power-on boot (fresh start)
    // Skip drawing if USB wake (screen is already correct)

    if (!setupScreenDrawn || isPowerOn) {
      USBSerial.println("Drawing setup screen...");
      drawSetupScreen();
      setupScreenDrawn = true;
    } else if (isUsbWake) {
      USBSerial.println("USB wake - setup screen already displayed, staying idle");
    } else {
      USBSerial.println("Setup screen already drawn - skipping");
    }

    if (usbConnected) {
      USBSerial.println("\n>>> USB CONNECTED - Staying awake for setup <<<");
      // Stay awake in loop(), never sleep while on USB
    } else {
      USBSerial.println("\n>>> ON BATTERY - Going to sleep <<<");
      setupScreenDrawn = false;  // Reset for next boot
      go_to_deep_sleep();
    }

  } else {
    // ===== CONFIGURED - NORMAL OPERATION =====
    USBSerial.println("\n>>> CONFIGURED - Weather Dashboard Mode <<<");

    // Decide if we should fetch and draw:
    // - Timer wake: YES (scheduled refresh)
    // - Button wake: YES (manual refresh)
    // - USB wake: NO (just stay idle for commands)
    // - Power-on: YES (first boot)

    bool shouldUpdate = false;

    if (isUsbWake) {
      USBSerial.println("USB wake - staying idle, NOT fetching data");
      USBSerial.println("(Dashboard already on screen, waiting for commands)");
      shouldUpdate = false;

    } else if (isButtonWake) {
      USBSerial.println("Button wake - fetching fresh data");
      shouldUpdate = true;

    } else if (isTimerWake) {
      USBSerial.println("Timer wake - fetching scheduled update");
      shouldUpdate = true;

    } else if (isPowerOn || bootCount == 1) {
      USBSerial.println("First boot or power-on - fetching initial data");
      shouldUpdate = true;

    } else {
      USBSerial.println("Unknown wake - fetching data to be safe");
      shouldUpdate = true;
    }

    if (shouldUpdate) {
      if (wasManualWake) {
        USBSerial.println("Manual wake - stabilizing...");
        for (int i = 3; i > 0; i--) {
          USBSerial.print(i); USBSerial.println("...");
          delay(1000);
          feedWatchdog();
        }
        blinkLED(3);
      }

      updateWeatherAndDisplay();
      blinkLED(2);
    } else {
      USBSerial.println("Skipping update - dashboard already current");
    }

    // Check USB status AFTER any updates
    usbConnected = PMU.isVbusIn();

    if (usbConnected) {
      USBSerial.println("\n>>> USB CONNECTED - Staying awake for commands <<<");
      // Stay awake in loop(), never sleep while on USB
    } else {
      USBSerial.println("\n>>> ON BATTERY - Going to sleep <<<");
      go_to_deep_sleep();
    }
  }

  bootCount++;
}


// =============================================================================
// SECTION 24: ARDUINO LOOP (runs when USB connected)
// =============================================================================

void loop() {
  static unsigned long lastBatteryCheck = 0;
  static unsigned long lastUsbCheck = 0;
  static int usbDisconnectCount = 0;

  feedWatchdog();

  // =====================================================================
  // FOOLPROOF USB DETECTION - TRIPLE VERIFICATION
  // Board NEVER sleeps unless ALL THREE checks confirm USB is gone:
  // 1. PMU says disconnected
  // 2. USBSerial is not working
  // 3. 10 consecutive checks (10 seconds) confirm disconnection
  // =====================================================================

  unsigned long currentMillis = millis();

  // Check USB status every 1 second
  if (currentMillis - lastUsbCheck >= 1000) {
    lastUsbCheck = currentMillis;

    // TRIPLE VERIFICATION:
    // Check 1: PMU reports USB status
    PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL);
    delay(50);
    bool pmuSaysConnected = PMU.isVbusIn();

    // Check 2: USBSerial connection is alive
    bool serialWorks = (bool)USBSerial;

    // Check 3: Combined verification
    bool usbActuallyConnected = pmuSaysConnected || serialWorks;

    if (!usbActuallyConnected) {
      // USB might be disconnected - start counting
      usbDisconnectCount++;

      USBSerial.print("USB disconnect check ");
      USBSerial.print(usbDisconnectCount);
      USBSerial.print("/10 (PMU: ");
      USBSerial.print(pmuSaysConnected ? "ON" : "OFF");
      USBSerial.print(", USBSerial: ");
      USBSerial.print(serialWorks ? "WORKS" : "DEAD");
      USBSerial.println(")");

      // ONLY sleep after 10 consecutive "disconnected" readings (10 seconds)
      if (usbDisconnectCount >= 10) {
        USBSerial.println("\n=== USB CONFIRMED DISCONNECTED (10 checks) ===");
        USBSerial.println("Going to sleep...");
        delay(500);
        go_to_deep_sleep();
        return;
      }

    } else {
      // USB is connected - reset counter
      if (usbDisconnectCount > 0) {
        USBSerial.print("[Coastal Weather] USB reconnected (was at ");
        USBSerial.print(usbDisconnectCount);
        USBSerial.println("/10) - staying awake");
        usbDisconnectCount = 0;
      }
    }
  }

  // Handle serial commands
  handleSerialCommands();

  // Check if sleep was requested (from serial command)
  if (sleepRequested) {
    sleepRequested = false;

    // Triple-check USB before sleeping
    PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL);
    delay(50);
    bool pmuSaysConnected = PMU.isVbusIn();
    bool serialWorks = (bool)USBSerial;

    if (!pmuSaysConnected && !serialWorks) {
      USBSerial.println("\n>>> Executing requested sleep (USB verified off) <<<");
      delay(200);
      go_to_deep_sleep();
      return;
    } else {
      USBSerial.println("Sleep requested but USB still connected - STAYING AWAKE");
      USBSerial.print("    PMU: ");
      USBSerial.print(pmuSaysConnected ? "CONNECTED" : "DISCONNECTED");
      USBSerial.print(", USBSerial: ");
      USBSerial.println(serialWorks ? "WORKS" : "DEAD");
    }
  }

  // ===== BOOT BUTTON - Manual refresh =====
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    USBSerial.println("\n>>> BOOT BUTTON - MANUAL REFRESH <<<");
    while (digitalRead(BOOT_BUTTON_PIN) == LOW) { delay(10); feedWatchdog(); }
    blinkLED(2);

    if (isConfigured) {
      USBSerial.println("Fetching fresh weather data...");
      updateWeatherAndDisplay();

      // Triple-check before sleeping
      PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL);
      delay(50);
      bool pmuSaysConnected = PMU.isVbusIn();
      bool serialWorks = (bool)USBSerial;

      if (!pmuSaysConnected && !serialWorks) {
        USBSerial.println(">>> On battery - sleeping <<<");
        go_to_deep_sleep();
        return;
      } else {
        USBSerial.println(">>> USB connected - staying awake <<<");
      }
    } else {
      // Not configured, show setup screen
      if (!setupScreenDrawn) {
        drawSetupScreen();
        setupScreenDrawn = true;
      }
    }
  }

  // ===== KEY BUTTON - Reserved for future use =====
  if (digitalRead(USER_BUTTON_PIN) == LOW) {
    USBSerial.println("\n>>> KEY BUTTON <<<");
    while (digitalRead(USER_BUTTON_PIN) == LOW) { delay(10); feedWatchdog(); }
    blinkLED(1);
    // No action currently
  }

  // LED indicator - slow blink while ready for commands
  digitalWrite(ACT_LED_PIN, ((currentMillis / 1000) % 2) ? HIGH : LOW);

  // Battery check every 30 seconds
  if (currentMillis - lastBatteryCheck >= 30000) {
    printBatteryStatus();
    lastBatteryCheck = currentMillis;
  }

  delay(100);
}
