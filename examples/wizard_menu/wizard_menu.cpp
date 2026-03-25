#include "Arduino.h"
#include "TFT_eSPI.h"
#include "TouchLib.h"
#include "Wire.h"
#include "WiFi.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/opt.h"
#include "lwip/netif.h"
#include "Preferences.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include "BLEDevice.h"
#include "LittleFS.h"
#include "AnimatedGIF.h"
#include "pin_config.h"
#include "apps/AppVersion.h"
#include "apps/AppRegistry.h"
#include "apps/AppProjects.h"

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
constexpr uint16_t kColorMenuBg = 0x2104;         // soft deep gray-blue
constexpr uint16_t kColorMenuHeaderBg = 0x74B8;   // pastel blue
constexpr uint16_t kColorMenuCardBg = 0x424D;     // muted slate-violet
constexpr uint16_t kColorMenuCardBorder = 0xBE79; // pastel mint border
constexpr uint16_t kColorMenuItemText = 0xFF9A;   // warm pastel yellow
constexpr uint16_t kColorMenuHintText = 0xC638;   // soft light gray
constexpr uint16_t kColorBackBtnBg = 0xA6DA;      // pastel mint
constexpr uint16_t kColorBackBtnText = 0x31A6;    // dark cyan-green
constexpr int16_t kMenuCardX = 10;
constexpr int16_t kMenuCardY = 16;
constexpr int16_t kMenuCardW = 300;
constexpr int16_t kMenuCardH = 78;

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
  BluetoothManager,
  WiFiPassword,
  Web,
  Projects,
  Settings,
  Activity,
  Fun
};

constexpr uint8_t kMenuCount = kAppRegistryCount;

const char *menuTitleByIndex(uint8_t idx) {
  if (idx >= kMenuCount) return "?";
  return kAppRegistry[idx].title;
}

AppId appByMenuIndex(uint8_t idx) {
  if (idx >= kMenuCount) return AppId::Timer;
  return kAppRegistry[idx].id;
}

struct ArkSpark;
struct ArkFireball;
struct BtDevice;

Screen currentScreen = Screen::Menu;
void drawTitle(const char *title, uint16_t bg);
void drawWebScreen(bool full);
void drawProjectsScreen(bool full);
void fetchProjectsFeed();
void projectsScrollBy(int8_t delta);
void drawBluetoothManagerScreen(bool full);
bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh);
String knownPasswordForSsid(const String &ssid);
void arkSpawnBonus(float x, float y);
void arkSpawnSparks(float x, float y, uint8_t count, float spread, float speed, bool sparksFromGround);
void arkDrawHud(bool full);
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
bool touchNeedsReinit = false;
uint32_t touchSkipReadUntilMs = 0;
uint32_t lastTouchPollMs = 0;

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
// Arkanoid (reuses Screen::Animation)
struct ArkBonus {
  bool active;
  float x;
  float y;
  uint8_t type; // 0=expand, 1=life, 2=slow, 3=fireball
};
struct ArkSpark {
  bool active;
  float x;
  float y;
  float vx;
  float vy;
  int16_t prevX;
  int16_t prevY;
  uint8_t life;
  uint8_t size;
  uint16_t color;
};
struct ArkFireball {
  bool active;
  int16_t x;
  int16_t y;
  int16_t prevX;
  int16_t prevY;
  int8_t vy;
};

constexpr uint8_t kArkCols = 10;
constexpr uint8_t kArkRowsMax = 7;
bool arkBlockAlive[kArkCols][kArkRowsMax] = {{false}};
uint8_t arkRows = 4;
int16_t arkBlockW = 14;
int16_t arkBlockH = 11;
int16_t arkBlockGap = 2;
int16_t arkBlocksX = 6;
int16_t arkBlocksY = 20;

int16_t arkPlayX = 4;
int16_t arkPlayY = 20;
int16_t arkPlayW = 162;
int16_t arkPlayH = 296;

float arkBallX = 85.0f;
float arkBallY = 170.0f;
float arkBallVx = 2.8f;
float arkBallVy = -3.0f;
int16_t arkBallR = 4;

int16_t arkPaddleX = 55;
int16_t arkPaddleY = 306;
int16_t arkPaddleW = 60;
int16_t arkPaddleH = 6;

int16_t arkPrevBallX = 85;
int16_t arkPrevBallY = 170;
int16_t arkPrevPaddleX = 55;
int16_t arkPrevPaddleW = 60;

uint32_t arkExitHoldMs = 0;
uint32_t arkPaddleBoostUntilMs = 0;
bool arkInitialized = false;
uint8_t arkLives = 3;
uint8_t arkLevel = 1;
uint32_t arkScore = 0;
char arkStatus[40] = "Move: BTN1/BTN2";
uint32_t arkStatusUntilMs = 0;
ArkBonus arkBonuses[4] = {{false,0,0,0},{false,0,0,0},{false,0,0,0},{false,0,0,0}};
constexpr uint8_t kArkSparkMax = 40;
ArkSpark arkSparks[kArkSparkMax] = {};
constexpr uint8_t kArkFireMax = 24;
ArkFireball arkFireballs[kArkFireMax] = {};
uint8_t arkFireLevel = 0;
uint32_t arkLastFireMs = 0;
float arkPaddlePreciseX = 55.0f;

// System
uint32_t lastSystemRefreshMs = 0;

// Activity
uint8_t activityGraph[60] = {0};
uint8_t activityHead = 0;
uint32_t lastActivityRefreshMs = 0;
uint32_t loopCounter = 0;
uint32_t fps = 0;
uint32_t lastUiFrameMs = 0;
float cpuLoadPercent = 0.0f;
uint32_t lastLoopTickMs = 0;

// WiFi manager
struct WiFiNet {
  String ssid;
  int32_t rssi;
  wifi_auth_mode_t auth;
  int32_t channel;
};
WiFiNet wifiNets[16];
uint8_t wifiNetCount = 0;
uint8_t wifiSelected = 0;
uint8_t wifiListOffset = 0;
String wifiSavedSsid;
String wifiSavedPass;
bool wifiAutoConnect = WIFI_DEFAULT_AUTO_CONNECT != 0;
String wifiStatusText = "Idle";
bool wifiConnecting = false;
uint32_t wifiConnectStartMs = 0;
uint32_t lastAutoWiFiAttemptMs = 0;
uint32_t lastWiFiUiTickMs = 0;
uint8_t wifiUiDots = 0;
wl_status_t lastWiFiStatusCode = WL_IDLE_STATUS;
String wifiConnectingSsid;
String wifiConnectingPass;
uint8_t wifiConnectRetryCount = 0;
bool wifiConnectHintValid = false;
String wifiConnectHintSsid;
int32_t wifiConnectHintChannel = 0;
uint8_t wifiLastDiscReason = 0;

struct WiFiTrafficStats {
  bool supported = false;
  bool appMode = false;
  bool ready = false;
  uint32_t lastRxBytesRaw = 0;
  uint32_t lastTxBytesRaw = 0;
  uint32_t lastRxPktsRaw = 0;
  uint32_t lastTxPktsRaw = 0;
  uint64_t totalRxBytes = 0;
  uint64_t totalTxBytes = 0;
  uint64_t totalRxPkts = 0;
  uint64_t totalTxPkts = 0;
  uint64_t baseRxBytes = 0;
  uint64_t baseTxBytes = 0;
  uint64_t baseRxPkts = 0;
  uint64_t baseTxPkts = 0;
};
WiFiTrafficStats wifiTraffic;
esp_netif_t *wifiStaNetif = nullptr;
uint32_t lastWiFiStatsPollMs = 0;
bool btn1PressedPrev = false;
bool btn2PressedPrev = false;
uint32_t buttonsDebounceMs = 0;

// Bluetooth manager (BLE)
struct BtDevice {
  String name;
  String addr;
  int rssi;
  String meta;
};
BtDevice btDevices[8];
uint8_t btDeviceCount = 0;
uint8_t btSelected = 0;
bool btInitialized = false;
String btStatusText = "Idle";
bool btScanInProgress = false;
bool btScanRequested = false;
uint32_t btScanStartMs = 0;
bool btInfoOpen = false;
uint8_t btInfoIndex = 0;

// Fun screen (GIF)
AnimatedGIF funGif;
bool funFsMounted = false;
bool funGifReady = false;
uint16_t funGifW = 220;
uint16_t funGifH = 124;
int16_t funGifX = 0;
int16_t funGifY = 0;
String funStatusText = "Tap to return";
String homerStatusText = "Tap to return";
TFT_eSprite menuSwipeSprite = TFT_eSprite(&tft);
bool menuSwipeSpriteReady = false;
uint8_t menuSwipeSpriteDepth = 16;

// WiFi password input
String wifiInputSsid;
String wifiInputPass;
bool wifiKbShift = false;
bool wifiKbSymbols = false;

// Web
String weatherText = "No data";
String githubText = "No data";
String githubRepo = "-";
String githubRangeStart = "--/--";
String githubRangeEnd = "--/--";
uint8_t githubDayCounts[7] = {0};
char githubDayLabels[7][6] = {{0}};
uint8_t githubHeat[371] = {0}; // 53 weeks * 7 days
uint8_t githubLevel[371] = {0}; // 0..4 from GitHub contributionLevel
bool weatherLoading = false;
bool githubLoading = false;
uint8_t weatherLoadingDots = 0;
uint8_t githubLoadingDots = 0;
uint8_t githubHeatMax = 1;
uint16_t githubCountBuf[400] = {0};
struct GitHubDayRec {
  int32_t jdn;
  uint16_t count;
  uint8_t level; // 0..4 (NONE..FOURTH_QUARTILE), 255 unknown
};
GitHubDayRec githubRecBuf[420];

// Projects feed
ProjectsFeedState projectsState;
uint8_t projectsListOffset = 0;
bool projectsHydrateActive = false;
uint8_t projectsHydrateCursor = 0;
uint8_t projectsHydrateTotal = 0;
uint8_t projectsHydrateDone = 0;

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

const char *wifiStatusName(wl_status_t s) {
  switch (s) {
    case WL_NO_SHIELD: return "NO_SHIELD";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "AUTH_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

uint64_t counterDelta32(uint32_t cur, uint32_t prev) {
  return (cur >= prev) ? (uint64_t)(cur - prev) : (uint64_t)cur + (1ULL << 32) - (uint64_t)prev;
}

String formatBytesHuman(uint64_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB"};
  double v = (double)bytes;
  uint8_t u = 0;
  while (v >= 1024.0 && u < 3) {
    v /= 1024.0;
    u++;
  }
  char buf[24];
  if (u == 0) snprintf(buf, sizeof(buf), "%llu %s", (unsigned long long)bytes, units[u]);
  else snprintf(buf, sizeof(buf), "%.2f %s", v, units[u]);
  return String(buf);
}

String formatCountHuman(uint64_t count) {
  char buf[24];
  if (count >= 1000000000ULL) snprintf(buf, sizeof(buf), "%.2fG", (double)count / 1000000000.0);
  else if (count >= 1000000ULL) snprintf(buf, sizeof(buf), "%.2fM", (double)count / 1000000.0);
  else if (count >= 1000ULL) snprintf(buf, sizeof(buf), "%.2fK", (double)count / 1000.0);
  else snprintf(buf, sizeof(buf), "%llu", (unsigned long long)count);
  return String(buf);
}

bool readWiFiTrafficRaw(uint32_t &rxBytes, uint32_t &txBytes, uint32_t &rxPkts, uint32_t &txPkts) {
#if MIB2_STATS
  if (!wifiStaNetif) wifiStaNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!wifiStaNetif) return false;
  void *impl = esp_netif_get_netif_impl(wifiStaNetif);
  if (!impl) return false;
  struct netif *nif = (struct netif *)impl;
  rxBytes = nif->mib2_counters.ifinoctets;
  txBytes = nif->mib2_counters.ifoutoctets;
  rxPkts = nif->mib2_counters.ifinucastpkts + nif->mib2_counters.ifinnucastpkts;
  txPkts = nif->mib2_counters.ifoutucastpkts + nif->mib2_counters.ifoutnucastpkts;
  return true;
#else
  (void)rxBytes; (void)txBytes; (void)rxPkts; (void)txPkts;
  return false;
#endif
}

void updateWiFiTrafficStats() {
  if (millis() - lastWiFiStatsPollMs < 500) return;
  lastWiFiStatsPollMs = millis();

  uint32_t rxB = 0, txB = 0, rxP = 0, txP = 0;
  if (!readWiFiTrafficRaw(rxB, txB, rxP, txP)) {
    wifiTraffic.appMode = true;
    wifiTraffic.supported = true;
    return;
  }
  wifiTraffic.supported = true;
  wifiTraffic.appMode = false;
  if (!wifiTraffic.ready) {
    wifiTraffic.lastRxBytesRaw = rxB;
    wifiTraffic.lastTxBytesRaw = txB;
    wifiTraffic.lastRxPktsRaw = rxP;
    wifiTraffic.lastTxPktsRaw = txP;
    wifiTraffic.ready = true;
    return;
  }

  wifiTraffic.totalRxBytes += counterDelta32(rxB, wifiTraffic.lastRxBytesRaw);
  wifiTraffic.totalTxBytes += counterDelta32(txB, wifiTraffic.lastTxBytesRaw);
  wifiTraffic.totalRxPkts += counterDelta32(rxP, wifiTraffic.lastRxPktsRaw);
  wifiTraffic.totalTxPkts += counterDelta32(txP, wifiTraffic.lastTxPktsRaw);

  wifiTraffic.lastRxBytesRaw = rxB;
  wifiTraffic.lastTxBytesRaw = txB;
  wifiTraffic.lastRxPktsRaw = rxP;
  wifiTraffic.lastTxPktsRaw = txP;
}

void addAppTraffic(uint32_t txBytes, uint32_t rxBytes) {
  if (txBytes == 0 && rxBytes == 0) return;
  // If raw netif counters are active, they already include app traffic.
  if (wifiTraffic.supported && !wifiTraffic.appMode) return;
  wifiTraffic.appMode = true;
  wifiTraffic.supported = true;
  wifiTraffic.totalTxBytes += txBytes;
  wifiTraffic.totalRxBytes += rxBytes;
  if (txBytes) wifiTraffic.totalTxPkts++;
  if (rxBytes) wifiTraffic.totalRxPkts++;
}
void resetWiFiTrafficStats() {
  updateWiFiTrafficStats();
  wifiTraffic.baseRxBytes = wifiTraffic.totalRxBytes;
  wifiTraffic.baseTxBytes = wifiTraffic.totalTxBytes;
  wifiTraffic.baseRxPkts = wifiTraffic.totalRxPkts;
  wifiTraffic.baseTxPkts = wifiTraffic.totalTxPkts;
}

void connectToWiFi(const String &ssid, const String &pass, bool saveCreds) {
  if (ssid.length() == 0) {
    wifiStatusText = "SSID is empty";
    return;
  }
  String passSan = pass;
  passSan.trim();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(btInitialized ? true : false);
  WiFi.disconnect(false, false);
  delay(120);
  if (wifiConnectHintValid && wifiConnectHintSsid == ssid && wifiConnectHintChannel > 0) {
    WiFi.begin(ssid.c_str(), passSan.c_str(), wifiConnectHintChannel, nullptr, true);
    Serial.printf("[WiFi] begin with channel hint=%ld\n", (long)wifiConnectHintChannel);
  } else {
    WiFi.begin(ssid.c_str(), passSan.c_str());
  }
  wifiConnecting = true;
  wifiConnectStartMs = millis();
  lastWiFiUiTickMs = 0;
  wifiUiDots = 0;
  lastWiFiStatusCode = WL_IDLE_STATUS;
  wifiStatusText = "Connecting to " + ssid;
  wifiConnectingSsid = ssid;
  wifiConnectingPass = passSan;
  wifiConnectRetryCount = 0;
  Serial.printf("[WiFi] connect start ssid='%s' pass_len=%u hint_ch=%ld\n", ssid.c_str(), (unsigned)passSan.length(), (long)(wifiConnectHintValid ? wifiConnectHintChannel : 0));
  if (saveCreds) {
    prefsSaveWiFi(ssid, passSan);
  }
}

void updateWiFiConnectionState() {
  if (!wifiConnecting) return;
  bool changed = false;
  wl_status_t st = WiFi.status();
  if (st != lastWiFiStatusCode) {
    lastWiFiStatusCode = st;
    Serial.printf("[WiFi] status=%s (%d)\n", wifiStatusName(st), (int)st);
  }
  // Fast recovery path for AUTH_FAILED (reason 202): retry without channel hint.
  if (st == WL_CONNECT_FAILED && wifiLastDiscReason == 202 && wifiConnectRetryCount < 2) {
    wifiConnectRetryCount++;
    wifiConnectHintValid = false;
    wifiStatusText = "Auth retry " + wifiConnectingSsid + " (" + String(wifiConnectRetryCount) + ")";
    Serial.printf("[WiFi] auth_failed -> retry %u (no hint)\n", (unsigned)wifiConnectRetryCount);
    WiFi.disconnect(false, false);
    delay(120);
    WiFi.begin(wifiConnectingSsid.c_str(), wifiConnectingPass.c_str());
    wifiConnectStartMs = millis();
    lastWiFiStatusCode = WL_IDLE_STATUS;
    if (currentScreen == Screen::WiFiManager) needFullRedraw = true;
    return;
  }
  if (st == WL_CONNECTED) {
    wifiConnecting = false;
    wifiStatusText = "Connected: " + WiFi.localIP().toString() + " RSSI " + String(WiFi.RSSI());
    Serial.printf("[WiFi] connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    changed = true;
  } else if (millis() - wifiConnectStartMs > 30000) {
    if (wifiConnectRetryCount < 1) {
      wifiConnectRetryCount++;
      wifiStatusText = "Retrying " + wifiConnectingSsid + " (" + String(wifiConnectRetryCount) + ")";
      Serial.printf("[WiFi] timeout -> retry %u ssid=%s status=%s (%d)\n", (unsigned)wifiConnectRetryCount, wifiConnectingSsid.c_str(), wifiStatusName(st), (int)st);
      WiFi.disconnect(false, false);
      delay(100);
      if (wifiConnectHintValid && wifiConnectHintSsid == wifiConnectingSsid && wifiConnectHintChannel > 0) WiFi.begin(wifiConnectingSsid.c_str(), wifiConnectingPass.c_str(), wifiConnectHintChannel, nullptr, true);
      else WiFi.begin(wifiConnectingSsid.c_str(), wifiConnectingPass.c_str());
      wifiConnectStartMs = millis();
      lastWiFiStatusCode = WL_IDLE_STATUS;
      changed = true;
      if (changed && currentScreen == Screen::WiFiManager) needFullRedraw = true;
      return;
    }
    wifiConnecting = false;
    wifiStatusText = String("Connect timeout (") + wifiStatusName(st) + ", r=" + String((unsigned)wifiLastDiscReason) + ")";
    Serial.printf("[WiFi] timeout final_status=%s (%d)\n", wifiStatusName(st), (int)st);
    WiFi.disconnect(false, false);
    changed = true;
  }
  if (changed && currentScreen == Screen::WiFiManager) {
    needFullRedraw = true;
  }
}

void scanWiFiNetworks() {
  wifiStatusText = "Scanning...";

  if (wifiConnecting) {
    wifiConnecting = false;
    WiFi.disconnect(false, false);
    delay(120);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(btInitialized ? true : false);
  WiFi.scanDelete();
  delay(80);

  int found = WiFi.scanNetworks();
  if (found < 0) {
    // Retry once after resetting STA state.
    Serial.printf("[WiFi] scan failed first try rc=%d, retrying...\n", found);
    WiFi.disconnect(false, false);
    delay(150);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(btInitialized ? true : false);
    found = WiFi.scanNetworks();
  }

  if (found < 0) {
    wifiStatusText = "Scan failed (" + String(found) + ")";
    Serial.printf("[WiFi] scan failed rc=%d\n", found);
    wifiNetCount = 0;
    return;
  }

  wifiNetCount = (found > 16) ? 16 : found;
  for (uint8_t i = 0; i < wifiNetCount; i++) {
    wifiNets[i].ssid = WiFi.SSID(i);
    wifiNets[i].rssi = WiFi.RSSI(i);
    wifiNets[i].auth = WiFi.encryptionType(i);
    wifiNets[i].channel = WiFi.channel(i);
  }
  wifiSelected = 0;
  wifiListOffset = 0;
  if (wifiNetCount == 0) wifiStatusText = "No networks";
  else wifiStatusText = "Found " + String(found) + " networks";
}
void ensureBluetoothInit() {
  if (btInitialized) return;
  BLEDevice::init("LilyHammer");
  btInitialized = true;
}

String buildBleMeta(BLEAdvertisedDevice &d) {
  String meta = "";
  if (d.haveServiceUUID()) {
    BLEUUID u = d.getServiceUUID();
    std::string us = u.toString();
    meta += "svc:" + String(us.c_str());
  } else {
    meta += "svc:-";
  }
  if (d.haveTXPower()) meta += " tx:" + String((int)d.getTXPower());
  else meta += " tx:-";
  if (d.haveManufacturerData()) meta += " mfg:" + String((int)d.getManufacturerData().length());
  else meta += " mfg:0";
  meta += " at:" + String((int)d.getAddressType());
  return meta;
}

String hex2(uint8_t v) {
  char b[3];
  snprintf(b, sizeof(b), "%02X", (unsigned)v);
  return String(b);
}

String hex4(uint16_t v) {
  char b[5];
  snprintf(b, sizeof(b), "%04X", (unsigned)v);
  return String(b);
}

String manufacturerDetails(BLEAdvertisedDevice &d) {
  if (!d.haveManufacturerData()) return "mfg:none";
  std::string m = d.getManufacturerData();
  if (m.length() < 2) return "mfg:short";

  uint16_t cid = ((uint8_t)m[1] << 8) | (uint8_t)m[0];
  String info = "cid:" + hex4(cid) + " len:" + String((int)m.length());
  if (m.length() >= 3) {
    uint8_t t0 = (uint8_t)m[2];
    info += " t0:" + hex2(t0);
    if (m.length() >= 4) {
      uint8_t t1 = (uint8_t)m[3];
      info += hex2(t1);
    }
  }
  return info;
}
String companyLabelFromId(uint16_t cid) {
  switch (cid) {
    case 0x004C: return "Apple BLE";
    case 0x00E0: return "Google BLE";
    case 0x0075: return "Samsung BLE";
    case 0x0006: return "Microsoft BLE";
    case 0x000F: return "Broadcom BLE";
    default: return "";
  }
}

String macVendorFromAddr(const String &addr) {
  if (addr.length() < 8) return "";
  String p = addr.substring(0, 8);
  p.toUpperCase();
  // Common prefixes seen in nearby consumer devices (not exhaustive).
  if (p == "AC:BC:32" || p == "A4:C3:61" || p == "28:CF:DA" || p == "34:AB:37") return "Apple";
  if (p == "F8:04:2E" || p == "38:AA:3C" || p == "20:C9:D0" || p == "3C:5A:B4") return "Samsung";
  if (p == "FC:C2:DE" || p == "7C:1E:52" || p == "00:15:5D") return "Microsoft";
  if (p == "A4:77:33" || p == "2C:54:CF") return "Xiaomi";
  return "";
}

String inferBleLabelFromAdvertised(BLEAdvertisedDevice &d) {
  if (d.haveName()) {
    String n = String(d.getName().c_str());
    n.trim();
    if (n.length() > 0) return n;
  }

  if (d.haveManufacturerData()) {
    std::string m = d.getManufacturerData();
    if (m.length() >= 2) {
      uint16_t cid = ((uint8_t)m[1] << 8) | (uint8_t)m[0];
      String label = companyLabelFromId(cid);
      if (label.length() > 0) return label;
    }
  }

  if (d.haveServiceUUID()) {
    std::string us = d.getServiceUUID().toString();
    String u = String(us.c_str());
    u.toLowerCase();
    if (u.indexOf("fe2c") >= 0) return "Tile BLE";
    if (u.indexOf("fd6f") >= 0) return "Google Nearby";
    if (u.indexOf("fe95") >= 0) return "Xiaomi BLE";
  }

  return "(unnamed)";
}
void fillBtDeviceFromAdvertised(BtDevice &dst, BLEAdvertisedDevice &d) {
  std::string name = d.getName();
  std::string addr = d.getAddress().toString();
  String inferred = inferBleLabelFromAdvertised(d);
  dst.addr = String(addr.c_str());
  if (inferred.length() == 0 || inferred == "(unnamed)") {
    String byMac = macVendorFromAddr(dst.addr);
    dst.name = byMac.length() ? (byMac + " (OUI)") : String("(unnamed)");
  } else {
    dst.name = inferred;
  }
  dst.rssi = d.getRSSI();
  dst.meta = buildBleMeta(d) + " " + manufacturerDetails(d);
}
void fillBluetoothDevicesFromResults(BLEScanResults &results, int found) {
  btDeviceCount = (found > 8) ? 8 : (uint8_t)max(found, 0);
  for (uint8_t i = 0; i < btDeviceCount; i++) {
    BLEAdvertisedDevice d = results.getDevice(i);
    fillBtDeviceFromAdvertised(btDevices[i], d);
  }
  if (btSelected >= btDeviceCount) btSelected = 0;
  btStatusText = (btDeviceCount == 0) ? String("No BLE devices") : ("Found " + String(found) + " BLE");
}

void updateBluetoothScanState() {
  // Synchronous scan is used to avoid unstable callback/task interactions.
}

void scanBluetoothDevices() {
  ensureBluetoothInit();
  if (btScanInProgress) return;

  btStatusText = "Scanning BLE (4x1s)...";
  btInfoOpen = false;
  btDeviceCount = 0;
  btScanInProgress = true;
  btScanStartMs = millis();
  needFullRedraw = true;
  drawBluetoothManagerScreen(false);
  delay(1);

  // Workaround for ESP32-S3 stability: temporarily disable Wi-Fi while BLE scan runs.
  wifi_mode_t wifiModeBefore = WiFi.getMode();
  bool restoreWiFi = (wifiModeBefore != WIFI_OFF);
  String resumeSsid = wifiSavedSsid.length() ? wifiSavedSsid : String(WIFI_DEFAULT_SSID);
  String resumePass = wifiSavedSsid.length() ? wifiSavedPass : String(WIFI_DEFAULT_PASSWORD);
  if (restoreWiFi) {
    WiFi.disconnect(false, false);
    delay(80);
    WiFi.mode(WIFI_OFF);
    delay(80);
  }

  BLEScan *scan = BLEDevice::getScan();
  scan->stop();
  scan->clearResults();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  BtDevice agg[16];
  uint8_t aggCount = 0;

  for (uint8_t pass = 0; pass < 4; pass++) {
    delay(0);
    BLEScanResults results = scan->start(1, false);
    int found = results.getCount();

    for (int i = 0; i < found; i++) {
      BLEAdvertisedDevice d = results.getDevice(i);
      std::string addrStd = d.getAddress().toString();
      String addr = String(addrStd.c_str());
      int idx = -1;
      for (uint8_t j = 0; j < aggCount; j++) {
        if (agg[j].addr == addr) { idx = j; break; }
      }

      BtDevice cur;
      fillBtDeviceFromAdvertised(cur, d);

      if (idx < 0) {
        if (aggCount < 16) {
          agg[aggCount++] = cur;
        }
      } else {
        // Keep non-empty name if it appears later in scan response.
        if (agg[idx].name == "(unnamed)" && cur.name != "(unnamed)") agg[idx].name = cur.name;
        if (cur.rssi > agg[idx].rssi) agg[idx].rssi = cur.rssi;
        if (cur.meta.length() > agg[idx].meta.length()) agg[idx].meta = cur.meta;
      }
    }

    btStatusText = "Scanning BLE " + String(pass + 1) + "/4";
    drawBluetoothManagerScreen(false);
    delay(1);
    scan->stop();
    scan->clearResults();
  }

  // Sort by RSSI descending (strongest first)
  for (uint8_t i = 0; i < aggCount; i++) {
    for (uint8_t j = i + 1; j < aggCount; j++) {
      if (agg[j].rssi > agg[i].rssi) {
        BtDevice t = agg[i];
        agg[i] = agg[j];
        agg[j] = t;
      }
    }
  }

  btDeviceCount = (aggCount > 8) ? 8 : aggCount;
  for (uint8_t i = 0; i < btDeviceCount; i++) btDevices[i] = agg[i];
  if (btSelected >= btDeviceCount) btSelected = 0;
  btStatusText = (aggCount == 0) ? String("No BLE devices") : ("Found " + String(aggCount) + " BLE adv");

  if (restoreWiFi) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(btInitialized ? true : false);
    if (resumeSsid.length() > 0) connectToWiFi(resumeSsid, resumePass, false);
  }

  btScanInProgress = false;
  touchNeedsReinit = true;
  touchSkipReadUntilMs = millis() + 250;
  needFullRedraw = true;
}
void resolveBluetoothDetails() {
  if (btScanInProgress) {
    btStatusText = "Wait: scanning BLE...";
    return;
  }
  if (btDeviceCount == 0) {
    btStatusText = "Scan devices first";
    return;
  }

  uint8_t idx = (btSelected < btDeviceCount) ? btSelected : 0;
  if (btDevices[idx].name != "(unnamed)") {
    for (uint8_t i = 0; i < btDeviceCount; i++) {
      if (btDevices[i].name == "(unnamed)") { idx = i; break; }
    }
  }

  btStatusText = "Details for " + String(idx + 1) + "/" + String(btDeviceCount);
  needFullRedraw = true;
  drawBluetoothManagerScreen(false);
  delay(1);

  wifi_mode_t wifiModeBefore = WiFi.getMode();
  bool restoreWiFi = (wifiModeBefore != WIFI_OFF);
  String resumeSsid = wifiSavedSsid.length() ? wifiSavedSsid : String(WIFI_DEFAULT_SSID);
  String resumePass = wifiSavedSsid.length() ? wifiSavedPass : String(WIFI_DEFAULT_PASSWORD);
  if (restoreWiFi) {
    WiFi.disconnect(false, false);
    delay(80);
    WiFi.mode(WIFI_OFF);
    delay(80);
  }

  uint8_t updated = 0;
  String targetAddr = btDevices[idx].addr;

  BLEAddress address(targetAddr.c_str());
  BLEClient *client = BLEDevice::createClient();
  bool ok = client->connect(address);
  if (ok) {
    String gattName = "";
    BLERemoteService *svc = client->getService(BLEUUID((uint16_t)0x1800));
    if (svc) {
      BLERemoteCharacteristic *ch = svc->getCharacteristic(BLEUUID((uint16_t)0x2A00));
      if (ch && ch->canRead()) {
        std::string val = ch->readValue();
        if (!val.empty()) gattName = String(val.c_str());
      }
    }

    if (gattName.length() > 0) {
      gattName.trim();
      if (gattName.length() > 0) {
        btDevices[idx].name = gattName;
        btDevices[idx].meta += " gatt-name";
        updated = 1;
      }
    }
    client->disconnect();
  }
  delete client;

  // Fallback: targeted active rescan can return scan-response name.
  if (updated == 0) {
    BLEScan *scan = BLEDevice::getScan();
    scan->stop();
    scan->clearResults();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    for (uint8_t pass = 0; pass < 2 && updated == 0; pass++) {
      BLEScanResults results = scan->start(1, false);
      int found = results.getCount();
      for (int i = 0; i < found; i++) {
        BLEAdvertisedDevice d = results.getDevice(i);
        String addr = String(d.getAddress().toString().c_str());
        if (addr == targetAddr) {
          String inferred = inferBleLabelFromAdvertised(d);
          if (inferred.length() > 0 && inferred != "(unnamed)") {
            btDevices[idx].name = inferred;
            btDevices[idx].meta = buildBleMeta(d) + " " + manufacturerDetails(d) + " adv-name";
            btDevices[idx].rssi = d.getRSSI();
            updated = 1;
            break;
          }
        }
      }
      scan->stop();
      scan->clearResults();
    }
  }

  if (restoreWiFi) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(btInitialized ? true : false);
    if (resumeSsid.length() > 0) connectToWiFi(resumeSsid, resumePass, false);
  }

  btSelected = idx;
  btStatusText = "Details done: +" + String(updated) + " name";
  touchNeedsReinit = true;
  touchSkipReadUntilMs = millis() + 250;
  needFullRedraw = true;
}
void connectBluetoothSelected() {
  if (btScanInProgress) {
    btStatusText = "Wait: scanning BLE...";
    return;
  }
  if (btDeviceCount == 0 || btSelected >= btDeviceCount) {
    btStatusText = "Select BLE device";
    return;
  }

  ensureBluetoothInit();
  btStatusText = "Connecting " + btDevices[btSelected].addr;

  BLEAddress address(btDevices[btSelected].addr.c_str());
  BLEClient *client = BLEDevice::createClient();
  bool ok = client->connect(address);
  if (ok) {
    btStatusText = "Connected(test): " + btDevices[btSelected].addr;
    client->disconnect();
  } else {
    btStatusText = "Connect failed";
  }
  delete client;
}

void drawBluetoothInfoPopup() {
  if (!btInfoOpen || btInfoIndex >= btDeviceCount) return;

  const int16_t px = 16;
  const int16_t py = 18;
  const int16_t pw = 288;
  const int16_t ph = 132;

  const BtDevice &d = btDevices[btInfoIndex];

  tft.fillRoundRect(px, py, pw, ph, 8, TFT_DARKGREY);
  tft.drawRoundRect(px, py, pw, ph, 8, TFT_WHITE);

  tft.fillRect(px + 2, py + 2, pw - 4, 18, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("BT Device", px + 8, py + 4, 2);

  tft.fillRoundRect(px + pw - 24, py + 4, 18, 14, 3, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawCentreString("X", px + pw - 15, py + 7, 1);

  tft.setTextColor(TFT_CYAN, TFT_DARKGREY);
  String line1 = "Name: " + d.name;
  if (line1.length() > 44) line1 = line1.substring(0, 44);
  tft.drawString(line1, px + 8, py + 26, 1);

  String line2 = "Addr: " + d.addr;
  if (line2.length() > 44) line2 = line2.substring(0, 44);
  tft.drawString(line2, px + 8, py + 38, 1);

  String line3 = "RSSI: " + String(d.rssi) + " dBm";
  tft.drawString(line3, px + 8, py + 50, 1);

  tft.setTextColor(TFT_YELLOW, TFT_DARKGREY);
  tft.drawString("Meta:", px + 8, py + 62, 1);

  String rem = d.meta;
  int16_t y = py + 74;
  while (rem.length() > 0 && y < (py + ph - 10)) {
    int cut = (rem.length() > 44) ? 44 : rem.length();
    if (cut < rem.length()) {
      int sp = rem.lastIndexOf(' ', cut);
      if (sp > 8) cut = sp;
    }
    String l = rem.substring(0, cut);
    tft.drawString(l, px + 8, y, 1);
    if (cut >= rem.length()) {
      rem = "";
    } else {
      rem = rem.substring(cut);
      rem.trim();
    }
    y += 10;
  }
}
void drawBluetoothManagerScreen(bool full) {
  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    drawTitle("Bluetooth", TFT_NAVY);

    tft.fillRect(4, 4, 312, 162, TFT_BLACK);

    tft.fillRoundRect(10, 10, 70, 22, 5, TFT_DARKCYAN);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.drawCentreString("SCAN", 45, 14, 2);

    tft.fillRoundRect(88, 10, 96, 22, 5, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.drawCentreString("DETAILS", 136, 14, 2);

    tft.fillRoundRect(190, 10, 70, 22, 5, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawCentreString("CLEAR", 225, 14, 2);

    for (uint8_t i = 0; i < btDeviceCount && i < 8; i++) {
      int16_t y = 62 + i * 12;
      uint16_t bg = (i == btSelected) ? TFT_NAVY : TFT_BLACK;
      tft.fillRect(6, y - 1, 308, 12, bg);
      tft.setTextColor(TFT_WHITE, bg);
      String s = String(i + 1) + ". " + btDevices[i].name + "  RSSI " + String(btDevices[i].rssi) + " dBm";
      if (s.length() > 52) s = s.substring(0, 52);
      tft.drawString(s, 8, y, 1);
    }
  }

  String st1 = btStatusText;
  if (st1.length() > 48) st1 = st1.substring(0, 48);
  String st2 = "";
  if (btDeviceCount > 0 && btSelected < btDeviceCount) {
    st2 = btDevices[btSelected].addr + " " + btDevices[btSelected].meta;
    if (st2.length() > 56) st2 = st2.substring(0, 56);
  }
  tft.fillRect(6, 34, 308, 24, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(st1, 8, 35, 1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(st2, 8, 47, 1);

  if (btInfoOpen && btInfoIndex >= btDeviceCount) btInfoOpen = false;
  drawBluetoothInfoPopup();
}

void handleBluetoothTap(int16_t x, int16_t y) {
  if (btInfoOpen) {
    (void)x;
    (void)y;
    btInfoOpen = false;
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 10, 10, 70, 22)) {
    if (!btScanInProgress) {
      btScanRequested = true;
      btStatusText = "Scan requested...";
      touchState.active = false;
    }
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 88, 10, 96, 22)) {
    resolveBluetoothDetails();
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 190, 10, 70, 22)) {
    btDeviceCount = 0;
    btInfoOpen = false;
    btStatusText = "Cleared";
    needFullRedraw = true;
    return;
  }

  for (uint8_t i = 0; i < btDeviceCount && i < 8; i++) {
    int16_t ry = 61 + i * 12;
    if (inRect(x, y, 6, ry, 308, 12)) {
      btSelected = i;
      btInfoIndex = i;
      btInfoOpen = true;
      btStatusText = "Selected: " + btDevices[i].addr;
      needFullRedraw = true;
      return;
    }
  }
}

void *funGifOpenFile(const char *fname, int32_t *pSize) {
  fs::File *f = new fs::File(LittleFS.open(fname, FILE_READ));
  if (!(*f)) {
    delete f;
    return nullptr;
  }
  *pSize = (int32_t)f->size();
  return (void *)f;
}

void funGifCloseFile(void *pHandle) {
  fs::File *f = static_cast<fs::File *>(pHandle);
  if (!f) return;
  f->close();
  delete f;
}

int32_t funGifReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t len) {
  fs::File *f = static_cast<fs::File *>(pFile->fHandle);
  if (!f) return 0;
  int32_t remain = pFile->iSize - pFile->iPos;
  if (remain <= 0) return 0;
  if (len > remain) len = remain;
  int32_t rd = (int32_t)f->read(pBuf, len);
  pFile->iPos += rd;
  return rd;
}

int32_t funGifSeekFile(GIFFILE *pFile, int32_t pos) {
  fs::File *f = static_cast<fs::File *>(pFile->fHandle);
  if (!f) return 0;
  f->seek(pos);
  pFile->iPos = pos;
  return pos;
}

void funGifDraw(GIFDRAW *pDraw) {
  if (funGifW == 0 || funGifH == 0) return;

  int srcY = pDraw->iY + pDraw->y;
  if (srcY < 0 || srcY >= funGifH) return;

  int dstY0 = (srcY * kScreenH) / funGifH;
  int dstY1 = ((srcY + 1) * kScreenH) / funGifH;
  if (dstY1 <= dstY0) dstY1 = dstY0 + 1;
  if (dstY0 < 0) dstY0 = 0;
  if (dstY1 > kScreenH) dstY1 = kScreenH;

  int srcX0 = pDraw->iX;
  int srcX1 = pDraw->iX + pDraw->iWidth;
  int dstX0 = (srcX0 * kScreenW) / funGifW;
  int dstX1 = ((srcX1 * kScreenW) + funGifW - 1) / funGifW;
  if (dstX0 < 0) dstX0 = 0;
  if (dstX1 > kScreenW) dstX1 = kScreenW;
  if (dstX1 <= dstX0) return;

  uint16_t lineBuf[320];
  uint8_t *pix = pDraw->pPixels;

  for (int dy = dstY0; dy < dstY1; dy++) {
    if (!pDraw->ucHasTransparency) {
      int n = 0;
      for (int dx = dstX0; dx < dstX1 && n < 320; dx++, n++) {
        int sx = (dx * funGifW) / kScreenW;
        int local = sx - pDraw->iX;
        if (local < 0) local = 0;
        if (local >= pDraw->iWidth) local = pDraw->iWidth - 1;
        lineBuf[n] = pDraw->pPalette[pix[local]];
      }
      tft.pushImage(funGifX + dstX0, funGifY + dy, n, 1, lineBuf);
    } else {
      int runStart = -1;
      int runLen = 0;
      for (int dx = dstX0; dx < dstX1; dx++) {
        int sx = (dx * funGifW) / kScreenW;
        int local = sx - pDraw->iX;
        if (local < 0 || local >= pDraw->iWidth) {
          if (runLen > 0) {
            tft.pushImage(funGifX + runStart, funGifY + dy, runLen, 1, lineBuf);
            runLen = 0;
            runStart = -1;
          }
          continue;
        }
        uint8_t idx = pix[local];
        if (idx == pDraw->ucTransparent) {
          if (runLen > 0) {
            tft.pushImage(funGifX + runStart, funGifY + dy, runLen, 1, lineBuf);
            runLen = 0;
            runStart = -1;
          }
          continue;
        }
        if (runStart < 0) runStart = dx;
        if (runLen < 320) lineBuf[runLen++] = pDraw->pPalette[idx];
      }
      if (runLen > 0) {
        tft.pushImage(funGifX + runStart, funGifY + dy, runLen, 1, lineBuf);
      }
    }
  }
}

bool openGifByPath(const char *path, String &statusText) {
  if (!funFsMounted) {
    funFsMounted = LittleFS.begin(true);
    if (!funFsMounted) {
      statusText = "LittleFS mount failed";
      return false;
    }
  }

  uint16_t gw = 220, gh = 124;
  fs::File fh = LittleFS.open(path, FILE_READ);
  if (fh) {
    uint8_t hdr[10];
    if (fh.read(hdr, sizeof(hdr)) == sizeof(hdr)) {
      gw = (uint16_t)(hdr[6] | (hdr[7] << 8));
      gh = (uint16_t)(hdr[8] | (hdr[9] << 8));
    }
    fh.close();
  }

  funGifW = gw;
  funGifH = gh;
  funGifX = 0;
  funGifY = 0;

  if (!funGif.open(path, funGifOpenFile, funGifCloseFile, funGifReadFile, funGifSeekFile, funGifDraw)) {
    statusText = "GIF open failed";
    return false;
  }

  statusText = "Tap anywhere to exit";
  funGifReady = true;
  return true;
}

bool openFunGif() {
  return openGifByPath("/images/this-is-fine.gif", funStatusText);
}

bool openHomerGif() {
  return openGifByPath("/images/homer.gif", homerStatusText);
}

void closeFunGif() {
  if (funGifReady) {
    funGif.close();
    funGifReady = false;
  }
}

void drawFunScreen(bool full) {
  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    closeFunGif();
    if (!openFunGif()) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString(funStatusText, 8, 76, 2);
      return;
    }
  }

  if (!funGifReady) return;
  tft.setSwapBytes(true);
  bool frameOk = funGif.playFrame(true, nullptr);
  tft.setSwapBytes(false);
  if (!frameOk) {
    closeFunGif();
    openFunGif();
  }
}

void drawHomerScreen(bool full) {
  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    closeFunGif();
    if (!openHomerGif()) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString(homerStatusText, 8, 76, 2);
      return;
    }
  }

  if (!funGifReady) return;
  tft.setSwapBytes(true);
  bool frameOk = funGif.playFrame(true, nullptr);
  tft.setSwapBytes(false);
  if (!frameOk) {
    closeFunGif();
    openHomerGif();
  }
}
bool isBackTap(int16_t x, int16_t y) {
  (void)x;
  (void)y;
  return false;
}

void drawBackButton() {
  return;
  tft.fillRoundRect(6, 6, kBackBtnW, kBackBtnH, 6, kColorBackBtnBg);
  tft.drawRoundRect(6, 6, kBackBtnW, kBackBtnH, 6, TFT_WHITE);
  tft.setTextColor(kColorBackBtnText, kColorBackBtnBg);
  tft.drawString("< BACK", 12, 12, 2);
}

void drawTitle(const char *title, uint16_t bg = TFT_NAVY) {
  (void)title;
  (void)bg;
  return;
  tft.fillRect(0, 0, kScreenW, 36, bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.drawString(title, 78, 10, 2);
  drawBackButton();
}

void drawMenuHeader() {
  return;
  tft.fillRect(0, 0, kScreenW, 30, kColorMenuHeaderBg);
  tft.setTextColor(TFT_WHITE, kColorMenuHeaderBg);
  char title[40];
  snprintf(title, sizeof(title), "LilyHammer Menu: %u/%u", (unsigned)(selectedMenuIndex + 1), (unsigned)kMenuCount);
  tft.drawCentreString(title, kScreenW / 2, 7, 2);
}
void drawMenuScreen() {
  if (tft.getRotation() != 3) tft.setRotation(3);
  tft.fillScreen(kColorMenuBg);
  drawMenuHeader();

  tft.fillRoundRect(kMenuCardX, kMenuCardY, kMenuCardW, kMenuCardH, 12, kColorMenuCardBg);
  tft.drawRoundRect(kMenuCardX, kMenuCardY, kMenuCardW, kMenuCardH, 12, kColorMenuCardBorder);
  tft.setTextColor(kColorMenuItemText, kColorMenuCardBg);
  tft.drawCentreString(menuTitleByIndex(selectedMenuIndex), kScreenW / 2, 44, 4);
  if (appByMenuIndex(selectedMenuIndex) == AppId::Projects && (projectsState.loading || projectsHydrateActive)) {
    tft.setTextColor(TFT_WHITE, kColorMenuCardBg);
    tft.drawCentreString("Loading...", kScreenW / 2, 82, 2);
  }

  tft.setTextColor(kColorMenuHintText, kColorMenuBg);
  tft.drawCentreString("Swipe left/right to switch", kScreenW / 2, 128, 2);
  tft.drawCentreString("Tap center to open", kScreenW / 2, 144, 2);
}


void drawMenuCardItemAt(int16_t cx, const char *label) {
  int16_t x = cx - (kMenuCardW / 2);
  tft.fillRoundRect(x, kMenuCardY, kMenuCardW, kMenuCardH, 12, kColorMenuCardBg);
  tft.drawRoundRect(x, kMenuCardY, kMenuCardW, kMenuCardH, 12, kColorMenuCardBorder);
  tft.setTextColor(kColorMenuItemText, kColorMenuCardBg);
  tft.drawCentreString(label, cx, 44, 4);
  if (strcmp(label, "Projects") == 0 && (projectsState.loading || projectsHydrateActive)) {
    tft.setTextColor(TFT_WHITE, kColorMenuCardBg);
    tft.drawCentreString("Loading...", cx, 82, 2);
  }
}

void drawMenuCardItemInSprite(TFT_eSprite &spr, int16_t cx, const char *label) {
  int16_t x = cx - (kMenuCardW / 2);
  spr.fillRoundRect(x, 0, kMenuCardW, kMenuCardH, 12, kColorMenuCardBg);
  spr.drawRoundRect(x, 0, kMenuCardW, kMenuCardH, 12, kColorMenuCardBorder);
  spr.setTextColor(kColorMenuItemText, kColorMenuCardBg);
  spr.drawCentreString(label, cx, 28, 4);
  if (strcmp(label, "Projects") == 0 && (projectsState.loading || projectsHydrateActive)) {
    spr.setTextColor(TFT_WHITE, kColorMenuCardBg);
    spr.drawCentreString("Loading...", cx, 56, 2);
  }
}

void animateMenuSwipe(uint8_t fromIdx, uint8_t toIdx, bool moveLeft) {
  if (!menuSwipeSpriteReady) {
    // Without sprite buffering ST7789 visibly flickers; do a clean instant switch.
    tft.fillRect(0, kMenuCardY, kScreenW, kMenuCardH, kColorMenuBg);
    drawMenuCardItemAt(kScreenW / 2, menuTitleByIndex(toIdx));
    return;
  }
  if (fromIdx >= kMenuCount || toIdx >= kMenuCount || fromIdx == toIdx) return;

  const int16_t startX = kScreenW / 2;
  const int16_t endOffset = kScreenW;
  const uint8_t frames = 5; // Fast to reduce tearing perception.
  int16_t lastD = -1;

  for (uint8_t i = 0; i <= frames; i++) {
    int32_t q = ((int32_t)i * 1024) / frames;
    int32_t inv = 1024 - q;
    int32_t easedQ = 1024 - (inv * inv * inv) / (1024 * 1024); // ease-out cubic
    int16_t d = (int16_t)((endOffset * easedQ) / 1024);
    if (d == lastD) continue;
    lastD = d;

    int16_t outX = moveLeft ? (startX - d) : (startX + d);
    int16_t inX = moveLeft ? (startX + (endOffset - d)) : (startX - (endOffset - d));

    menuSwipeSprite.fillRect(0, 0, kScreenW, kMenuCardH, kColorMenuBg);
    drawMenuCardItemInSprite(menuSwipeSprite, outX, menuTitleByIndex(fromIdx));
    drawMenuCardItemInSprite(menuSwipeSprite, inX, menuTitleByIndex(toIdx));
    menuSwipeSprite.pushSprite(0, kMenuCardY);
    delay(1);
  }

  menuSwipeSprite.fillRect(0, 0, kScreenW, kMenuCardH, kColorMenuBg);
  drawMenuCardItemInSprite(menuSwipeSprite, startX, menuTitleByIndex(toIdx));
  menuSwipeSprite.pushSprite(0, kMenuCardY);
}
void drawTimerControls() {
  tft.fillRoundRect(20, 108, 130, 42, 8, timerRunning ? TFT_ORANGE : TFT_GREEN);
  tft.setTextColor(TFT_BLACK, timerRunning ? TFT_ORANGE : TFT_GREEN);
  tft.drawCentreString(timerRunning ? "PAUSE" : "START", 85, 121, 2);

  tft.fillRoundRect(170, 108, 130, 42, 8, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawCentreString("RESET", 235, 121, 2);
}

void drawTimerScreen(bool full) {
  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    drawTitle("Timer");
    tft.fillRect(20, 10, 280, 54, TFT_BLACK);
    tft.drawRect(20, 10, 280, 54, TFT_DARKGREY);
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
  // Update only inner area to avoid visible tearing on the frame border.
  tft.fillRect(22, 12, 276, 50, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString(buf, kScreenW / 2, 20, 4);
  lastTimerDrawMs = elapsed;
}

void arkResetBonuses() {
  for (uint8_t i = 0; i < 4; i++) arkBonuses[i].active = false;
}

uint16_t arkBlockColor(uint8_t c, uint8_t r) {
  return tft.color565(40 + r * 22, 120 + c * 7, 180 - r * 20);
}

void arkRestorePixel(int16_t x, int16_t y) {
  if (x < arkPlayX + 1 || x > arkPlayX + arkPlayW - 2 || y < arkPlayY + 1 || y > arkPlayY + arkPlayH - 2) return;
  if (y >= arkPaddleY && y < arkPaddleY + arkPaddleH && x >= arkPaddleX && x < arkPaddleX + arkPaddleW) {
    tft.drawPixel(x, y, TFT_WHITE);
    return;
  }
  for (uint8_t c = 0; c < kArkCols; c++) {
    for (uint8_t r = 0; r < arkRows; r++) {
      if (!arkBlockAlive[c][r]) continue;
      int16_t bx = arkBlocksX + c * (arkBlockW + arkBlockGap);
      int16_t by = arkBlocksY + r * (arkBlockH + arkBlockGap);
      if (x >= bx && x < bx + arkBlockW && y >= by && y < by + arkBlockH) {
        tft.drawPixel(x, y, arkBlockColor(c, r));
        return;
      }
    }
  }
  tft.drawPixel(x, y, TFT_BLACK);
}

void arkRestoreSpark(const ArkSpark &sp) {
  if (sp.prevX < 0 || sp.prevY < 0) return;
  for (int16_t dy = 0; dy < sp.size; dy++) {
    for (int16_t dx = 0; dx < sp.size; dx++) {
      arkRestorePixel(sp.prevX + dx, sp.prevY + dy);
    }
  }
}

void arkResetSparks() {
  for (uint8_t i = 0; i < kArkSparkMax; i++) {
    arkSparks[i].active = false;
    arkSparks[i].prevX = -1;
    arkSparks[i].prevY = -1;
  }
}

void arkResetFireballs() {
  for (uint8_t i = 0; i < kArkFireMax; i++) {
    arkFireballs[i].active = false;
    arkFireballs[i].prevX = -1;
    arkFireballs[i].prevY = -1;
  }
}

void arkRestoreFireball(const ArkFireball &fb) {
  if (fb.prevX < 0 || fb.prevY < 0) return;
  for (int16_t dy = 0; dy < 6; dy++) {
    for (int16_t dx = 0; dx < 2; dx++) {
      arkRestorePixel(fb.prevX + dx, fb.prevY + dy);
    }
  }
}

void arkSpawnFireball(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < kArkFireMax; i++) {
    if (arkFireballs[i].active) continue;
    arkFireballs[i].active = true;
    arkFireballs[i].x = x;
    arkFireballs[i].y = y;
    arkFireballs[i].prevX = -1;
    arkFireballs[i].prevY = -1;
    arkFireballs[i].vy = -6;
    break;
  }
}

void arkTryShoot(uint32_t now) {
  if (arkFireLevel == 0) return;
  if (now - arkLastFireMs < 720) return;
  arkLastFireMs = now;

  int16_t y = arkPaddleY - 7;
  if (arkFireLevel >= 1) {
    arkSpawnFireball(arkPaddleX + arkPaddleW - 3, y);
    arkSpawnSparks((float)(arkPaddleX + arkPaddleW - 3), (float)(arkPaddleY - 1), 3, 0.35f, 1.2f, true);
  }
  if (arkFireLevel >= 2) {
    arkSpawnFireball(arkPaddleX + 1, y);
    arkSpawnSparks((float)(arkPaddleX + 1), (float)(arkPaddleY - 1), 3, 0.35f, 1.2f, true);
  }
  if (arkFireLevel >= 3) {
    arkSpawnFireball(arkPaddleX + arkPaddleW / 2 - 1, y);
    arkSpawnSparks((float)(arkPaddleX + arkPaddleW / 2), (float)(arkPaddleY - 1), 4, 0.35f, 1.2f, true);
  }
}

void arkUpdateFireballs() {
  for (uint8_t i = 0; i < kArkFireMax; i++) {
    ArkFireball &fb = arkFireballs[i];
    if (!fb.active) continue;

    arkRestoreFireball(fb);
    fb.y += fb.vy;

    if (fb.y < arkPlayY + 2) {
      fb.active = false;
      fb.prevX = -1;
      fb.prevY = -1;
      continue;
    }

    bool hit = false;
    for (uint8_t c = 0; c < kArkCols && !hit; c++) {
      for (uint8_t r = 0; r < arkRows && !hit; r++) {
        if (!arkBlockAlive[c][r]) continue;
        int16_t bx = arkBlocksX + c * (arkBlockW + arkBlockGap);
        int16_t by = arkBlocksY + r * (arkBlockH + arkBlockGap);
        if (fb.x + 1 < bx || fb.x > bx + arkBlockW || fb.y + 5 < by || fb.y > by + arkBlockH) continue;

        arkBlockAlive[c][r] = false;
        arkScore += 10U * arkLevel;
        tft.fillRect(bx, by, arkBlockW, arkBlockH, TFT_BLACK);
        arkSpawnSparks((float)(bx + arkBlockW / 2), (float)(by + arkBlockH / 2), 16, 0.8f, 3.0f, false);
        arkSpawnBonus((float)(bx + arkBlockW / 2), (float)(by + arkBlockH / 2));
        arkDrawHud(false);

        fb.active = false;
        fb.prevX = -1;
        fb.prevY = -1;
        hit = true;
      }
    }
    if (hit) continue;

    fb.prevX = fb.x;
    fb.prevY = fb.y;
    tft.fillRect(fb.x, fb.y, 2, 5, TFT_ORANGE);
    tft.fillRect(fb.x, fb.y + 5, 2, 1, TFT_YELLOW);
  }
}

void arkSpawnSparks(float x, float y, uint8_t count, float speedMin, float speedMax, bool upwardBias) {
  static const uint16_t palette[] = {TFT_WHITE, TFT_YELLOW, TFT_ORANGE, TFT_CYAN};
  for (uint8_t n = 0; n < count; n++) {
    uint8_t slot = kArkSparkMax;
    for (uint8_t i = 0; i < kArkSparkMax; i++) {
      if (!arkSparks[i].active) { slot = i; break; }
    }
    if (slot >= kArkSparkMax) break;

    ArkSpark &sp = arkSparks[slot];
    sp.active = true;
    sp.x = x;
    sp.y = y;
    float a = upwardBias ? radians((float)random(-150, -29)) : radians((float)random(0, 360));
    float v = speedMin + ((float)random(0, 1000) / 999.0f) * (speedMax - speedMin);
    sp.vx = cosf(a) * v;
    sp.vy = sinf(a) * v;
    sp.prevX = -1;
    sp.prevY = -1;
    sp.life = (uint8_t)random(6, 12);
    sp.size = (random(0, 100) < 75) ? 1 : 2;
    sp.color = palette[random(0, (int)(sizeof(palette) / sizeof(palette[0])))];
  }
}

void arkUpdateSparks() {
  for (uint8_t i = 0; i < kArkSparkMax; i++) {
    ArkSpark &sp = arkSparks[i];
    if (!sp.active) continue;

    arkRestoreSpark(sp);

    sp.x += sp.vx;
    sp.y += sp.vy;
    sp.vy += 0.08f;
    sp.vx *= 0.98f;
    if (sp.life > 0) sp.life--;

    int16_t x = (int16_t)sp.x;
    int16_t y = (int16_t)sp.y;
    if (sp.life == 0 || x < arkPlayX + 1 || x > arkPlayX + arkPlayW - 2 || y < arkPlayY + 1 || y > arkPlayY + arkPlayH - 2) {
      sp.active = false;
      sp.prevX = -1;
      sp.prevY = -1;
      continue;
    }

    int16_t maxX = arkPlayX + arkPlayW - 2;
    int16_t maxY = arkPlayY + arkPlayH - 2;
    int16_t drawW = min<int16_t>(sp.size, maxX - x + 1);
    int16_t drawH = min<int16_t>(sp.size, maxY - y + 1);
    if (drawW <= 0 || drawH <= 0) {
      sp.active = false;
      sp.prevX = -1;
      sp.prevY = -1;
      continue;
    }

    sp.prevX = x;
    sp.prevY = y;
    tft.fillRect(x, y, drawW, drawH, sp.color);
  }
}
void arkSetStatus(const char *text, uint32_t ttlMs = 2000) {
  snprintf(arkStatus, sizeof(arkStatus), "%s", text);
  arkStatusUntilMs = (ttlMs > 0) ? (millis() + ttlMs) : 0;
}

void arkBuildLevel() {
  arkRows = (uint8_t)min((int)kArkRowsMax, 3 + (int)arkLevel);
  for (uint8_t c = 0; c < kArkCols; c++) {
    for (uint8_t r = 0; r < kArkRowsMax; r++) arkBlockAlive[c][r] = (r < arkRows);
  }
}

void arkRespawnBall() {
  arkBallX = arkPaddleX + arkPaddleW * 0.5f;
  arkBallY = arkPaddleY - arkBallR - 1;
  float s = 2.8f + 0.35f * (float)arkLevel;
  arkBallVx = (random(0, 2) == 0) ? s : -s;
  arkBallVy = -s;
}

void arkApplyBonus(uint8_t type) {
  if (type == 0) {
    arkPaddleW = min<int16_t>(92, arkPaddleW + 14);
    arkPaddleBoostUntilMs = millis() + 12000;
    arkSetStatus("Bonus: wider paddle");
  } else if (type == 1) {
    arkLives = min<uint8_t>(9, arkLives + 1);
    arkSetStatus("Bonus: +1 life");
  } else if (type == 2) {
    arkBallVx *= 0.75f;
    arkBallVy *= 0.75f;
    arkSetStatus("Bonus: slow ball");
  } else {
    if (arkFireLevel < 3) {
      arkFireLevel++;
      if (arkFireLevel == 1) arkSetStatus("Fire: right cannon");
      else if (arkFireLevel == 2) arkSetStatus("Fire: +left cannon");
      else arkSetStatus("Fire: +center cannon");
    } else {
      arkSetStatus("Fire cannons: MAX");
    }
  }
}

void arkSpawnBonus(float x, float y) {
  if (random(0, 100) >= 22) return;
  for (uint8_t i = 0; i < 4; i++) {
    if (!arkBonuses[i].active) {
      arkBonuses[i].active = true;
      arkBonuses[i].x = x;
      arkBonuses[i].y = y;
      arkBonuses[i].type = (uint8_t)random(0, 4);
      break;
    }
  }
}

void arkDrawHud(bool full) {
  static String prevTop = "";
  static String prevSt = "";

  if (full) {
    prevTop = "";
    prevSt = "";
  }

  String top = "L" + String(arkLevel) + " HP:" + String(arkLives) + " F:" + String(arkFireLevel) + " S:" + String(arkScore);
  if (top.length() > 26) top = top.substring(0, 26);
  if (full || top != prevTop) {
    tft.fillRect(4, 4, 162, 12, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(top, 6, 6, 1);
    prevTop = top;
  }

  String st = String(arkStatus);
  if (st.length() > 26) st = st.substring(0, 26);
  if (full || st != prevSt) {
    tft.fillRect(4, 274, 162, 10, TFT_BLACK);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(st, 6, 275, 1);
    prevSt = st;
  }
}

void arkInitGame(bool full) {
  if (full || !arkInitialized) {
    arkInitialized = true;
    arkLives = 3;
    arkLevel = 1;
    arkScore = 0;
    arkPaddleW = 60;
    arkPaddleX = arkPlayX + (arkPlayW - arkPaddleW) / 2;
    arkPaddlePreciseX = (float)arkPaddleX;
    arkPaddleY = arkPlayY + arkPlayH - 10;
    arkExitHoldMs = 0;
    arkPaddleBoostUntilMs = 0;
    arkBuildLevel();
    arkResetBonuses();
    arkResetSparks();
    arkResetFireballs();
    arkFireLevel = 0;
    arkLastFireMs = 0;
    arkRespawnBall();
    arkPrevBallX = (int16_t)arkBallX;
    arkPrevBallY = (int16_t)arkBallY;
    arkPrevPaddleX = arkPaddleX;
    arkPrevPaddleW = arkPaddleW;
    arkSetStatus("Hold both 3s to exit");
  }
}

bool arkAllBlocksCleared() {
  for (uint8_t c = 0; c < kArkCols; c++) for (uint8_t r = 0; r < arkRows; r++) if (arkBlockAlive[c][r]) return false;
  return true;
}

void arkDrawBlocks(bool full) {
  if (full) {
    tft.fillRect(arkPlayX + 1, arkPlayY + 13, arkPlayW - 2, arkPlayH - 15, TFT_BLACK);
  }
  for (uint8_t c = 0; c < kArkCols; c++) {
    for (uint8_t r = 0; r < arkRows; r++) {
      int16_t bx = arkBlocksX + c * (arkBlockW + arkBlockGap);
      int16_t by = arkBlocksY + r * (arkBlockH + arkBlockGap);
      if (arkBlockAlive[c][r]) {
        uint16_t col = tft.color565(40 + r * 22, 120 + c * 7, 180 - r * 20);
        tft.fillRect(bx, by, arkBlockW, arkBlockH, col);
      } else if (full) {
        tft.fillRect(bx, by, arkBlockW, arkBlockH, TFT_BLACK);
      }
    }
  }
}

void drawAnimationScreen(bool full) {
  if (full) {
    if (tft.getRotation() != 0) tft.setRotation(0);
    tft.fillScreen(kColorMenuBg);
    drawTitle("Arkanoid");
    tft.drawRect(arkPlayX, arkPlayY, arkPlayW, arkPlayH, TFT_DARKGREY);
  }

  arkInitGame(full);
  uint32_t now = millis();
  if (arkStatusUntilMs > 0 && (int32_t)(now - arkStatusUntilMs) >= 0) {
    arkStatus[0] = '\0';
    arkStatusUntilMs = 0;
  }
  arkDrawHud(full);
  if (full) arkDrawBlocks(true);

  if (!full && now - lastAnimFrameMs < 15) return;
  lastAnimFrameMs = now;

  if (arkPaddleBoostUntilMs > 0 && now > arkPaddleBoostUntilMs && arkPaddleW > 60) {
    arkPaddleW = 60;
    if (arkPaddleX + arkPaddleW > (arkPlayX + arkPlayW - 2)) arkPaddleX = arkPlayX + arkPlayW - 2 - arkPaddleW;
    arkSetStatus("Paddle size normal");
  }

    // Erase only the uncovered parts of previous paddle to reduce flicker on fast direction changes.
  if (arkPrevPaddleX != arkPaddleX || arkPrevPaddleW != arkPaddleW) {
    int16_t oldL = arkPrevPaddleX;
    int16_t oldR = arkPrevPaddleX + arkPrevPaddleW - 1;
    int16_t newL = arkPaddleX;
    int16_t newR = arkPaddleX + arkPaddleW - 1;

    int16_t eraseLeftL = oldL;
    int16_t eraseLeftR = min<int16_t>(oldR, (int16_t)(newL - 1));
    if (eraseLeftR >= eraseLeftL) {
      tft.fillRect(eraseLeftL, arkPaddleY, eraseLeftR - eraseLeftL + 1, arkPaddleH, TFT_BLACK);
    }

    int16_t eraseRightL = max<int16_t>(oldL, (int16_t)(newR + 1));
    int16_t eraseRightR = oldR;
    if (eraseRightR >= eraseRightL) {
      tft.fillRect(eraseRightL, arkPaddleY, eraseRightR - eraseRightL + 1, arkPaddleH, TFT_BLACK);
    }
  }
  tft.fillCircle(arkPrevBallX, arkPrevBallY, arkBallR, TFT_BLACK);
  for (uint8_t i = 0; i < 4; i++) {
    if (!arkBonuses[i].active) continue;
    tft.fillRect((int16_t)arkBonuses[i].x - 4, (int16_t)arkBonuses[i].y - 4, 8, 8, TFT_BLACK);
    arkBonuses[i].y += 1.1f + 0.12f * (float)arkLevel;
    if (arkBonuses[i].y > arkPlayY + arkPlayH - 2) {
      arkBonuses[i].active = false;
      continue;
    }

    // If a falling bonus enters a live block area, drop it to avoid level-transition artifacts.
    bool bonusInsideBlock = false;
    for (uint8_t c = 0; c < kArkCols && !bonusInsideBlock; c++) {
      for (uint8_t r = 0; r < arkRows && !bonusInsideBlock; r++) {
        if (!arkBlockAlive[c][r]) continue;
        int16_t bx = arkBlocksX + c * (arkBlockW + arkBlockGap);
        int16_t by = arkBlocksY + r * (arkBlockH + arkBlockGap);
        if (arkBonuses[i].x >= bx && arkBonuses[i].x <= bx + arkBlockW && arkBonuses[i].y >= by && arkBonuses[i].y <= by + arkBlockH) {
          bonusInsideBlock = true;
        }
      }
    }
    if (bonusInsideBlock) {
      arkBonuses[i].active = false;
      continue;
    }

    if (arkBonuses[i].y >= arkPaddleY - 3 && arkBonuses[i].x >= arkPaddleX - 3 && arkBonuses[i].x <= (arkPaddleX + arkPaddleW + 3)) {
      uint8_t t = arkBonuses[i].type;
      float sx = arkBonuses[i].x;
      float sy = arkBonuses[i].y;
      arkBonuses[i].active = false;
      arkApplyBonus(t);
      // Bonus pop effect: small burst on pickup, denser for life bonus.
      uint8_t burst = (t == 1) ? 16 : (t == 3 ? 18 : 11);
      arkSpawnSparks(sx, sy, burst, 0.7f, 2.6f, false);
      arkDrawHud(false);
      continue;
    }
    uint16_t bc = (arkBonuses[i].type == 0) ? TFT_GREEN : (arkBonuses[i].type == 1 ? TFT_ORANGE : (arkBonuses[i].type == 2 ? TFT_CYAN : TFT_RED));
    tft.fillRect((int16_t)arkBonuses[i].x - 3, (int16_t)arkBonuses[i].y - 3, 7, 7, bc);
  }

  arkBallX += arkBallVx;
  arkBallY += arkBallVy;

  float minX = (float)(arkPlayX + 1 + arkBallR);
  float maxX = (float)(arkPlayX + arkPlayW - 2 - arkBallR);
  float minY = (float)(arkPlayY + 1 + arkBallR);

  if (arkBallX < minX) {
    arkBallX = minX;
    arkBallVx = fabsf(arkBallVx);
    arkSpawnSparks((float)(arkPlayX + 2), arkBallY, 5, 0.5f, 1.8f, false);
  }
  if (arkBallX > maxX) {
    arkBallX = maxX;
    arkBallVx = -fabsf(arkBallVx);
    arkSpawnSparks((float)(arkPlayX + arkPlayW - 3), arkBallY, 5, 0.5f, 1.8f, false);
  }
  if (arkBallY < minY) {
    arkBallY = minY;
    arkBallVy = fabsf(arkBallVy);
    arkSpawnSparks(arkBallX, (float)(arkPlayY + 2), 6, 0.6f, 1.9f, false);
  }

  // Paddle collision
  if (arkBallVy > 0 && arkBallY + arkBallR >= arkPaddleY && arkBallY - arkBallR <= arkPaddleY + arkPaddleH && arkBallX >= arkPaddleX - 2 && arkBallX <= arkPaddleX + arkPaddleW + 2) {
    arkBallY = (float)(arkPaddleY - arkBallR - 1);
    float hit = (arkBallX - (arkPaddleX + arkPaddleW * 0.5f)) / max<float>(1.0f, (float)arkPaddleW * 0.5f);
    float speed = sqrtf(arkBallVx * arkBallVx + arkBallVy * arkBallVy);
    arkBallVx = hit * (2.3f + 0.12f * arkLevel);
    arkBallVy = -max<float>(2.5f, speed - fabsf(arkBallVx) * 0.25f);
    arkSpawnSparks(arkBallX, arkPaddleY - 1, 9, 0.9f, 2.3f, true);
  }

  // Block collision
  bool hitBlock = false;
  for (uint8_t c = 0; c < kArkCols && !hitBlock; c++) {
    for (uint8_t r = 0; r < arkRows && !hitBlock; r++) {
      if (!arkBlockAlive[c][r]) continue;
      int16_t bx = arkBlocksX + c * (arkBlockW + arkBlockGap);
      int16_t by = arkBlocksY + r * (arkBlockH + arkBlockGap);
      if (arkBallX + arkBallR < bx || arkBallX - arkBallR > bx + arkBlockW || arkBallY + arkBallR < by || arkBallY - arkBallR > by + arkBlockH) continue;

      arkBlockAlive[c][r] = false;
      arkScore += 10U * arkLevel;
      tft.fillRect(bx, by, arkBlockW, arkBlockH, TFT_BLACK);
      arkSpawnSparks((float)(bx + arkBlockW / 2), (float)(by + arkBlockH / 2), 14, 0.7f, 2.8f, false);
      arkSpawnBonus((float)(bx + arkBlockW / 2), (float)(by + arkBlockH / 2));
      arkDrawHud(false);

      float cx = bx + arkBlockW * 0.5f;
      float cy = by + arkBlockH * 0.5f;
      if (fabsf(arkBallX - cx) > fabsf(arkBallY - cy)) arkBallVx = -arkBallVx;
      else arkBallVy = -arkBallVy;
      hitBlock = true;
    }
  }

  arkTryShoot(now);
  arkUpdateFireballs();

  // Ball out
  if (arkBallY - arkBallR > (arkPlayY + arkPlayH)) {
    if (arkLives > 0) arkLives--;
    if (arkLives == 0) {
      arkSetStatus("Game over. Restarting level 1");
      arkDrawHud(false);
      arkLevel = 1;
      arkLives = 3;
      arkScore = 0;
      arkPaddleW = 60;
      arkBuildLevel();
      arkResetBonuses();
      arkResetSparks();
      arkResetFireballs();
      arkFireLevel = 0;
      arkLastFireMs = 0;
    }
    arkRespawnBall();
    arkDrawBlocks(false);
    arkDrawHud(false);
  }

  if (arkAllBlocksCleared()) {
    arkLevel++;
    arkBuildLevel();
    arkResetBonuses();
    arkResetSparks();
    arkResetFireballs();
    arkLastFireMs = now;
    arkRespawnBall();
    arkSetStatus("Level up!");
    arkDrawBlocks(true);
    arkDrawHud(false);
  }

  if (arkPaddleX < arkPlayX + 1) arkPaddleX = arkPlayX + 1;
  if (arkPaddleX + arkPaddleW > arkPlayX + arkPlayW - 2) arkPaddleX = arkPlayX + arkPlayW - 2 - arkPaddleW;

  arkUpdateSparks();

  // draw current dynamic objects
  tft.fillRect(arkPaddleX, arkPaddleY, arkPaddleW, arkPaddleH, TFT_WHITE);
  uint16_t color = tft.color565((uint8_t)(50 + (arkLevel * 15) % 180), (uint8_t)(120 + (arkScore / 13) % 120), (uint8_t)(200 - min<uint32_t>(120, arkScore / 20)));
  tft.fillCircle((int16_t)arkBallX, (int16_t)arkBallY, arkBallR, color);

  arkPrevBallX = (int16_t)arkBallX;
  arkPrevBallY = (int16_t)arkBallY;
  arkPrevPaddleX = arkPaddleX;
  arkPrevPaddleW = arkPaddleW;
}
void drawSystemInfoScreen(bool full) {
  uint32_t now = millis();
  if (!full && now - lastSystemRefreshMs < 500) return;
  lastSystemRefreshMs = now;

  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    drawTitle("System info");
    tft.fillRect(4, 4, 312, 162, TFT_BLACK);
  }

  static String prev[8];
  if (full) {
    for (uint8_t i = 0; i < 8; i++) prev[i] = "";
  }

  String lines[8];
  String chipModel = ESP.getChipModel();
  lines[0] = "Chip: " + chipModel + " rev " + String(ESP.getChipRevision());

  uint16_t cpuMHz = ESP.getCpuFreqMHz();
  lines[1] = "CPU: " + String(cpuMHz) + " MHz  load " + String(cpuLoadPercent, 1) + "%";

  uint32_t heapTotal = ESP.getHeapSize() / 1024;
  uint32_t heapFree = ESP.getFreeHeap() / 1024;
  uint32_t heapUsed = (heapTotal > heapFree) ? (heapTotal - heapFree) : 0;
  float heapPct = (heapTotal > 0) ? (100.0f * (float)heapUsed / (float)heapTotal) : 0.0f;
  lines[2] = "Heap U/F/T KB: " + String(heapUsed) + "/" + String(heapFree) + "/" + String(heapTotal);
  lines[3] = "Heap used: " + String(heapPct, 1) + "%";

  uint32_t psTotal = ESP.getPsramSize() / 1024;
  uint32_t psFree = ESP.getFreePsram() / 1024;
  uint32_t psUsed = (psTotal > psFree) ? (psTotal - psFree) : 0;
  if (psTotal > 0) {
    float psPct = 100.0f * (float)psUsed / (float)psTotal;
    lines[4] = "PSRAM U/F/T KB: " + String(psUsed) + "/" + String(psFree) + "/" + String(psTotal);
    lines[5] = "PSRAM used: " + String(psPct, 1) + "%";
  } else {
    lines[4] = "PSRAM: not present";
    lines[5] = "PSRAM used: n/a";
  }

  uint32_t appUsed = ESP.getSketchSize() / 1024;
  uint32_t appFree = ESP.getFreeSketchSpace() / 1024;
  uint32_t appTotal = appUsed + appFree;
  float flashPct = (appTotal > 0) ? (100.0f * (float)appUsed / (float)appTotal) : 0.0f;
  lines[6] = "Flash(app) U/F/T KB: " + String(appUsed) + "/" + String(appFree) + "/" + String(appTotal) + " (" + String(flashPct, 1) + "%)";

  lines[7] = String("WiFi: ") + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("not connected"));

  for (uint8_t i = 0; i < 8; i++) {
    if (!full && lines[i] == prev[i]) continue;
    int16_t y = 8 + i * 19;
    tft.fillRect(10, y, 302, 14, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(lines[i], 12, y, 1);
    prev[i] = lines[i];
  }
  tft.fillRect(246, 152, 64, 12, TFT_BLACK);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawRightString(String("FW ") + String(FW_VERSION), 308, 154, 1);
}
void drawWiFiStatusLine() {
  String statusLine = wifiStatusText;
  if (wifiConnecting) statusLine += String('.', wifiUiDots + 1);
  if (statusLine.length() > 40) statusLine = statusLine.substring(0, 40);
  tft.fillRect(10, 46, 300, 12, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(statusLine, 12, 47, 1);
}
void drawWiFiManagerScreen(bool full) {
  updateWiFiTrafficStats();

  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    drawTitle("WiFi manager");

    tft.fillRect(4, 4, 312, 162, TFT_BLACK);

    tft.fillRoundRect(10, 10, 70, 22, 5, TFT_DARKCYAN);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.drawCentreString("SCAN", 45, 14, 2);

    tft.fillRoundRect(88, 10, 70, 22, 5, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.drawCentreString("SAVED", 123, 14, 2);

    tft.fillRoundRect(166, 10, 70, 22, 5, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawCentreString("FORGET", 201, 14, 2);

    uint16_t resetBg = tft.color565(95, 55, 35);
    tft.fillRoundRect(244, 10, 66, 22, 5, resetBg);
    tft.setTextColor(TFT_WHITE, resetBg);
    tft.drawCentreString("RESET", 277, 14, 2);

    String savedInfo = "Saved: " + (wifiSavedSsid.length() ? wifiSavedSsid : String("(none)"));
    if (savedInfo.length() > 46) savedInfo = savedInfo.substring(0, 46);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(savedInfo, 12, 34, 1);
  }

  drawWiFiStatusLine();

  tft.fillRect(10, 58, 300, 26, TFT_BLACK);
  if (wifiTraffic.supported) {
    uint64_t rxBytes = wifiTraffic.totalRxBytes - wifiTraffic.baseRxBytes;
    uint64_t txBytes = wifiTraffic.totalTxBytes - wifiTraffic.baseTxBytes;
    uint64_t rxPkts = wifiTraffic.totalRxPkts - wifiTraffic.baseRxPkts;
    uint64_t txPkts = wifiTraffic.totalTxPkts - wifiTraffic.baseTxPkts;

    String line1 = (wifiTraffic.appMode ? "APP RX " : "RX ") + formatBytesHuman(rxBytes) + "  TX " + formatBytesHuman(txBytes);
    String line2 = wifiTraffic.appMode
      ? ("HTTP RX " + formatCountHuman(rxPkts) + "  TX " + formatCountHuman(txPkts))
      : ("PKT RX " + formatCountHuman(rxPkts) + "  TX " + formatCountHuman(txPkts));
    if (line1.length() > 44) line1 = line1.substring(0, 44);
    if (line2.length() > 44) line2 = line2.substring(0, 44);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(line1, 12, 59, 1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(line2, 12, 71, 1);
  } else {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("Traffic stats: not available", 12, 65, 1);
  }

  const uint8_t visibleRows = 6;
  for (uint8_t i = 0; i < visibleRows; i++) {
    uint8_t idx = wifiListOffset + i;
    if (idx >= wifiNetCount) break;
    int16_t y = 90 + i * 12;
    uint16_t bg = (idx == wifiSelected) ? TFT_NAVY : TFT_BLACK;
    tft.fillRect(10, y - 1, 274, 12, bg);
    tft.setTextColor(TFT_WHITE, bg);
    String lock = (wifiNets[idx].auth == WIFI_AUTH_OPEN) ? " " : "*";
    String s = String(idx + 1) + ". " + wifiNets[idx].ssid + " (" + String(wifiNets[idx].rssi) + ")" + lock;
    if (s.length() > 44) s = s.substring(0, 44);
    tft.drawString(s, 12, y, 1);
  }

  if (wifiNetCount > visibleRows) {
    tft.fillRoundRect(286, 90, 24, 12, 3, TFT_DARKGREY);
    tft.fillRoundRect(286, 146, 24, 12, 3, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawCentreString("^", 298, 91, 1);
    tft.drawCentreString("v", 298, 147, 1);
  }
}
const char *kbRowsAlpha[3] = {"qwertyuiop", "asdfghjkl_", "zxcvbnm123"};
const char *kbRowsSym[3] = {"1234567890", "!@#$%^&*()", "._-+=/?:;,"};

void drawWiFiPasswordScreen(bool full) {
  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    drawTitle("WiFi password");
  }

  tft.fillRect(10, 0, 300, 170, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  String ss = "SSID: " + wifiInputSsid;
  if (ss.length() > 32) ss = ss.substring(0, 32);
  tft.drawString(ss, 12, 6, 2);
  if (wifiInputPass.length() == 0) {
    wifiInputPass = knownPasswordForSsid(wifiInputSsid);
  }

  String pwd = "PWD: " + wifiInputPass;
  if (pwd.length() > 40) pwd = pwd.substring(0, 40);
  tft.drawString(pwd, 12, 22, 2);

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
    tft.fillRoundRect(topBtns[i].x, 40, topBtns[i].w, 20, 4, topBtns[i].c);
    tft.setTextColor(TFT_WHITE, topBtns[i].c);
    tft.drawCentreString(topBtns[i].txt, topBtns[i].x + topBtns[i].w / 2, 43, 1);
  }

  const char **rows = wifiKbSymbols ? kbRowsSym : kbRowsAlpha;
  for (uint8_t r = 0; r < 3; r++) {
    for (uint8_t c = 0; c < 10; c++) {
      int16_t x = 10 + c * 30;
      int16_t y = 64 + r * 34;
      tft.fillRoundRect(x, y, 28, 30, 3, TFT_DARKCYAN);
      char ch = rows[r][c];
      if (!wifiKbSymbols && wifiKbShift && ch >= 'a' && ch <= 'z') ch = ch - 32;
      char text[2] = {ch, 0};
      tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
      tft.drawCentreString(text, x + 14, y + 8, 2);
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

float parseFloatField(const String &json, const char *key, float fallback) {
  String pat = String("\"") + key + "\":";
  int p = json.indexOf(pat);
  if (p < 0) return fallback;
  p += pat.length();
  int e = p;
  while (e < (int)json.length() && (json[e] == '-' || json[e] == '+' || json[e] == '.' || (json[e] >= '0' && json[e] <= '9'))) e++;
  return json.substring(p, e).toFloat();
}

String formatMonthYear(const String &isoDate) {
  if (isoDate.length() < 7) return "--/--";
  return isoDate.substring(5, 7) + "/" + isoDate.substring(2, 4);
}


int32_t isoDateToJdn(const String &isoDate) {
  if (isoDate.length() < 10) return 0;
  int y = isoDate.substring(0, 4).toInt();
  int m = isoDate.substring(5, 7).toInt();
  int d = isoDate.substring(8, 10).toInt();
  int a = (14 - m) / 12;
  int y2 = y + 4800 - a;
  int m2 = m + 12 * a - 3;
  return d + ((153 * m2 + 2) / 5) + 365L * y2 + y2 / 4 - y2 / 100 + y2 / 400 - 32045;
}

uint8_t dayOfWeekFromJdn(int32_t jdn) {
  // 0=Sunday ... 6=Saturday
  return (uint8_t)((jdn + 1) % 7);
}

void mapGitHubRecsToHeat(int recCount, int32_t maxJdn) {
  int32_t weekStartJdn = maxJdn - dayOfWeekFromJdn(maxJdn);
  int32_t firstWeekStartJdn = weekStartJdn - 52 * 7;

  for (uint16_t i = 0; i < 371; i++) { githubHeat[i] = 0; githubLevel[i] = 0; }
  for (int i = 0; i < recCount; i++) {
    int32_t jdn = githubRecBuf[i].jdn;
    if (jdn < firstWeekStartJdn || jdn > (weekStartJdn + 6)) continue;
    int32_t dayOffset = jdn - firstWeekStartJdn;
    uint8_t col = (uint8_t)(dayOffset / 7);
    uint8_t row = dayOfWeekFromJdn(jdn);
    uint16_t idx = col * 7 + row;
    githubHeat[idx] = (uint8_t)min((int)githubRecBuf[i].count, 250);
    githubLevel[idx] = (githubRecBuf[i].level <= 4) ? githubRecBuf[i].level : 255;
  }
}

void computeGitHubHeatMax() {
  githubHeatMax = 1;
  for (uint16_t i = 0; i < 371; i++) {
    if (githubHeat[i] > githubHeatMax) githubHeatMax = githubHeat[i];
  }
}
String knownPasswordForSsid(const String &ssid) {
  if (ssid.length() == 0) return "";
  if (wifiSavedSsid == ssid && wifiSavedPass.length() > 0) return wifiSavedPass;
  if (String(WIFI_DEFAULT_SSID) == ssid && String(WIFI_DEFAULT_PASSWORD).length() > 0) return String(WIFI_DEFAULT_PASSWORD);
  return "";
}


bool resolvePreferredWiFi(String &ssid, String &pass) {
  if (wifiSavedSsid.length() > 0) {
    ssid = wifiSavedSsid;
    pass = wifiSavedPass;
    return true;
  }
  if (String(WIFI_DEFAULT_SSID).length() > 0) {
    ssid = String(WIFI_DEFAULT_SSID);
    pass = String(WIFI_DEFAULT_PASSWORD);
    return true;
  }
  return false;
}

void maybeAutoConnectWiFi(bool forceNow = false) {
  if (!wifiAutoConnect) return;
  if (wifiConnecting) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (btScanInProgress) return;

  uint32_t now = millis();
  if (!forceNow && (now - lastAutoWiFiAttemptMs) < 12000) return;

  String ssid, pass;
  if (!resolvePreferredWiFi(ssid, pass) || ssid.length() == 0) return;

  wifiConnectHintValid = false;
  connectToWiFi(ssid, pass, false);
  lastAutoWiFiAttemptMs = now;
}
String makeLoadingLabel(uint8_t dots) {
  String s = "LOAD";
  uint8_t count = (dots % 3) + 1;
  for (uint8_t i = 0; i < count; i++) s += '.';
  return s;
}

void tickWebLoadingUi(bool weatherBtn, bool githubBtn) {
  if (weatherBtn) weatherLoadingDots = (weatherLoadingDots + 1) % 3;
  if (githubBtn) githubLoadingDots = (githubLoadingDots + 1) % 3;
  if (currentScreen == Screen::Web) {
    // Avoid full Web redraw during loading animation to reduce visible flicker.
    tft.fillRoundRect(10, 10, 145, 24, 5, TFT_DARKCYAN);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    String wb = weatherLoading ? makeLoadingLabel(weatherLoadingDots) : String("WEATHER");
    tft.drawCentreString(wb, 82, 14, 2);

    tft.fillRoundRect(165, 10, 145, 24, 5, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    String gb = githubLoading ? makeLoadingLabel(githubLoadingDots) : String("GITHUB");
    tft.drawCentreString(gb, 237, 14, 2);
    delay(1);
  }
}

void fetchWeather() {
  tickWebLoadingUi(true, false);
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
  addAppTraffic((uint32_t)(url.length() + 180), 0);

  tickWebLoadingUi(true, false);
  if (!http.begin(client, url)) {
    weatherText = "Weather: begin failed";
    return;
  }

  http.setTimeout(8000);
  tickWebLoadingUi(true, false);
  int code = http.GET();
  if (code != 200) {
    weatherText = "Weather HTTP " + String(code);
    http.end();
    return;
  }

  tickWebLoadingUi(true, false);
  String body = http.getString();
  addAppTraffic(0, (uint32_t)body.length());
  http.end();

  int cur = body.indexOf("\"current\":{");
  if (cur < 0) {
    weatherText = "Weather parse: no current";
    return;
  }

  int segEnd = body.indexOf("}", cur + 11);
  if (segEnd < 0) segEnd = min((int)body.length(), cur + 260);
  String curSeg = body.substring(cur, segEnd + 1);

  float t = parseFloatField(curSeg, "temperature_2m", -999.0f);
  float w = parseFloatField(curSeg, "wind_speed_10m", -1.0f);
  int c = parseIntField(curSeg, "weather_code", -1);

  if (t <= -998.0f || w < 0) {
    weatherText = "Weather parse failed";
    return;
  }

  weatherText = String(WEATHER_LABEL) + ": " + String(t, 1) + "C wind " + String(w, 1);
  Serial.printf("[Weather] t=%.1f w=%.1f code=%d\n", t, w, c);
}

void fetchGitHub() {
  tickWebLoadingUi(false, true);
  if (WiFi.status() != WL_CONNECTED) {
    githubText = "GitHub: no WiFi";
    return;
  }

  for (uint16_t i = 0; i < 371; i++) { githubHeat[i] = 0; githubLevel[i] = 0; }
  githubHeatMax = 1;
  githubRepo = "All public repos";
  githubRangeStart = "--/--";
  githubRangeEnd = "--/--";

  bool parsed = false;

  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    tickWebLoadingUi(false, true);
    if (http.begin(client, "https://api.github.com/graphql")) {
      http.addHeader("User-Agent", "LilyHammer-TDisplayS3");
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(12000);
      String token = String(GITHUB_TOKEN);
      if (token.length() > 0) http.addHeader("Authorization", "token " + token);

      String payload =
        "{\"query\":\"query(!){user(login:){contributionsCollection{contributionCalendar{totalContributions weeks{contributionDays{date contributionCount contributionLevel}}}}}}\","
        "\"variables\":{\"login\":\"" + String(GITHUB_USERNAME) + "\"}}";

      addAppTraffic((uint32_t)(payload.length() + 260), 0);
      tickWebLoadingUi(false, true);
      int code = http.POST(payload);
      if (code == 200) {
        tickWebLoadingUi(false, true);
        String body = http.getString();
        addAppTraffic(0, (uint32_t)body.length());

        int recCount = 0;
        long sum = 0;
        int32_t maxJdn = 0;
        String minDate = "";
        String maxDate = "";

        int pos = 0;
        const String dateKey = "\"date\":\"";
        const String countKey = "\"contributionCount\":";
        const String lvlKey = "\"contributionLevel\":\"";
        while (true) {
          int dp = body.indexOf(dateKey, pos);
          if (dp < 0) break;
          int ds = dp + dateKey.length();
          int de = body.indexOf('"', ds);
          if (de < 0) break;
          String d = body.substring(ds, de);

          int cp = body.indexOf(countKey, de);
          if (cp < 0) break;
          int cs = cp + countKey.length();
          int ce = cs;
          while (ce < (int)body.length() && (body[ce] >= '0' && body[ce] <= '9')) ce++;
          int cnt = body.substring(cs, ce).toInt();
          uint8_t lvl = 255;
          int lp = body.indexOf(lvlKey, ce);
          if (lp > 0) {
            int ls = lp + lvlKey.length();
            int le = body.indexOf('"', ls);
            if (le > ls) {
              String lv = body.substring(ls, le);
              if (lv == "NONE") lvl = 0;
              else if (lv == "FIRST_QUARTILE") lvl = 1;
              else if (lv == "SECOND_QUARTILE") lvl = 2;
              else if (lv == "THIRD_QUARTILE") lvl = 3;
              else if (lv == "FOURTH_QUARTILE") lvl = 4;
            }
          }

          if (d.length() == 10 && recCount < 420) {
            int32_t jdn = isoDateToJdn(d);
            githubRecBuf[recCount].jdn = jdn;
            githubRecBuf[recCount].count = (uint16_t)min(cnt, 65535);
            githubRecBuf[recCount].level = lvl;
            recCount++;
            sum += cnt;
            if (jdn > maxJdn) maxJdn = jdn;
            if (minDate.length() == 0 || d < minDate) minDate = d;
            if (maxDate.length() == 0 || d > maxDate) maxDate = d;
          }

          pos = ce + 1;
          if ((recCount & 31) == 0) {
            delay(0);
            tickWebLoadingUi(false, true);
          }
        }

        if (recCount > 0 && maxJdn > 0) {
          mapGitHubRecsToHeat(recCount, maxJdn);
          computeGitHubHeatMax();
          int total = parseIntField(body, "totalContributions", -1);
          if (total < 0) total = (int)sum;
          githubText = "Public contributions/year: " + String(total);
          if (minDate.length() == 10 && maxDate.length() == 10) {
            githubRangeStart = formatMonthYear(minDate);
            githubRangeEnd = formatMonthYear(maxDate);
          }
          parsed = true;
          Serial.printf("[GitHub] GraphQL rec=%d total=%d max=%u\n", recCount, total, githubHeatMax);
        }
      } else {
        Serial.printf("[GitHub] GraphQL HTTP %d\n", code);
      }
      http.end();
    }
  }

  if (!parsed) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = "https://github-contributions-api.jogruber.de/v4/" + String(GITHUB_USERNAME) + "?y=last";
    addAppTraffic((uint32_t)(url.length() + 180), 0);

    tickWebLoadingUi(false, true);
    if (!http.begin(client, url)) {
      githubText = "GitHub: begin failed";
      return;
    }
    http.addHeader("User-Agent", "LilyHammer-TDisplayS3");
    http.setTimeout(12000);
    tickWebLoadingUi(false, true);
    int code = http.GET();
    if (code != 200) {
      githubText = "GitHub HTTP " + String(code);
      http.end();
      return;
    }

    tickWebLoadingUi(false, true);
    String body = http.getString();
    addAppTraffic(0, (uint32_t)body.length());
    http.end();

    int recCount = 0;
    int32_t maxJdn = 0;
    long total = 0;
    String minDate = "";
    String maxDate = "";

    int pos = 0;
    while (true) {
      int dp = body.indexOf("\"date\":\"", pos);
      if (dp < 0) break;
      int ds = dp + 8;
      int de = body.indexOf('"', ds);
      if (de < 0) break;
      String d = body.substring(ds, de);

      int cp = body.indexOf("\"count\":", de);
      if (cp < 0) break;
      int cs = cp + 8;
      int ce = cs;
      while (ce < (int)body.length() && (body[ce] >= '0' && body[ce] <= '9')) ce++;
      int cnt = body.substring(cs, ce).toInt();

      if (d.length() == 10 && recCount < 420) {
        int32_t jdn = isoDateToJdn(d);
        githubRecBuf[recCount].jdn = jdn;
        githubRecBuf[recCount].count = (uint16_t)min(cnt, 65535);
        githubRecBuf[recCount].level = 255;
        recCount++;
        total += cnt;
        if (jdn > maxJdn) maxJdn = jdn;
        if (minDate.length() == 0 || d < minDate) minDate = d;
        if (maxDate.length() == 0 || d > maxDate) maxDate = d;
      }

      pos = ce + 1;
      if ((recCount & 31) == 0) {
        delay(0);
        tickWebLoadingUi(false, true);
      }
    }

    if (recCount == 0 || maxJdn == 0) {
      githubText = "GitHub parse failed";
      return;
    }

    mapGitHubRecsToHeat(recCount, maxJdn);
    computeGitHubHeatMax();
    githubText = "Public contributions/year: " + String(total);
    if (minDate.length() == 10 && maxDate.length() == 10) {
      githubRangeStart = formatMonthYear(minDate);
      githubRangeEnd = formatMonthYear(maxDate);
    }
    Serial.printf("[GitHub] fallback rec=%d total=%ld max=%u\n", recCount, total, githubHeatMax);
  }
}

void drawWebScreen(bool full) {
  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    drawTitle("Web");
  }

  if (full) tft.fillRect(4, 4, 312, 162, TFT_BLACK);

  tft.fillRoundRect(10, 10, 145, 24, 5, TFT_DARKCYAN);
  tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
  String wb = weatherLoading ? makeLoadingLabel(weatherLoadingDots) : String("WEATHER");
  tft.drawCentreString(wb, 82, 14, 2);

  tft.fillRoundRect(165, 10, 145, 24, 5, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  String gb = githubLoading ? makeLoadingLabel(githubLoadingDots) : String("GITHUB");
  tft.drawCentreString(gb, 237, 14, 2);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  String wt = weatherText;
  if (wt.length() > 46) wt = wt.substring(0, 46);
  while (wt.length() < 46) wt += " ";
  tft.drawString(wt, 4, 38, 1);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  String gt = githubText;
  if (gt.length() > 46) gt = gt.substring(0, 46);
  while (gt.length() < 46) gt += " ";
  tft.drawString(gt, 4, 50, 1);
  tft.setTextColor(kColorMenuHintText, kColorMenuBg);
  tft.drawString(githubRangeStart, 4, 62, 1);
  tft.drawRightString(githubRangeEnd, 316, 62, 1);

  uint8_t nonZero[371];
  uint16_t n = 0;
  for (uint16_t i = 0; i < 371; i++) {
    if (githubHeat[i] > 0) nonZero[n++] = githubHeat[i];
  }
  for (uint16_t i = 1; i < n; i++) {
    uint8_t key = nonZero[i];
    int j = i - 1;
    while (j >= 0 && nonZero[j] > key) {
      nonZero[j + 1] = nonZero[j];
      j--;
    }
    nonZero[j + 1] = key;
  }
  uint8_t t1 = 1, t2 = 2, t3 = 4;
  if (n > 0) {
    t1 = nonZero[(n - 1) * 25 / 100];
    t2 = nonZero[(n - 1) * 50 / 100];
    t3 = nonZero[(n - 1) * 80 / 100];
    if (t2 < t1) t2 = t1;
    if (t3 < t2) t3 = t2;
  }

  int16_t hx = 1, hy = 74;
  const uint8_t weeks = 53;
  const uint8_t rows = 7;
  const uint8_t cellW = 5;
  const uint8_t cellH = 5;
  const uint8_t gapX = 1;
  const uint8_t gapY = 1;

  for (uint8_t col = 0; col < weeks; col++) {
    for (uint8_t row = 0; row < rows; row++) {
      uint16_t arrIdx = col * rows + row;
      uint8_t v = githubHeat[arrIdx];
      uint8_t lvl = githubLevel[arrIdx];
      if (lvl > 4) {
        if (v == 0) lvl = 0;
        else if (v <= t1) lvl = 1;
        else if (v <= t2) lvl = 2;
        else if (v <= t3) lvl = 3;
        else lvl = 4;
      }
      uint16_t c = tft.color565(34, 39, 44);
      if (lvl == 1) c = tft.color565(155, 233, 168);
      else if (lvl == 2) c = tft.color565(64, 196, 99);
      else if (lvl == 3) c = tft.color565(48, 161, 78);
      else if (lvl == 4) c = tft.color565(33, 110, 57);
      tft.fillRect(hx + col * (cellW + gapX), hy + row * (cellH + gapY), cellW, cellH, c);
    }
  }
}

String fetchGitHubCommitMessage(const String &repo, const String &sha) {
  if (repo.length() == 0 || repo == "(unknown repo)") return "";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = (sha.length() > 0)
               ? ("https://api.github.com/repos/" + repo + "/commits/" + sha)
               : ("https://api.github.com/repos/" + repo + "/commits?per_page=1");
  addAppTraffic((uint32_t)(url.length() + 180), 0);

  if (!http.begin(client, url)) return "";
  http.addHeader("User-Agent", "LilyHammer-TDisplayS3");
  http.setTimeout(3000);

  int code = http.GET();
  String body = "";
  if (code == 200) {
    body = http.getString();
    addAppTraffic(0, (uint32_t)body.length());
  }
  http.end();

  if (code != 200 || body.length() == 0) {
    Serial.printf("[Projects] commit lookup failed repo=%s sha=%s code=%d\n", repo.c_str(), sha.c_str(), code);
    return "";
  }

  int msgPos = body.indexOf("\"message\":");
  if (msgPos < 0) return "";

  int q0 = body.indexOf('"', msgPos + 10);
  if (q0 < 0) return "";
  int i = q0 + 1;
  bool esc = false;
  String out = "";
  for (; i < (int)body.length(); i++) {
    char ch = body[i];
    if (esc) {
      out += ch;
      esc = false;
      continue;
    }
    if (ch == '\\') {
      out += ch;
      esc = true;
      continue;
    }
    if (ch == '"') break;
    out += ch;
  }

  out.replace("\\n", " ");
  out.replace("\\r", " ");
  out.replace("\\t", " ");
  out.replace("\\\"", "\"");
  if (out.length() > 64) out = out.substring(0, 64);
  return out;
}
void beginProjectsHydration() {
  projectsHydrateActive = false;
  projectsHydrateCursor = 0;
  projectsHydrateDone = 0;
  projectsHydrateTotal = 0;

  for (uint8_t i = 0; i < projectsState.count; i++) {
    if (projectsState.items[i].message == "(no message)") projectsHydrateTotal++;
  }

  if (projectsHydrateTotal > 0) {
    projectsHydrateActive = true;
    projectsState.loading = true;
    projectsState.status = "Loading details 0/" + String(projectsHydrateTotal);
  } else {
    projectsState.loading = false;
    projectsState.status = (projectsState.count == 0)
      ? String("No recent push events")
      : ("Recent push events: " + String(projectsState.count));
  }
}

void tickProjectsHydration() {
  if (!projectsHydrateActive) return;

  while (projectsHydrateCursor < projectsState.count) {
    if (projectsState.items[projectsHydrateCursor].message == "(no message)") break;
    projectsHydrateCursor++;
  }

  if (projectsHydrateCursor >= projectsState.count) {
    projectsHydrateActive = false;
    projectsState.loading = false;
    projectsState.status = (projectsState.count == 0)
      ? String("No recent push events")
      : ("Recent push events: " + String(projectsState.count));
    if (currentScreen == Screen::Projects || currentScreen == Screen::Menu) needFullRedraw = true;
    return;
  }

  String m = fetchGitHubCommitMessage(projectsState.items[projectsHydrateCursor].repo,
                                      projectsState.items[projectsHydrateCursor].sha);
  if (m.length() > 0) projectsState.items[projectsHydrateCursor].message = m;

  projectsHydrateDone++;
  projectsHydrateCursor++;
  projectsState.status = "Loading details " + String(projectsHydrateDone) + "/" + String(projectsHydrateTotal);
  if (currentScreen == Screen::Projects) needFullRedraw = true;
}

void fetchProjectsFeed() {
  projectsHydrateActive = false;
  projectsState.loading = true;
  projectsState.loadingDots = 0;
  projectsState.status = "Loading commits...";

  if (WiFi.status() != WL_CONNECTED) {
    projectsState.loading = false;
    projectsState.status = "Projects: no WiFi";
    return;
  }

  String body = "";
  int code = -1;
  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = "https://api.github.com/users/" + String(GITHUB_USERNAME) + "/events/public?per_page=50";
    addAppTraffic((uint32_t)(url.length() + 180), 0);

    if (!http.begin(client, url)) {
      projectsState.loading = false;
      projectsState.status = "Projects: begin failed";
      return;
    }

    http.addHeader("User-Agent", "LilyHammer-TDisplayS3");
    http.setTimeout(8000);

    code = http.GET();
    if (code == 200) {
      body = http.getString();
      addAppTraffic(0, (uint32_t)body.length());
    }
    http.end();
  }

  if (code != 200) {
    projectsState.loading = false;
    projectsState.status = "Projects HTTP " + String(code);
    return;
  }

  projectsState.count = (uint8_t)parseGitHubPushEvents(body, projectsState.items, kProjectCommitMax);
  projectsListOffset = 0;
  Serial.printf("[Projects] parsed=%u body=%u\n", (unsigned)projectsState.count, (unsigned)body.length());
  if (projectsState.count > 0) {
    Serial.printf("[Projects] sample repo=%s date=%s time=%s sha=%s msg=%s\n",
                  projectsState.items[0].repo.c_str(),
                  projectsState.items[0].date.c_str(),
                  projectsState.items[0].time.c_str(),
                  projectsState.items[0].sha.c_str(),
                  projectsState.items[0].message.c_str());
  }
  projectsState.lastFetchMs = millis();

  beginProjectsHydration();
}
void projectsScrollBy(int8_t delta) {
  const uint8_t visible = 5;
  if (projectsState.count <= visible) {
    projectsListOffset = 0;
    return;
  }
  int16_t maxOff = (int16_t)projectsState.count - visible;
  int16_t next = (int16_t)projectsListOffset + delta;
  if (next < 0) next = 0;
  if (next > maxOff) next = maxOff;
  if ((uint8_t)next != projectsListOffset) {
    projectsListOffset = (uint8_t)next;
    needFullRedraw = true;
  }
}
void drawProjectsScreen(bool full) {
  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    drawTitle("Projects");
  }

  tft.fillRect(4, 4, 312, 162, TFT_BLACK);

  uint16_t refBg = projectsState.loading ? TFT_DARKGREY : TFT_DARKCYAN;
  tft.fillRoundRect(10, 10, 96, 24, 5, refBg);
  tft.setTextColor(TFT_WHITE, refBg);
  String refTxt = projectsState.loading ? makeLoadingLabel(projectsState.loadingDots) : String("REFRESH");
  tft.drawCentreString(refTxt, 58, 16, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  String st = projectsState.status;
  if (st.length() > 46) st = st.substring(0, 46);
  tft.drawString(st, 10, 42, 1);

  const uint8_t visible = 5;
  uint8_t start = projectsListOffset;
  if (projectsState.count <= visible) start = 0;

  int16_t y = 56;
  for (uint8_t i = start; i < projectsState.count && i < (uint8_t)(start + visible); i++) {
    String line1 = String(i + 1) + ". [" + projectsState.items[i].date + " " + projectsState.items[i].time + "] " + projectsState.items[i].repo;
    String line2 = "   " + projectsState.items[i].message;

    if (line1.length() > 48) line1 = line1.substring(0, 48);
    if (line2.length() > 48) line2 = line2.substring(0, 48);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(line1, 8, y, 1);
    y += 10;
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(line2, 8, y, 1);
    y += 10;
  }

  if (projectsState.count == 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Tap REFRESH to load commits", 10, 66, 1);
  } else if (projectsState.count > visible) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Swipe list up/down", 204, 18, 1);
    tft.drawString(String(start + 1) + "-" + String(min((int)projectsState.count, (int)start + visible)) + "/" + String(projectsState.count), 226, 30, 1);
  }
}
void drawSettingsScreen(bool full) {
  if (full) {
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    drawTitle("Settings");
  }

  tft.fillRect(4, 4, 312, 162, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Backlight", 14, 12, 2);

  tft.fillRoundRect(110, 10, 28, 24, 4, TFT_DARKGREY);
  tft.fillRoundRect(236, 10, 28, 24, 4, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawCentreString("-", 124, 16, 2);
  tft.drawCentreString("+", 250, 16, 2);

  tft.fillRect(144, 14, 86, 8, TFT_DARKGREY);
  int w = map(backlightValue, 20, 255, 1, 86);
  tft.fillRect(144, 14, w, 8, TFT_GREEN);

  tft.fillRoundRect(10, 44, 145, 24, 5, wifiAutoConnect ? TFT_DARKGREEN : TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, wifiAutoConnect ? TFT_DARKGREEN : TFT_DARKGREY);
  tft.drawCentreString(wifiAutoConnect ? "AUTO: ON" : "AUTO: OFF", 82, 49, 2);

  tft.fillRoundRect(165, 44, 145, 24, 5, tft.color565(120,0,0));
  tft.setTextColor(TFT_WHITE, tft.color565(120,0,0));
  tft.drawCentreString("FORGET WIFI", 237, 49, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Default WiFi in file:", 12, 84, 1);
  String s = String(WIFI_DEFAULT_SSID);
  if (s.length() == 0) s = "(not set)";
  if (s.length() > 40) s = s.substring(0, 40);
  tft.drawString(s, 12, 98, 1);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawRightString(String("FW ") + String(FW_VERSION), 308, 154, 1);
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
    if (tft.getRotation() != 3) tft.setRotation(3);
    tft.fillScreen(kColorMenuBg);
    drawTitle("Activity monitor");
    tft.fillRect(4, 4, 312, 162, TFT_BLACK);
    tft.drawRect(12, 34, 296, 128, TFT_DARKGREY);
  } else {
    // Partial redraw to avoid full-frame flashes on periodic updates.
    tft.fillRect(12, 6, 296, 22, TFT_BLACK);
    tft.fillRect(13, 35, 294, 126, TFT_BLACK);
    tft.drawRect(12, 34, 296, 128, TFT_DARKGREY);
  }

  char line[64];
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(line, sizeof(line), "Loop FPS: %lu", (unsigned long)fps);
  tft.drawString(line, 14, 8, 2);
  snprintf(line, sizeof(line), "Heap: %lu KB", (unsigned long)freeKb);
  tft.drawString(line, 180, 8, 2);

  for (uint8_t i = 0; i < 60; i++) {
    uint8_t idx = (activityHead + i) % 60;
    uint8_t h = map(activityGraph[idx], 0, 200, 2, 124);
    int16_t x = 16 + i * 4;
    int16_t y = 160 - h;
    uint16_t c = tft.color565(40 + h * 2, 180, 20);
    tft.drawFastVLine(x, y, h, c);
  }
}

void drawCurrentScreen(bool full) {
  switch (currentScreen) {
    case Screen::Menu: if (full) drawMenuScreen(); break;
    case Screen::Timer: drawTimerScreen(full); break;
    case Screen::Homer: drawHomerScreen(full); break;
    case Screen::Animation: drawAnimationScreen(full); break;
    case Screen::SystemInfo: drawSystemInfoScreen(full); break;
    case Screen::WiFiManager: drawWiFiManagerScreen(full); break;
    case Screen::BluetoothManager: drawBluetoothManagerScreen(full); break;
    case Screen::WiFiPassword: drawWiFiPasswordScreen(full); break;
    case Screen::Web: drawWebScreen(full); break;
    case Screen::Projects: drawProjectsScreen(full); break;
    case Screen::Settings: drawSettingsScreen(full); break;
    case Screen::Activity: drawActivityScreen(full); break;
    case Screen::Fun: drawFunScreen(full); break;
  }
}

bool screenNeedsContinuousRefresh() {
  switch (currentScreen) {
    case Screen::Timer:
    case Screen::Animation:
    case Screen::SystemInfo:
    case Screen::Activity:
    case Screen::Fun:
    case Screen::Homer:
      return true;
    case Screen::WiFiManager:
      return false;
    default:
      return false;
  }
}
void openSelectedMenu() {
  AppId app = appByMenuIndex(selectedMenuIndex);
  switch (app) {
    case AppId::Timer: currentScreen = Screen::Timer; break;
    case AppId::HomerPicture: currentScreen = Screen::Homer; break;
    case AppId::Arkanoid: currentScreen = Screen::Animation; arkInitialized = false; break;
    case AppId::SystemInfo: currentScreen = Screen::SystemInfo; break;
    case AppId::WiFiManager: currentScreen = Screen::WiFiManager; break;
    case AppId::Bluetooth: currentScreen = Screen::BluetoothManager; break;
    case AppId::Web: currentScreen = Screen::Web; break;
    case AppId::Projects: currentScreen = Screen::Projects; break;
    case AppId::Settings: currentScreen = Screen::Settings; break;
    case AppId::ActivityMonitor: currentScreen = Screen::Activity; break;
    case AppId::Fun: currentScreen = Screen::Fun; break;
    default: currentScreen = Screen::Menu; break;
  }

  if (currentScreen == Screen::Projects && projectsState.count == 0 && !projectsState.loading) {
    fetchProjectsFeed();
  }

  needFullRedraw = true;
}

void goBackToMenu() {
  if (tft.getRotation() != 3) tft.setRotation(3);
  touch.setRotation(1);
  if (currentScreen == Screen::Fun || currentScreen == Screen::Homer) closeFunGif();
  currentScreen = Screen::Menu;
  needFullRedraw = true;
}

bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

void handleWiFiPasswordTap(int16_t x, int16_t y) {
  if (inRect(x, y, 10, 40, 48, 20)) {
    wifiKbShift = !wifiKbShift;
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 60, 40, 38, 20)) {
    wifiKbSymbols = !wifiKbSymbols;
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 100, 40, 52, 20)) {
    if (wifiInputPass.length() > 0) wifiInputPass.remove(wifiInputPass.length() - 1);
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 154, 40, 36, 20)) {
    wifiInputPass = "";
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 192, 40, 34, 20)) {
    connectToWiFi(wifiInputSsid, wifiInputPass, true);
    currentScreen = Screen::WiFiManager;
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 228, 40, 40, 20)) {
    currentScreen = Screen::WiFiManager;
    needFullRedraw = true;
    return;
  }

  if (y >= 64 && y <= 162 && x >= 10 && x <= 309) {
    uint8_t row = (y - 64) / 34;
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

void ensureWiFiSelectionVisible() {
  const uint8_t visibleRows = 6;
  if (wifiNetCount <= visibleRows) {
    wifiListOffset = 0;
    return;
  }
  if (wifiSelected < wifiListOffset) wifiListOffset = wifiSelected;
  uint8_t lastVisible = wifiListOffset + visibleRows - 1;
  if (wifiSelected > lastVisible) wifiListOffset = wifiSelected - (visibleRows - 1);
  uint8_t maxOffset = wifiNetCount - visibleRows;
  if (wifiListOffset > maxOffset) wifiListOffset = maxOffset;
}

void wifiScrollBy(int8_t delta) {
  const uint8_t visibleRows = 6;
  if (wifiNetCount <= visibleRows || delta == 0) return;
  int16_t maxOffset = (int16_t)wifiNetCount - visibleRows;
  int16_t next = (int16_t)wifiListOffset + delta;
  if (next < 0) next = 0;
  if (next > maxOffset) next = maxOffset;
  if (next == wifiListOffset) return;
  wifiListOffset = (uint8_t)next;
  if (wifiSelected < wifiListOffset) wifiSelected = wifiListOffset;
  uint8_t lastVisible = wifiListOffset + visibleRows - 1;
  if (wifiSelected > lastVisible) wifiSelected = lastVisible;
  needFullRedraw = true;
}

void handleWiFiManagerTap(int16_t x, int16_t y) {
  if (inRect(x, y, 10, 10, 70, 22)) {
    scanWiFiNetworks();
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 88, 10, 70, 22)) {
    wifiConnectHintValid = false;
    if (wifiSavedSsid.length() > 0) {
      connectToWiFi(wifiSavedSsid, wifiSavedPass, false);
    } else {
      wifiStatusText = "No saved WiFi";
    }
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 166, 10, 70, 22)) {
    prefsForgetWiFi();
    wifiStatusText = "Saved WiFi cleared";
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 244, 10, 66, 22)) {
    resetWiFiTrafficStats();
    wifiStatusText = "Traffic stats reset";
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 286, 90, 24, 12)) {
    wifiScrollBy(-1);
    return;
  }
  if (inRect(x, y, 286, 146, 24, 12)) {
    wifiScrollBy(1);
    return;
  }

  for (uint8_t i = 0; i < 6; i++) {
    uint8_t idx = wifiListOffset + i;
    if (idx >= wifiNetCount) break;
    int16_t ry = 89 + i * 12;
    if (inRect(x, y, 10, ry, 274, 12)) {
      wifiSelected = idx;
      wifiInputSsid = wifiNets[idx].ssid;
      wifiConnectHintSsid = wifiNets[idx].ssid;
      wifiConnectHintChannel = wifiNets[idx].channel;
      wifiConnectHintValid = wifiConnectHintChannel > 0;
      ensureWiFiSelectionVisible();
      wifiInputPass = knownPasswordForSsid(wifiInputSsid);
      if (wifiNets[idx].auth == WIFI_AUTH_OPEN) {
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
  if (inRect(x, y, 110, 10, 28, 24)) {
    backlightValue -= 20;
    applyBacklight();
    prefsSaveSettings();
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 236, 10, 28, 24)) {
    backlightValue += 20;
    applyBacklight();
    prefsSaveSettings();
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 10, 44, 145, 24)) {
    wifiAutoConnect = !wifiAutoConnect;
    prefsSaveSettings();
    needFullRedraw = true;
    return;
  }
  if (inRect(x, y, 165, 44, 145, 24)) {
    prefsForgetWiFi();
    wifiStatusText = "Saved WiFi cleared";
    needFullRedraw = true;
    return;
  }
}


void handleProjectsTap(int16_t x, int16_t y) {
  if (inRect(x, y, 10, 10, 96, 24)) {
    if (projectsState.loading || projectsHydrateActive) return;
    drawProjectsScreen(false);
    fetchProjectsFeed();
    needFullRedraw = true;
    return;
  }
}
void onTap(int16_t x, int16_t y) {
  if (currentScreen == Screen::Menu) {
    if (x > 10 && x < 310 && y > 16 && y < 160) openSelectedMenu();
    return;
  }

  if (isBackTap(x, y) && currentScreen != Screen::WiFiPassword) {
    goBackToMenu();
    return;
  }

  if (currentScreen == Screen::Fun || currentScreen == Screen::Homer) {
    goBackToMenu();
    return;
  }

  if (currentScreen == Screen::Animation) {
    // Arkanoid is controlled by physical buttons.
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

  if (currentScreen == Screen::BluetoothManager) {
    handleBluetoothTap(x, y);
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
    if (inRect(x, y, 10, 10, 145, 24)) {
      weatherLoading = true;
      weatherLoadingDots = 0;
      tickWebLoadingUi(true, false);
      fetchWeather();
      weatherLoading = false;
      weatherLoadingDots = 0;
      drawWebScreen(false);
      return;
    }
    if (inRect(x, y, 165, 10, 145, 24)) {
      githubLoading = true;
      githubLoadingDots = 0;
      tickWebLoadingUi(false, true);
      fetchGitHub();
      githubLoading = false;
      githubLoadingDots = 0;
      drawWebScreen(false);
      return;
    }
    return;
  }
  if (currentScreen == Screen::Projects) {
    handleProjectsTap(x, y);
    return;
  }

  if (currentScreen == Screen::Settings) {
    handleSettingsTap(x, y);
    return;
  }
}

void onSwipeLeft() {
  if (currentScreen != Screen::Menu) return;
  uint8_t oldIndex = selectedMenuIndex;
  if (selectedMenuIndex + 1 < kMenuCount) selectedMenuIndex++;
  else selectedMenuIndex = 0;
  animateMenuSwipe(oldIndex, selectedMenuIndex, true);
  drawMenuHeader();
  needFullRedraw = false;
}

void onSwipeRight() {
  if (currentScreen == Screen::Menu) {
    uint8_t oldIndex = selectedMenuIndex;
    if (selectedMenuIndex > 0) selectedMenuIndex--;
    else selectedMenuIndex = kMenuCount - 1;
    animateMenuSwipe(oldIndex, selectedMenuIndex, false);
    drawMenuHeader();
    needFullRedraw = false;
    return;
  }
  if (currentScreen == Screen::WiFiPassword) {
    currentScreen = Screen::WiFiManager;
  } else {
    goBackToMenu();
  }
}

void handlePhysicalButtons() {
  bool b1 = digitalRead(PIN_BUTTON_1) == LOW;
  bool b2 = digitalRead(PIN_BUTTON_2) == LOW;

  if (currentScreen == Screen::Animation) {
    uint32_t now = millis();
    if (b1 && b2) {
      if (arkExitHoldMs == 0) arkExitHoldMs = now;
      uint32_t held = now - arkExitHoldMs;
      if (held >= 3000) {
        arkInitialized = false;
        arkExitHoldMs = 0;
        if (tft.getRotation() != 3) tft.setRotation(3);
        touch.setRotation(1);
        goBackToMenu();
      } else {
        snprintf(arkStatus, sizeof(arkStatus), "Exit in %.1fs", (3000 - held) / 1000.0f);
        arkStatusUntilMs = millis() + 2000;
        drawAnimationScreen(false);
      }
    } else {
      arkExitHoldMs = 0;
      int16_t prevPaddleX = arkPaddleX;
      float prevPreciseX = arkPaddlePreciseX;
      float baseSpeed = (5.0f + (float)arkLevel * 0.5f) / 3.0f;
      float smoothSpeed = max(0.55f, baseSpeed * 0.5f);
      if (b1 && !b2) arkPaddlePreciseX -= smoothSpeed;
      if (b2 && !b1) arkPaddlePreciseX += smoothSpeed;

      float minP = (float)(arkPlayX + 1);
      float maxP = (float)(arkPlayX + arkPlayW - 2 - arkPaddleW);
      if (arkPaddlePreciseX < minP) arkPaddlePreciseX = minP;
      if (arkPaddlePreciseX > maxP) arkPaddlePreciseX = maxP;
      arkPaddleX = (int16_t)roundf(arkPaddlePreciseX);

      bool moved = (fabsf(arkPaddlePreciseX - prevPreciseX) > 0.01f);
      bool atLeftWall = (arkPaddleX <= arkPlayX + 1);
      bool atRightWall = (arkPaddleX + arkPaddleW >= arkPlayX + arkPlayW - 2);
      bool hitLeft = (b1 && !b2 && prevPaddleX > arkPaddleX && atLeftWall);
      bool hitRight = (b2 && !b1 && prevPaddleX < arkPaddleX && atRightWall);
      bool grindLeft = (b1 && !b2 && atLeftWall);
      bool grindRight = (b2 && !b1 && atRightWall);
      if (hitLeft || grindLeft) {
        uint8_t sideBurst = hitLeft ? 14 : 8;
        arkSpawnSparks((float)(arkPlayX + 2), (float)(arkPaddleY + arkPaddleH / 2), sideBurst, 0.45f, 1.8f, false);
      }
      if (hitRight || grindRight) {
        uint8_t sideBurst = hitRight ? 14 : 8;
        arkSpawnSparks((float)(arkPlayX + arkPlayW - 3), (float)(arkPaddleY + arkPaddleH / 2), sideBurst, 0.45f, 1.8f, false);
      }

      // Ground friction sparks while paddle slides over the bottom edge.
      if (moved) {
        float gx = (float)(arkPaddleX + random(2, max<int16_t>(3, arkPaddleW - 2)));
        float gy = (float)(arkPaddleY + arkPaddleH + 1);
        arkSpawnSparks(gx, gy, 5, 0.35f, 1.25f, true);
      }
    }

    btn1PressedPrev = b1;
    btn2PressedPrev = b2;
    return;
  }

  if (millis() - buttonsDebounceMs >= 120) {
    if (b1 && !btn1PressedPrev) {
      buttonsDebounceMs = millis();
      if (currentScreen == Screen::WiFiPassword) {
        currentScreen = Screen::WiFiManager;
        needFullRedraw = true;
      } else if (currentScreen != Screen::Menu) {
        goBackToMenu();
      }
    }

    if (b2 && !btn2PressedPrev) {
      buttonsDebounceMs = millis();
      if (currentScreen == Screen::WiFiManager) {
        wifiScrollBy(1);
      } else if (currentScreen == Screen::Menu) {
        onSwipeLeft();
      }
    }
  }

  btn1PressedPrev = b1;
  btn2PressedPrev = b2;
}

void reinitTouchControllerAfterBle() {
  touchState.active = false;
  digitalWrite(PIN_TOUCH_RES, LOW);
  delay(20);
  digitalWrite(PIN_TOUCH_RES, HIGH);
  delay(80);
  if (!touch.init()) {
    Serial.println("[Touch] reinit failed after BLE scan");
  } else {
    touch.setRotation(1);
    Serial.println("[Touch] reinit ok after BLE scan");
  }
}
void processTouch() {
  if (btScanInProgress || millis() < touchSkipReadUntilMs) {
    touchState.active = false;
    return;
  }

  uint32_t now = millis();
  if (now - lastTouchPollMs < 12) return;
  lastTouchPollMs = now;

  // Touch controller asserts INT low when fresh touch data is ready.
  // Avoid blind I2C polling while idle to prevent Wire requestFrom errors.
  bool touchIrqActive = (digitalRead(PIN_TOUCH_INT) == LOW);
  if (!touchIrqActive && !touchState.active) return;

  bool hasTouch = false;
  int16_t x = 0;
  int16_t y = 0;

  if (touchIrqActive && touch.read() && touch.getPointNum() > 0) {
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
  bool verticalWiFiSwipe = (currentScreen == Screen::WiFiManager) &&
                           (abs(dy) >= 20) && (abs(dy) > abs(dx)) &&
                           (touchState.startY >= 86);
  bool verticalProjectsSwipe = (currentScreen == Screen::Projects) &&
                               (abs(dy) >= 14) && (abs(dy) > abs(dx)) &&
                               (touchState.startY >= 52);
  bool horizontalProjectsBack = (currentScreen == Screen::Projects) &&
                                (dx > 0) && (abs(dx) >= 95) && (abs(dx) > (abs(dy) * 2));
  bool isTap = abs(dx) < kTapMoveThreshold && abs(dy) < kTapMoveThreshold && dt <= kTapTimeMs;

  if (verticalWiFiSwipe) {
    if (dy < 0) wifiScrollBy(1);
    else wifiScrollBy(-1);
  } else if (verticalProjectsSwipe) {
    if (dy < 0) projectsScrollBy(1);
    else projectsScrollBy(-1);
  } else if (horizontalProjectsBack) {
    onSwipeRight();
  } else if (horizontalSwipe && currentScreen != Screen::Projects) {
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
  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.println("LilyHammer boot");
  Serial.printf("Reset reason: %d\\n", (int)esp_reset_reason());
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      wifiLastDiscReason = info.wifi_sta_disconnected.reason;
      Serial.printf("[WiFi] disconnected reason=%u\n", (unsigned)wifiLastDiscReason);
    }
  });
  randomSeed((uint32_t)esp_random());

  tft.begin();
#if defined(LCD_MODULE_CMD_1)
  for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++) {
    tft.writecommand(lcd_st7789v[i].cmd);
    for (uint8_t j = 0; j < (lcd_st7789v[i].len & 0x7F); j++) tft.writedata(lcd_st7789v[i].data[j]);
    if (lcd_st7789v[i].len & 0x80) delay(120);
  }
#endif
  tft.setRotation(3);
  tft.fillScreen(kColorMenuBg);
  menuSwipeSprite.setColorDepth(16);
  menuSwipeSpriteReady = (menuSwipeSprite.createSprite(kScreenW, kMenuCardH) != nullptr);
  if (!menuSwipeSpriteReady) {
    menuSwipeSprite.setColorDepth(8);
    menuSwipeSpriteReady = (menuSwipeSprite.createSprite(kScreenW, kMenuCardH) != nullptr);
    if (menuSwipeSpriteReady) menuSwipeSpriteDepth = 8;
  }
  Serial.printf("[UI] menu swipe sprite: %s depth=%u\n", menuSwipeSpriteReady ? "OK" : "FAIL", menuSwipeSpriteDepth);

  Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
  if (!touch.init()) {
    tft.drawString("Touch init failed", 10, 10, 2);
    Serial.println("Touch IC not found");
  } else {
    touch.setRotation(1);
  }

  // Initialize BLE early to avoid first-time coex crash when Wi-Fi is already active.
  ensureBluetoothInit();

  prefsLoad();
  applyBacklight();

  bool bootAutoConnectStarted = false;
  if (wifiAutoConnect) {
    String bootSsid, bootPass;
    if (resolvePreferredWiFi(bootSsid, bootPass) && bootSsid.length() > 0) {
      connectToWiFi(bootSsid, bootPass, false);
      lastAutoWiFiAttemptMs = millis();
      bootAutoConnectStarted = true;
    }
  }

  // Do not interrupt autoconnect with an immediate startup scan.
  if (!bootAutoConnectStarted) scanWiFiNetworks();
  lastActivityRefreshMs = millis();
  drawCurrentScreen(true);
}

void loop() {
  uint32_t nowLoopMs = millis();
  if (lastLoopTickMs != 0) {
    uint32_t dt = nowLoopMs - lastLoopTickMs;
    if (dt > 0) {
      float busyMs = (dt > 5) ? (float)(dt - 5) : 0.0f;
      float instantLoad = (busyMs * 100.0f) / (float)dt;
      if (instantLoad < 0.0f) instantLoad = 0.0f;
      if (instantLoad > 100.0f) instantLoad = 100.0f;
      cpuLoadPercent = cpuLoadPercent * 0.85f + instantLoad * 0.15f;
    }
  }
  lastLoopTickMs = nowLoopMs;

  loopCounter++;
  updateWiFiConnectionState();
  maybeAutoConnectWiFi(false);
  tickProjectsHydration();
  updateBluetoothScanState();
  if (btScanRequested && !btScanInProgress && currentScreen == Screen::BluetoothManager) {
    btScanRequested = false;
    scanBluetoothDevices();
  }
  if (touchNeedsReinit) {
    reinitTouchControllerAfterBle();
    touchNeedsReinit = false;
    touchSkipReadUntilMs = millis() + 150;
  }
  handlePhysicalButtons();
  processTouch();

  // Safety net: menu must always be in landscape orientation.
  if (currentScreen == Screen::Menu && tft.getRotation() != 3) {
    tft.setRotation(3);
    touch.setRotation(1);
    needFullRedraw = true;
  }

  if (needFullRedraw) {
    drawCurrentScreen(true);
    needFullRedraw = false;
    lastUiFrameMs = millis();
  } else if (screenNeedsContinuousRefresh() && (millis() - lastUiFrameMs >= 33)) {
    drawCurrentScreen(false);
    lastUiFrameMs = millis();
  } else if (currentScreen == Screen::WiFiManager && (millis() - lastWiFiUiTickMs >= 500)) {
    if (wifiConnecting) wifiUiDots = (wifiUiDots + 1) % 3;
    drawWiFiStatusLine();
    lastWiFiUiTickMs = millis();
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




































































































































































































































































