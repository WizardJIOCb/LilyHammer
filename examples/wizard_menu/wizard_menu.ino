#include "Arduino.h"
#include "TFT_eSPI.h"
#include "TouchLib.h"
#include "Wire.h"
#include "WiFi.h"
#include "Preferences.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include "pin_config.h"

#define LCD_MODULE_CMD_1

#if defined(TOUCH_MODULES_CST_MUTUAL)
TouchLib touch(Wire, PIN_IIC_SDA, PIN_IIC_SCL, CTS328_SLAVE_ADDRESS, PIN_TOUCH_RES);
#elif defined(TOUCH_MODULES_CST_SELF)
TouchLib touch(Wire, PIN_IIC_SDA, PIN_IIC_SCL, CTS820_SLAVE_ADDRESS, PIN_TOUCH_RES);
#else
#error "Please choose touch model in build_flags"
#endif

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

constexpr int16_t kScreenW = 320;
constexpr int16_t kScreenH = 170;
constexpr int16_t kBackBtnW = 62;
constexpr int16_t kBackBtnH = 26;
constexpr int16_t kSwipeThreshold = 45;
constexpr int16_t kTapMoveThreshold = 12;
constexpr uint16_t kTapTimeMs = 320;
constexpr bool kTouchMirrorX = true;
constexpr bool kTouchMirrorY = false;

#ifndef WIFI_DEFAULT_SSID
#define WIFI_DEFAULT_SSID ""
#endif
#ifndef WIFI_DEFAULT_PASSWORD
#define WIFI_DEFAULT_PASSWORD ""
#endif
#ifndef WIFI_DEFAULT_AUTO_CONNECT
#define WIFI_DEFAULT_AUTO_CONNECT 1
#endif
#ifndef WEATHER_LAT
#define WEATHER_LAT 55.7558
#endif
#ifndef WEATHER_LON
#define WEATHER_LON 37.6173
#endif
#ifndef WEATHER_LABEL
#define WEATHER_LABEL "Default city"
#endif

enum class Screen : uint8_t {
  Menu,
  Timer,
  Homer,
  Animation,
  SystemInfo,
  WiFiManager,
  WiFiPassword,
  Web,
  Settings,
  Activity
};

const char *kMenuItems[] = {
  "Timer",
  "Homer picture",
  "Animation",
  "System info",
  "WiFi manager",
  "Web",
  "Settings",
  "Activity monitor"
};
constexpr uint8_t kMenuCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);

Screen currentScreen = Screen::Menu;
uint8_t selectedMenuIndex = 0;
bool needFullRedraw = true;

struct {
  bool active = false;
  int16_t startX = 0;
  int16_t startY = 0;
  int16_t lastX = 0;
  int16_t lastY = 0;
  uint32_t startMs = 0;
} touchState;

// Timer
bool timerRunning = false;
uint32_t timerAccumulatedMs = 0;
uint32_t timerStartMs = 0;
uint32_t lastTimerDrawMs = UINT32_MAX;

// Animation
int16_t ballX = 30;
int16_t ballY = 90;
int16_t ballVx = 2;
int16_t ballVy = 2;
uint32_t lastAnimFrameMs = 0;

// System
uint32_t lastSystemRefreshMs = 0;

// Activity
uint8_t activityGraph[60] = {0};
uint8_t activityHead = 0;
uint32_t lastActivityRefreshMs = 0;
uint32_t loopCounter = 0;
uint32_t fps = 0;

// WiFi manager
struct WiFiNet {
  String ssid;
  int32_t rssi;
  wifi_auth_mode_t auth;
};
WiFiNet wifiNets[8];
uint8_t wifiNetCount = 0;
uint8_t wifiSelected = 0;
String wifiSavedSsid;
String wifiSavedPass;
bool wifiAutoConnect = WIFI_DEFAULT_AUTO_CONNECT != 0;
String wifiStatusText = "Idle";
bool wifiConnecting = false;
uint32_t wifiConnectStartMs = 0;

// WiFi password input
String wifiInputSsid;
String wifiInputPass;
bool wifiKbShift = false;
bool wifiKbSymbols = false;

// Web
String weatherText = "No data";
String githubText = "No data";
String githubRepo = "-";
uint8_t githubDayCounts[7] = {0};
char githubDayLabels[7][6] = {{0}};

// Settings
int backlightValue = 255;

#if defined(LCD_MODULE_CMD_1)
typedef struct {
  uint8_t cmd;
  uint8_t data[14];
  uint8_t len;
} lcd_cmd_t;

lcd_cmd_t lcd_st7789v[] = {
  {0x11, {0}, 0 | 0x80},
  {0x3A, {0x05}, 1},
  {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33}, 5},
  {0xB7, {0x75}, 1},
  {0xBB, {0x28}, 1},
  {0xC0, {0x2C}, 1},
  {0xC2, {0x01}, 1},
  {0xC3, {0x1F}, 1},
  {0xC6, {0x13}, 1},
  {0xD0, {0xA7}, 1},
  {0xD0, {0xA4, 0xA1}, 2},
  {0xD6, {0xA1}, 1},
  {0xE0, {0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B, 0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32}, 14},
  {0xE1, {0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B, 0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37}, 14},
};
#endif

void applyBacklight() {
  backlightValue = constrain(backlightValue, 20, 255);
  analogWrite(PIN_LCD_BL, backlightValue);
}

void prefsLoad() {
  prefs.begin("wizard", false);
  wifiSavedSsid = prefs.getString("wifi_ssid", "");
  wifiSavedPass = prefs.getString("wifi_pass", "");
  wifiAutoConnect = prefs.getBool("wifi_auto", WIFI_DEFAULT_AUTO_CONNECT != 0);
  backlightValue = prefs.getInt("bl", 255);
}

void prefsSaveWiFi(const String &ssid, const String &pass) {
  wifiSavedSsid = ssid;
  wifiSavedPass = pass;
  prefs.putString("wifi_ssid", wifiSavedSsid);
  prefs.putString("wifi_pass", wifiSavedPass);
}

void prefsForgetWiFi() {
  wifiSavedSsid = "";
  wifiSavedPass = "";
  prefs.remove("wifi_ssid");
  prefs.remove("wifi_pass");
}

void prefsSaveSettings() {
  prefs.putBool("wifi_auto", wifiAutoConnect);
  prefs.putInt("bl", backlightValue);
}

void connectToWiFi(const String &ssid, const String &pass, bool saveCreds) {
  if (ssid.length() == 0) {
    wifiStatusText = "SSID is empty";
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  wifiConnecting = true;
  wifiConnectStartMs = millis();
  wifiStatusText = "Connecting to " + ssid;
  if (saveCreds) {
    prefsSaveWiFi(ssid, pass);
  }
}

void updateWiFiConnectionState() {
  if (!wifiConnecting) return;
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    wifiConnecting = false;
    wifiStatusText = "Connected: " + WiFi.localIP().toString();
    return;
  }
  if (millis() - wifiConnectStartMs > 20000) {
    wifiConnecting = false;
    wifiStatusText = "Connect timeout";
    WiFi.disconnect();
  }
}

void scanWiFiNetworks() {
  wifiStatusText = "Scanning...";
  int found = WiFi.scanNetworks(false, true);
  if (found < 0) {
    wifiStatusText = "Scan failed";
    wifiNetCount = 0;
    return;
  }
  wifiNetCount = (found > 8) ? 8 : found;
  for (uint8_t i = 0; i < wifiNetCount; i++) {
    wifiNets[i].ssid = WiFi.SSID(i);
    wifiNets[i].rssi = WiFi.RSSI(i);
    wifiNets[i].auth = WiFi.encryptionType(i);
  }
  if (wifiNetCount == 0) wifiStatusText = "No networks";
  else wifiStatusText = "Found " + String(found) + " networks";
}

bool isBackTap(int16_t x, int16_t y) {
  return (x >= 6 && x <= (6 + kBackBtnW) && y >= 6 && y <= (6 + kBackBtnH));
}

void drawBackButton() {
  tft.fillRoundRect(6, 6, kBackBtnW, kBackBtnH, 6, TFT_DARKGREY);
  tft.drawRoundRect(6, 6, kBackBtnW, kBackBtnH, 6, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString("< BACK", 12, 13, 2);
}

void drawTitle(const char *title, uint16_t bg = TFT_NAVY) {
  tft.fillRect(0, 0, kScreenW, 36, bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.drawString(title, 78, 10, 2);
  drawBackButton();
}

void drawMenuScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, kScreenW, 30, TFT_DARKCYAN);
  tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
  tft.drawCentreString("LilyHammer Menu", kScreenW / 2, 7, 2);

  tft.fillRoundRect(20, 40, 280, 92, 12, TFT_DARKGREY);
  tft.drawRoundRect(20, 40, 280, 92, 12, TFT_CYAN);
  tft.setTextColor(TFT_YELLOW, TFT_DARKGREY);
  tft.drawCentreString(kMenuItems[selectedMenuIndex], kScreenW / 2, 72, 4);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawCentreString("Swipe left/right to switch", kScreenW / 2, 138, 2);
  tft.drawCentreString("Tap center to open", kScreenW / 2, 154, 2);
}

void drawTimerControls() {
  tft.fillRoundRect(20, 108, 130, 42, 8, timerRunning ? TFT_ORANGE : TFT_GREEN);
  tft.setTextColor(TFT_BLACK, timerRunning ? TFT_ORANGE : TFT_GREEN);
  tft.drawCentreString(timerRunning ? "PAUSE" : "START", 85, 122, 2);

  tft.fillRoundRect(170, 108, 130, 42, 8, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawCentreString("RESET", 235, 122, 2);
}

void drawTimerScreen(bool full) {
  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("Timer");
    drawTimerControls();
  }

  uint32_t now = millis();
  uint32_t elapsed = timerAccumulatedMs + (timerRunning ? now - timerStartMs : 0);
  if (!full && elapsed == lastTimerDrawMs) return;

  uint32_t centi = (elapsed / 10) % 100;
  uint32_t sec = (elapsed / 1000) % 60;
  uint32_t min = elapsed / 60000;
  char buf[24];
  snprintf(buf, sizeof(buf), "%02lu:%02lu.%02lu", (unsigned long)min, (unsigned long)sec, (unsigned long)centi);

  tft.fillRect(20, 46, 280, 46, TFT_BLACK);
  tft.drawRect(20, 46, 280, 46, TFT_DARKGREY);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString(buf, kScreenW / 2, 57, 4);
  lastTimerDrawMs = elapsed;
}

void drawHomerScreen() {
  tft.fillScreen(TFT_BLACK);
  drawTitle("Homer picture");

  tft.fillCircle(115, 96, 42, TFT_YELLOW);
  tft.fillRect(90, 66, 50, 15, TFT_YELLOW);
  tft.fillRect(92, 115, 45, 20, TFT_YELLOW);

  tft.fillCircle(103, 92, 10, TFT_WHITE);
  tft.fillCircle(122, 92, 10, TFT_WHITE);
  tft.fillCircle(106, 93, 3, TFT_BLACK);
  tft.fillCircle(125, 93, 3, TFT_BLACK);
  tft.drawLine(95, 116, 132, 116, TFT_BLACK);

  tft.fillRect(176, 52, 132, 84, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("Demo image", 184, 72, 2);
  tft.drawString("Tap BACK", 184, 102, 2);
}

void drawAnimationScreen(bool full) {
  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("Animation");
    tft.drawRect(10, 42, 300, 118, TFT_DARKGREY);
    ballX = 30;
    ballY = 90;
    ballVx = 2;
    ballVy = 2;
    lastAnimFrameMs = 0;
  }

  uint32_t now = millis();
  if (!full && now - lastAnimFrameMs < 20) return;
  lastAnimFrameMs = now;

  tft.fillCircle(ballX, ballY, 9, TFT_BLACK);
  ballX += ballVx;
  ballY += ballVy;
  if (ballX < 20 || ballX > 300) ballVx = -ballVx;
  if (ballY < 52 || ballY > 150) ballVy = -ballVy;
  uint16_t color = tft.color565((ballX * 3) % 255, (ballY * 2) % 255, (ballX + ballY) % 255);
  tft.fillCircle(ballX, ballY, 9, color);
}

void drawSystemInfoScreen(bool full) {
  uint32_t now = millis();
  if (!full && now - lastSystemRefreshMs < 500) return;
  lastSystemRefreshMs = now;

  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("System info");
  }

  tft.fillRect(10, 42, 300, 118, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String chipModel = ESP.getChipModel();
  char line[80];
  snprintf(line, sizeof(line), "Chip: %s rev %d", chipModel.c_str(), ESP.getChipRevision());
  tft.drawString(line, 14, 48, 2);
  snprintf(line, sizeof(line), "CPU: %d MHz", ESP.getCpuFreqMHz());
  tft.drawString(line, 14, 68, 2);
  snprintf(line, sizeof(line), "Heap free: %u KB", (unsigned int)(ESP.getFreeHeap() / 1024));
  tft.drawString(line, 14, 88, 2);
  snprintf(line, sizeof(line), "Flash: %u MB", (unsigned int)(ESP.getFlashChipSize() / (1024 * 1024)));
  tft.drawString(line, 14, 108, 2);
  snprintf(line, sizeof(line), "WiFi: %s", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "not connected");
  tft.drawString(line, 14, 128, 2);
}

void drawWiFiManagerScreen(bool full) {
  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("WiFi manager");
  }

  tft.fillRect(10, 42, 300, 118, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.fillRoundRect(10, 44, 70, 22, 5, TFT_DARKCYAN);
  tft.drawCentreString("SCAN", 45, 50, 2);
  tft.fillRoundRect(88, 44, 70, 22, 5, TFT_DARKGREEN);
  tft.drawCentreString("SAVED", 123, 50, 2);
  tft.fillRoundRect(166, 44, 70, 22, 5, TFT_DARKGREY);
  tft.drawCentreString("FORGET", 201, 50, 2);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(wifiStatusText, 12, 70, 2);

  for (uint8_t i = 0; i < wifiNetCount && i < 5; i++) {
    int16_t y = 90 + i * 15;
    uint16_t bg = (i == wifiSelected) ? TFT_NAVY : TFT_BLACK;
    tft.fillRect(10, y - 1, 300, 14, bg);
    tft.setTextColor(TFT_WHITE, bg);
    String lock = (wifiNets[i].auth == WIFI_AUTH_OPEN) ? " " : "*";
    String s = String(i + 1) + ". " + wifiNets[i].ssid + " (" + String(wifiNets[i].rssi) + ")" + lock;
    if (s.length() > 42) s = s.substring(0, 42);
    tft.drawString(s, 12, y, 1);
  }
}

const char *kbRowsAlpha[3] = {"qwertyuiop", "asdfghjkl_", "zxcvbnm123"};
const char *kbRowsSym[3] = {"1234567890", "!@#$%^&*()", "._-+=/?:;,"};

void drawWiFiPasswordScreen(bool full) {
  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("WiFi password");
  }

  tft.fillRect(10, 42, 300, 128, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  String ss = "SSID: " + wifiInputSsid;
  if (ss.length() > 32) ss = ss.substring(0, 32);
  tft.drawString(ss, 12, 44, 2);

  String masked = "";
  for (uint16_t i = 0; i < wifiInputPass.length(); i++) masked += "*";
  String pwd = "PWD: " + masked;
  if (pwd.length() > 40) pwd = pwd.substring(0, 40);
  tft.drawString(pwd, 12, 60, 2);

  struct Btn { const char *txt; int x; int w; uint16_t c; };
  Btn topBtns[6] = {
    {"SHIFT", 10, 48, TFT_DARKGREY},
    {"SYM", 60, 38, TFT_DARKGREY},
    {"BKSP", 100, 52, TFT_DARKGREY},
    {"CLR", 154, 36, TFT_DARKGREY},
    {"OK", 192, 34, TFT_DARKGREEN},
    {"CAN", 228, 40, tft.color565(120,0,0)},
  };
  for (uint8_t i = 0; i < 6; i++) {
    tft.fillRoundRect(topBtns[i].x, 78, topBtns[i].w, 20, 4, topBtns[i].c);
    tft.setTextColor(TFT_WHITE, topBtns[i].c);
    tft.drawCentreString(topBtns[i].txt, topBtns[i].x + topBtns[i].w / 2, 83, 1);
  }

  const char **rows = wifiKbSymbols ? kbRowsSym : kbRowsAlpha;
  for (uint8_t r = 0; r < 3; r++) {
    for (uint8_t c = 0; c < 10; c++) {
      int16_t x = 10 + c * 30;
      int16_t y = 102 + r * 22;
      tft.fillRoundRect(x, y, 28, 20, 3, TFT_DARKCYAN);
      char ch = rows[r][c];
      if (!wifiKbSymbols && wifiKbShift && ch >= 'a' && ch <= 'z') ch = ch - 32;
      char text[2] = {ch, 0};
      tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
      tft.drawCentreString(text, x + 14, y + 5, 2);
    }
  }
}

int parseIntField(const String &json, const char *key, int fallback) {
  String pat = String("\"") + key + "\":";
  int p = json.indexOf(pat);
  if (p < 0) return fallback;
  p += pat.length();
  int e = p;
  while (e < (int)json.length() && (json[e] == '-' || json[e] == '.' || (json[e] >= '0' && json[e] <= '9'))) e++;
  return json.substring(p, e).toInt();
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    weatherText = "Weather: no WiFi";
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(WEATHER_LAT, 4) +
               "&longitude=" + String(WEATHER_LON, 4) +
               "&current=temperature_2m,weather_code,wind_speed_10m&timezone=auto";

  if (!http.begin(client, url)) {
    weatherText = "Weather: begin failed";
    return;
  }
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) {
    weatherText = "Weather HTTP " + String(code);
    http.end();
    return;
  }
  String body = http.getString();
  http.end();

  int t = parseIntField(body, "temperature_2m", -999);
  int w = parseIntField(body, "wind_speed_10m", -1);
  int c = parseIntField(body, "weather_code", -1);
  weatherText = String(WEATHER_LABEL) + ": " + String(t) + "C wind " + String(w) + " code " + String(c);
}

void fetchGitHub() {
  if (WiFi.status() != WL_CONNECTED) {
    githubText = "GitHub: no WiFi";
    return;
  }

  for (uint8_t i = 0; i < 7; i++) {
    githubDayCounts[i] = 0;
    githubDayLabels[i][0] = 0;
  }
  githubRepo = "-";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.github.com/users/WizardJIOCb/events/public?per_page=30";

  if (!http.begin(client, url)) {
    githubText = "GitHub: begin failed";
    return;
  }
  http.addHeader("User-Agent", "LilyHammer-TDisplayS3");
  http.setTimeout(10000);
  int code = http.GET();
  if (code != 200) {
    githubText = "GitHub HTTP " + String(code);
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  int pushCount = 0;
  int pos = 0;
  while (true) {
    int p = body.indexOf("\"type\":\"PushEvent\"", pos);
    if (p < 0) break;
    pushCount++;

    if (githubRepo == "-") {
      int rp = body.indexOf("\"name\":\"", p);
      if (rp > 0) {
        rp += 8;
        int re = body.indexOf("\"", rp);
        if (re > rp) githubRepo = body.substring(rp, re);
      }
    }

    int cp = body.indexOf("\"created_at\":\"", p);
    if (cp > 0) {
      cp += 14;
      if (cp + 10 <= (int)body.length()) {
        String d = body.substring(cp, cp + 10);
        bool found = false;
        for (uint8_t i = 0; i < 7; i++) {
          if (githubDayLabels[i][0] != 0 && d.equals(String(githubDayLabels[i]))) {
            githubDayCounts[i]++;
            found = true;
            break;
          }
        }
        if (!found) {
          for (uint8_t i = 0; i < 7; i++) {
            if (githubDayLabels[i][0] == 0) {
              d.toCharArray(githubDayLabels[i], sizeof(githubDayLabels[i]));
              githubDayCounts[i] = 1;
              break;
            }
          }
        }
      }
    }

    pos = p + 1;
  }

  githubText = "Push events: " + String(pushCount) + " /30";
}

void drawWebScreen(bool full) {
  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("Web");
  }

  tft.fillRect(10, 42, 300, 118, TFT_BLACK);
  tft.fillRoundRect(10, 44, 84, 24, 5, TFT_DARKCYAN);
  tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
  tft.drawCentreString("WEATHER", 52, 51, 2);

  tft.fillRoundRect(100, 44, 84, 24, 5, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.drawCentreString("GITHUB", 142, 51, 2);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  String wt = weatherText;
  if (wt.length() > 48) wt = wt.substring(0, 48);
  tft.drawString(wt, 12, 74, 1);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  String gt = githubText;
  if (gt.length() > 48) gt = gt.substring(0, 48);
  tft.drawString(gt, 12, 88, 1);
  tft.drawString("Last repo: " + githubRepo, 12, 100, 1);

  uint8_t maxV = 1;
  for (uint8_t i = 0; i < 7; i++) if (githubDayCounts[i] > maxV) maxV = githubDayCounts[i];
  for (uint8_t i = 0; i < 7; i++) {
    int16_t x = 14 + i * 42;
    int16_t h = (githubDayCounts[i] * 44) / maxV;
    tft.fillRect(x, 152 - h, 18, h, TFT_ORANGE);
    if (githubDayLabels[i][0] != 0) {
      String d = String(githubDayLabels[i]);
      if (d.length() >= 10) d = d.substring(5, 10);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(d, x - 2, 154, 1);
    }
  }
}

void drawSettingsScreen(bool full) {
  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("Settings");
  }

  tft.fillRect(10, 42, 300, 118, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Backlight", 14, 48, 2);

  tft.fillRoundRect(110, 44, 28, 24, 4, TFT_DARKGREY);
  tft.fillRoundRect(236, 44, 28, 24, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawCentreString("-", 124, 51, 2);
  tft.drawCentreString("+", 250, 51, 2);

  tft.fillRect(144, 50, 86, 8, TFT_DARKGREY);
  int w = map(backlightValue, 20, 255, 1, 86);
  tft.fillRect(144, 50, w, 8, TFT_GREEN);

  tft.fillRoundRect(10, 82, 140, 24, 5, wifiAutoConnect ? TFT_DARKGREEN : TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, wifiAutoConnect ? TFT_DARKGREEN : TFT_DARKGREY);
  tft.drawCentreString(wifiAutoConnect ? "AUTO: ON" : "AUTO: OFF", 80, 89, 2);

  tft.fillRoundRect(160, 82, 110, 24, 5, tft.color565(120,0,0));
  tft.setTextColor(TFT_WHITE, tft.color565(120,0,0));
  tft.drawCentreString("FORGET WIFI", 215, 89, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Default WiFi in file:", 12, 118, 1);
  String s = String(WIFI_DEFAULT_SSID);
  if (s.length() == 0) s = "(not set)";
  if (s.length() > 40) s = s.substring(0, 40);
  tft.drawString(s, 12, 132, 1);
}

void pushActivityValue(uint8_t value) {
  activityGraph[activityHead] = value;
  activityHead = (activityHead + 1) % 60;
}

void drawActivityScreen(bool full) {
  uint32_t now = millis();
  if (!full && now - lastActivityRefreshMs < 1000) return;

  uint32_t elapsed = now - lastActivityRefreshMs;
  fps = (elapsed > 0) ? (loopCounter * 1000UL) / elapsed : 0;
  loopCounter = 0;
  lastActivityRefreshMs = now;

  uint32_t freeKb = ESP.getFreeHeap() / 1024;
  uint8_t v = (freeKb > 200) ? 200 : (uint8_t)freeKb;
  pushActivityValue(v);

  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("Activity monitor");
  }

  tft.fillRect(10, 42, 300, 118, TFT_BLACK);
  tft.drawRect(12, 70, 296, 84, TFT_DARKGREY);

  char line[64];
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(line, sizeof(line), "Loop FPS: %lu", (unsigned long)fps);
  tft.drawString(line, 14, 46, 2);
  snprintf(line, sizeof(line), "Heap: %lu KB", (unsigned long)freeKb);
  tft.drawString(line, 180, 46, 2);

  for (uint8_t i = 0; i < 60; i++) {
    uint8_t idx = (activityHead + i) % 60;
    uint8_t h = map(activityGraph[idx], 0, 200, 2, 80);
    int16_t x = 16 + i * 4;
    int16_t y = 152 - h;
    uint16_t c = tft.color565(40 + h * 2, 180, 20);
    tft.drawFastVLine(x, y, h, c);
  }
}

void drawCurrentScreen(bool full) {
  switch (currentScreen) {
    case Screen::Menu: if (full) drawMenuScreen(); break;
    case Screen::Timer: drawTimerScreen(full); break;
    case Screen::Homer: if (full) drawHomerScreen(); break;
    case Screen::Animation: drawAnimationScreen(full); break;
    case Screen::SystemInfo: drawSystemInfoScreen(full); break;
    case Screen::WiFiManager: drawWiFiManagerScreen(full); break;
    case Screen::WiFiPassword: drawWiFiPasswordScreen(full); break;
    case Screen::Web: drawWebScreen(full); break;
    case Screen::Settings: drawSettingsScreen(full); break;
    case Screen::Activity: drawActivityScreen(full); break;
  }
}

void openSelectedMenu() {
  switch (selectedMenuIndex) {
    case 0: currentScreen = Screen::Timer; break;
    case 1: currentScreen = Screen::Homer; break;
    case 2: currentScreen = Screen::Animation; break;
    case 3: currentScreen = Screen::SystemInfo; break;
    case 4: currentScreen = Screen::WiFiManager; break;
    case 5: currentScreen = Screen::Web; break;
    case 6: currentScreen = Screen::Settings; break;
    case 7: currentScreen = Screen::Activity; break;
    default: currentScreen = Screen::Menu; break;
  }
  needFullRedraw = true;
}

void goBackToMenu() {
  currentScreen = Screen::Menu;
  needFullRedraw = true;
}

bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

void handleWiFiPasswordTap(int16_t x, int16_t y) {
  if (inRect(x, y, 10, 78, 48, 20)) {
    wifiKbShift = !wifiKbShift;
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 60, 78, 38, 20)) {
    wifiKbSymbols = !wifiKbSymbols;
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 100, 78, 52, 20)) {
    if (wifiInputPass.length() > 0) wifiInputPass.remove(wifiInputPass.length() - 1);
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 154, 78, 36, 20)) {
    wifiInputPass = "";
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 192, 78, 34, 20)) {
    connectToWiFi(wifiInputSsid, wifiInputPass, true);
    currentScreen = Screen::WiFiManager;
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 228, 78, 40, 20)) {
    currentScreen = Screen::WiFiManager;
    needFullRedraw = true;
    return;
  }

  if (y >= 102 && y <= 166 && x >= 10 && x <= 309) {
    uint8_t row = (y - 102) / 22;
    uint8_t col = (x - 10) / 30;
    if (row < 3 && col < 10 && wifiInputPass.length() < 63) {
      const char **rows = wifiKbSymbols ? kbRowsSym : kbRowsAlpha;
      char ch = rows[row][col];
      if (!wifiKbSymbols && wifiKbShift && ch >= 'a' && ch <= 'z') ch -= 32;
      wifiInputPass += ch;
      if (wifiKbShift) wifiKbShift = false;
      needFullRedraw = true;
    }
  }
}

void handleWiFiManagerTap(int16_t x, int16_t y) {
  if (inRect(x, y, 10, 44, 70, 22)) {
    scanWiFiNetworks();
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 88, 44, 70, 22)) {
    if (wifiSavedSsid.length() > 0) {
      connectToWiFi(wifiSavedSsid, wifiSavedPass, false);
    } else {
      wifiStatusText = "No saved WiFi";
    }
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 166, 44, 70, 22)) {
    prefsForgetWiFi();
    wifiStatusText = "Saved WiFi cleared";
    needFullRedraw = true;
    return;
  }

  for (uint8_t i = 0; i < wifiNetCount && i < 5; i++) {
    int16_t ry = 89 + i * 15;
    if (inRect(x, y, 10, ry, 300, 14)) {
      wifiSelected = i;
      wifiInputSsid = wifiNets[i].ssid;
      wifiInputPass = "";
      if (wifiNets[i].auth == WIFI_AUTH_OPEN) {
        connectToWiFi(wifiInputSsid, "", true);
      } else {
        currentScreen = Screen::WiFiPassword;
      }
      needFullRedraw = true;
      return;
    }
  }
}

void handleSettingsTap(int16_t x, int16_t y) {
  if (inRect(x, y, 110, 44, 28, 24)) {
    backlightValue -= 20;
    applyBacklight();
    prefsSaveSettings();
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 236, 44, 28, 24)) {
    backlightValue += 20;
    applyBacklight();
    prefsSaveSettings();
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 10, 82, 140, 24)) {
    wifiAutoConnect = !wifiAutoConnect;
    prefsSaveSettings();
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 160, 82, 110, 24)) {
    prefsForgetWiFi();
    wifiStatusText = "Saved WiFi cleared";
    needFullRedraw = true;
    return;
  }
}

void onTap(int16_t x, int16_t y) {
  if (currentScreen == Screen::Menu) {
    if (x > 20 && x < 300 && y > 40 && y < 132) openSelectedMenu();
    return;
  }

  if (isBackTap(x, y) && currentScreen != Screen::WiFiPassword) {
    goBackToMenu();
    return;
  }

  if (currentScreen == Screen::Timer) {
    if (inRect(x, y, 20, 108, 130, 42)) {
      if (!timerRunning) {
        timerRunning = true;
        timerStartMs = millis();
      } else {
        timerAccumulatedMs += millis() - timerStartMs;
        timerRunning = false;
      }
      drawTimerControls();
      lastTimerDrawMs = UINT32_MAX;
    } else if (inRect(x, y, 170, 108, 130, 42)) {
      timerRunning = false;
      timerAccumulatedMs = 0;
      lastTimerDrawMs = UINT32_MAX;
      drawTimerControls();
    }
    return;
  }

  if (currentScreen == Screen::WiFiManager) {
    handleWiFiManagerTap(x, y);
    return;
  }

  if (currentScreen == Screen::WiFiPassword) {
    if (isBackTap(x, y)) {
      currentScreen = Screen::WiFiManager;
      needFullRedraw = true;
      return;
    }
    handleWiFiPasswordTap(x, y);
    return;
  }

  if (currentScreen == Screen::Web) {
    if (inRect(x, y, 10, 44, 84, 24)) {
      fetchWeather();
      needFullRedraw = true;
      return;
    }
    if (inRect(x, y, 100, 44, 84, 24)) {
      fetchGitHub();
      needFullRedraw = true;
      return;
    }
    return;
  }

  if (currentScreen == Screen::Settings) {
    handleSettingsTap(x, y);
    return;
  }
}

void onSwipeLeft() {
  if (currentScreen != Screen::Menu) return;
  if (selectedMenuIndex + 1 < kMenuCount) {
    selectedMenuIndex++;
    needFullRedraw = true;
  }
}

void onSwipeRight() {
  if (currentScreen == Screen::Menu) {
    if (selectedMenuIndex > 0) {
      selectedMenuIndex--;
      needFullRedraw = true;
    }
    return;
  }
  if (currentScreen == Screen::WiFiPassword) {
    currentScreen = Screen::WiFiManager;
  } else {
    goBackToMenu();
  }
}

void processTouch() {
  bool hasTouch = false;
  int16_t x = 0;
  int16_t y = 0;

  if (touch.read() && touch.getPointNum() > 0) {
    TP_Point p = touch.getPoint(0);
    x = p.x;
    y = p.y;

    if (kTouchMirrorX) x = (kScreenW - 1) - x;
    if (kTouchMirrorY) y = (kScreenH - 1) - y;
    x = constrain(x, 0, kScreenW - 1);
    y = constrain(y, 0, kScreenH - 1);
    hasTouch = true;
  }

  if (hasTouch) {
    if (!touchState.active) {
      touchState.active = true;
      touchState.startX = x;
      touchState.startY = y;
      touchState.lastX = x;
      touchState.lastY = y;
      touchState.startMs = millis();
    } else {
      touchState.lastX = x;
      touchState.lastY = y;
    }
    return;
  }

  if (!touchState.active) return;

  int16_t dx = touchState.lastX - touchState.startX;
  int16_t dy = touchState.lastY - touchState.startY;
  uint32_t dt = millis() - touchState.startMs;

  bool horizontalSwipe = abs(dx) >= kSwipeThreshold && abs(dx) > abs(dy);
  bool isTap = abs(dx) < kTapMoveThreshold && abs(dy) < kTapMoveThreshold && dt <= kTapTimeMs;

  if (horizontalSwipe) {
    if (dx > 0) onSwipeRight();
    else onSwipeLeft();
  } else if (isTap) {
    onTap(touchState.lastX, touchState.lastY);
  }

  touchState.active = false;
}

void setup() {
  gpio_hold_dis((gpio_num_t)PIN_TOUCH_RES);

  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  pinMode(PIN_TOUCH_RES, OUTPUT);
  digitalWrite(PIN_TOUCH_RES, LOW);
  delay(120);
  digitalWrite(PIN_TOUCH_RES, HIGH);

  pinMode(PIN_LCD_BL, OUTPUT);
  analogWrite(PIN_LCD_BL, 255);

  Serial.begin(115200);
  Serial.println("LilyHammer boot");

  tft.begin();
#if defined(LCD_MODULE_CMD_1)
  for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++) {
    tft.writecommand(lcd_st7789v[i].cmd);
    for (uint8_t j = 0; j < (lcd_st7789v[i].len & 0x7F); j++) tft.writedata(lcd_st7789v[i].data[j]);
    if (lcd_st7789v[i].len & 0x80) delay(120);
  }
#endif
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
  if (!touch.init()) {
    tft.drawString("Touch init failed", 10, 10, 2);
    Serial.println("Touch IC not found");
  } else {
    touch.setRotation(1);
  }

  prefsLoad();
  applyBacklight();

  if (wifiAutoConnect) {
    if (wifiSavedSsid.length() > 0) {
      connectToWiFi(wifiSavedSsid, wifiSavedPass, false);
    } else if (String(WIFI_DEFAULT_SSID).length() > 0) {
      connectToWiFi(String(WIFI_DEFAULT_SSID), String(WIFI_DEFAULT_PASSWORD), false);
    }
  }

  scanWiFiNetworks();
  lastActivityRefreshMs = millis();
  drawCurrentScreen(true);
}

void loop() {
  loopCounter++;
  updateWiFiConnectionState();
  processTouch();

  if (needFullRedraw) {
    drawCurrentScreen(true);
    needFullRedraw = false;
  } else {
    drawCurrentScreen(false);
  }

  delay(5);
}

#if PIN_LCD_WR   != TFT_WR || \
    PIN_LCD_RD   != TFT_RD || \
    PIN_LCD_CS   != TFT_CS || \
    PIN_LCD_DC   != TFT_DC || \
    PIN_LCD_RES  != TFT_RST || \
    PIN_LCD_D0   != TFT_D0 || \
    PIN_LCD_D1   != TFT_D1 || \
    PIN_LCD_D2   != TFT_D2 || \
    PIN_LCD_D3   != TFT_D3 || \
    PIN_LCD_D4   != TFT_D4 || \
    PIN_LCD_D5   != TFT_D5 || \
    PIN_LCD_D6   != TFT_D6 || \
    PIN_LCD_D7   != TFT_D7 || \
    PIN_LCD_BL   != TFT_BL || \
    TFT_BACKLIGHT_ON != HIGH || \
    170 != TFT_WIDTH || \
    320 != TFT_HEIGHT
#error "Please select Setup206_LilyGo_T_Display_S3.h in TFT_eSPI/User_Setup_Select.h"
#endif

