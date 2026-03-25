#pragma once

// Default Wi-Fi credentials from file (used only if there is no saved Wi-Fi in NVS).
// IMPORTANT: ESP32-S3 supports only 2.4 GHz Wi-Fi networks.
// A 5 GHz SSID will not connect.
// Leave empty strings if you do not want hardcoded defaults.
#define WIFI_DEFAULT_SSID            "TP-Link_2AB4"
#define WIFI_DEFAULT_PASSWORD        "9666280519616669"
#define WIFI_DEFAULT_AUTO_CONNECT    1

// Optional weather location for Web screen (Open-Meteo)
#define WEATHER_LAT                  54.4815
#define WEATHER_LON                  53.4710
#define WEATHER_LABEL                "Oktyabrsky, Bashkortostan"
// GitHub settings for Web screen.
// Optional: set token to improve rate limits and data quality.
// Keep empty string to use unauthenticated mode/fallback sources.
#define GITHUB_USERNAME              "WizardJIOCb"
#ifndef GITHUB_TOKEN
#define GITHUB_TOKEN                 ""
#endif

/* ESP32S3 */
#define PIN_LCD_BL                   38

#define PIN_LCD_D0                   39
#define PIN_LCD_D1                   40
#define PIN_LCD_D2                   41
#define PIN_LCD_D3                   42
#define PIN_LCD_D4                   45
#define PIN_LCD_D5                   46
#define PIN_LCD_D6                   47
#define PIN_LCD_D7                   48

#define PIN_POWER_ON                 15

#define PIN_LCD_RES                  5
#define PIN_LCD_CS                   6
#define PIN_LCD_DC                   7
#define PIN_LCD_WR                   8
#define PIN_LCD_RD                   9

#define PIN_BUTTON_1                 0
#define PIN_BUTTON_2                 14
#define PIN_BAT_VOLT                 4

#define PIN_IIC_SCL                  17
#define PIN_IIC_SDA                  18

#define PIN_TOUCH_INT                16
#define PIN_TOUCH_RES                21







