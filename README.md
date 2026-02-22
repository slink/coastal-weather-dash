# Coastal Weather Dashboard

An e-paper dashboard that shows weather, tides, and surf conditions. Battery powered, auto-updates throughout the day, looks great on a shelf. No API keys needed.

---

## What It Does

Shows current weather, today's tides, surf conditions, and a 7-day forecast on a 7.5" color e-ink display. Updates six times a day (6am, 9am, 12pm, 3pm, 6pm, 9pm) and sleeps the rest of the time. Runs for weeks on a single charge.

That's it. That's the project.

---

## What You Need

- **Waveshare ESP32-S3-PhotoPainter** - comes with the 7.5" color e-paper display already attached, LiPo battery, and USB-C cable all included
- **Arduino IDE** - free

That's the whole shopping list. The data sources are all free public APIs - no accounts, no keys, no subscriptions.

---

## Setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Add ESP32 board support (v2.0.17) via Board Manager
3. Install libraries: **GxEPD2**, **ArduinoJson**, **XPowersLib**
4. Copy `firmware/config.example.h` to `firmware/config.h`
5. Edit `config.h` with your WiFi credentials, coordinates, and station IDs
6. Set the board settings below (the Flash Mode one matters!)
7. Upload `firmware/CoastalWeather_V1.ino`
8. Done. Go check the surf.

### Finding Your Station IDs

- **Tide station:** Find yours at [tidesandcurrents.noaa.gov](https://tidesandcurrents.noaa.gov/) - search by location, grab the station ID from the URL
- **Buoy station:** Find yours at [ndbc.noaa.gov](https://www.ndbc.noaa.gov/) - click the nearest buoy on the map

---

## Arduino IDE Settings

Get these right or your board will be sad:

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Disabled |
| USB Mode | USB-OTG (TinyUSB) |
| Flash Mode | **DIO** (NOT OPI!) |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |

The Flash Mode one is important. Ask me how I know.

---

## Buttons

| Button | What it does |
|--------|--------------|
| KEY | Absolutely nothing. It's there for vibes. |
| BOOT | Force screen refresh and data fetch |
| PWR | Power on/off |

---

## What's In Here

```
coastal-weather-dash/
├── firmware/
│   ├── CoastalWeather_V1.ino  ← The Arduino code
│   ├── config.example.h       ← Copy to config.h, add your settings
│   ├── weather_icons.h        ← Generated icon bitmaps
│   └── Fonts/                 ← Custom display fonts
├── tools/
│   ├── generate_icons.py      ← Icon generation pipeline
│   ├── png_to_4bpp.py         ← PNG to PROGMEM converter
│   └── icons/                 ← Source PNGs for weather icons
├── docs/
│   └── plans/                 ← Design documents
├── README.md                  ← You are here
└── LICENSE                    ← MIT
```

---

## Data Sources

All free, all public, no API keys required:

| Data | Source | What it provides |
|------|--------|-----------------|
| Weather | [Open-Meteo](https://open-meteo.com/) | Current conditions + 7-day forecast |
| Tides | [NOAA CO-OPS](https://tidesandcurrents.noaa.gov/) | Tide predictions by station |
| Surf | [NOAA NDBC](https://www.ndbc.noaa.gov/) | Real-time buoy wave height and period |

---

## Credits

Forked from [Ibis Dash](https://github.com/jdlcdl/ibis-dash) by jdlcdl - an excellent Strava e-paper dashboard that provided the hardware platform, power management, and display infrastructure this project builds on.

Built with [GxEPD2](https://github.com/ZinggJM/GxEPD2), [ArduinoJson](https://arduinojson.org/), and [XPowersLib](https://github.com/lewisxhe/XPowersLib).

---

## License

MIT - use it, modify it, make it better.
