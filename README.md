# LilyHammer (LILYGO T-Display-S3 Touch)

LilyHammer is a custom firmware for **LILYGO T-Display-S3 Touch (ESP32-S3)** with a swipe-based touchscreen UI, Wi-Fi/Bluetooth tools, web data widgets, and fun full-screen animations.

Repository: https://github.com/WizardJIOCb/LilyHammer

## What It Does

Main menu cards (infinite loop swipe):

- Timer
- Homer (full-screen GIF)
- Animation (interactive bouncing ball)
- System info
- WiFi manager
- Bluetooth manager
- Web (Weather + GitHub stats)
- Settings
- Activity monitor
- Fun (full-screen GIF)

## Controls

Touch:

- Swipe left/right in main menu to switch cards
- Tap center card to open screen
- In content screens, use UI buttons

Hardware buttons:

- `BUTTON_1` (`GPIO0`): Back to main menu
- `BUTTON_2` (`GPIO14`): Next menu card (looped)

## Wi-Fi Behavior

- Firmware supports **auto-connect on boot**
- Priority order:
1. Saved Wi-Fi credentials (NVS)
2. Defaults from `examples/wizard_menu/pin_config.h`
- Auto reconnect attempts run in background if connection is lost

Default config location:

- `examples/wizard_menu/pin_config.h`

Key settings:

- `WIFI_DEFAULT_SSID`
- `WIFI_DEFAULT_PASSWORD`
- `WIFI_DEFAULT_AUTO_CONNECT`

## Web Screen

### Weather

- Source: Open-Meteo
- Configurable by:
  - `WEATHER_LAT`
  - `WEATHER_LON`
  - `WEATHER_LABEL`

### GitHub

- Username via `GITHUB_USERNAME`
- Optional token via `GITHUB_TOKEN` (better rate limits)
- Shows yearly public contribution heatmap

## Bluetooth Screen

- BLE scan
- Device list with RSSI
- Details popup on tap
- Vendor/name inference from advertising data when possible

Note: many BLE devices intentionally hide full names; in such cases only partial metadata is available.

## GIF Screens

- `Fun` loads `/images/this-is-fine.gif`
- `Homer` loads `/images/homer.gif`
- Files are stored in LittleFS (`data/images/...`)

## Build And Flash (PlatformIO)

Project default environment is already set to `wizard_menu` in `platformio.ini`.

Build:

```powershell
platformio run -e wizard_menu
```

Upload:

```powershell
platformio run -e wizard_menu -t upload
```

Serial monitor:

```powershell
platformio device monitor -b 115200
```

Upload LittleFS files (GIFs):

```powershell
platformio run -e wizard_menu -t uploadfs
```

## Quick Dev Script

A helper script is included:

- `start-dev.bat`

It rebuilds and uploads firmware for quick iteration.

## Important Notes

- ESP32-S3 supports **2.4 GHz Wi-Fi only**
- If flashing fails, check USB port and boot mode
- Keep secrets safe: avoid committing real API keys/tokens to public repos

## Core Files

- Firmware entry: `examples/wizard_menu/wizard_menu.ino`
- Board/project config: `platformio.ini`
- Runtime config: `examples/wizard_menu/pin_config.h`

## License / Credits

- Based on LILYGO T-Display-S3 platform examples and ESP32 Arduino ecosystem
- Custom LilyHammer UI and features by project contributors
