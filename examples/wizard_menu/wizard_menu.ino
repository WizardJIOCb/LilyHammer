#include "Arduino.h"
#include "TFT_eSPI.h"
#include "TouchLib.h"
#include "Wire.h"
#include "WiFi.h"
#include "pin_config.h"

// The display module variant on some T-Display-S3 revisions needs this init sequence.
#define LCD_MODULE_CMD_1

#if defined(TOUCH_MODULES_CST_MUTUAL)
TouchLib touch(Wire, PIN_IIC_SDA, PIN_IIC_SCL, CTS328_SLAVE_ADDRESS, PIN_TOUCH_RES);
#elif defined(TOUCH_MODULES_CST_SELF)
TouchLib touch(Wire, PIN_IIC_SDA, PIN_IIC_SCL, CTS820_SLAVE_ADDRESS, PIN_TOUCH_RES);
#else
#error "Please choose the touch driver model in build_flags: TOUCH_MODULES_CST_MUTUAL or TOUCH_MODULES_CST_SELF"
#endif

TFT_eSPI tft = TFT_eSPI();

constexpr int16_t kScreenW = 320;
constexpr int16_t kScreenH = 170;
constexpr int16_t kBackBtnW = 62;
constexpr int16_t kBackBtnH = 26;
constexpr int16_t kSwipeThreshold = 45;
constexpr int16_t kTapMoveThreshold = 12;
constexpr uint16_t kTapTimeMs = 320;

enum class Screen : uint8_t {
  Menu,
  Timer,
  Homer,
  Animation,
  SystemInfo,
  WiFiBt,
  Activity
};

const char *kMenuItems[] = {
  "Timer",
  "Homer picture",
  "Animation",
  "System info",
  "WiFi / Bluetooth",
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

// Timer screen
bool timerRunning = false;
uint32_t timerAccumulatedMs = 0;
uint32_t timerStartMs = 0;
uint32_t lastTimerDrawMs = UINT32_MAX;

// Animation screen
int16_t ballX = 30;
int16_t ballY = 90;
int16_t ballVx = 2;
int16_t ballVy = 2;
uint32_t lastAnimFrameMs = 0;

// System screen
uint32_t lastSystemRefreshMs = 0;

// WiFi/BT screen
bool wifiConnectAttempted = false;
uint32_t lastWiFiRefreshMs = 0;

// Activity screen
uint8_t activityGraph[60] = {0};
uint8_t activityHead = 0;
uint32_t lastActivityRefreshMs = 0;
uint32_t loopCounter = 0;
uint32_t fps = 0;

// Touch alignment for T-Display-S3:
// 1) rotate touch data (swap XY) via touch.setRotation(1)
// 2) mirror X to match TFT orientation in setRotation(3)
constexpr bool kTouchMirrorX = true;
constexpr bool kTouchMirrorY = false;

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
  tft.drawCentreString("Wizard Hub Menu", kScreenW / 2, 7, 2);

  tft.fillRoundRect(20, 40, 280, 92, 12, TFT_DARKGREY);
  tft.drawRoundRect(20, 40, 280, 92, 12, TFT_CYAN);
  tft.setTextColor(TFT_YELLOW, TFT_DARKGREY);
  tft.drawCentreString(kMenuItems[selectedMenuIndex], kScreenW / 2, 72, 4);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawCentreString("Swipe left/right to switch item", kScreenW / 2, 138, 2);
  tft.drawCentreString("Tap center to open", kScreenW / 2, 154, 2);

  if (selectedMenuIndex > 0) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("<", 6, 82, 4);
  }
  if (selectedMenuIndex + 1 < kMenuCount) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(">", 304, 82, 4);
  }
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
  if (!full && elapsed == lastTimerDrawMs) {
    return;
  }

  uint32_t centi = (elapsed / 10) % 100;
  uint32_t sec = (elapsed / 1000) % 60;
  uint32_t min = (elapsed / 60000);
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

  // Stylized Homer-like drawing to keep sketch lightweight.
  tft.fillCircle(115, 96, 42, TFT_YELLOW);
  tft.fillRect(90, 66, 50, 15, TFT_YELLOW);   // forehead
  tft.fillRect(92, 115, 45, 20, TFT_YELLOW);  // jaw

  tft.fillCircle(103, 92, 10, TFT_WHITE);
  tft.fillCircle(122, 92, 10, TFT_WHITE);
  tft.fillCircle(106, 93, 3, TFT_BLACK);
  tft.fillCircle(125, 93, 3, TFT_BLACK);

  tft.drawLine(128, 95, 140, 99, TFT_ORANGE);
  tft.drawLine(140, 99, 128, 105, TFT_ORANGE);
  tft.drawLine(95, 116, 132, 116, TFT_BLACK);  // mouth
  tft.drawArc(114, 114, 18, 14, 10, 170, TFT_BLACK, TFT_YELLOW);
  tft.fillCircle(104, 126, 6, TFT_LIGHTGREY);  // beard dots
  tft.fillCircle(118, 127, 6, TFT_LIGHTGREY);
  tft.fillCircle(132, 126, 6, TFT_LIGHTGREY);

  tft.drawLine(90, 63, 98, 52, TFT_BLACK);     // hair
  tft.drawLine(98, 52, 102, 64, TFT_BLACK);
  tft.drawLine(98, 63, 106, 52, TFT_BLACK);
  tft.drawLine(106, 52, 110, 64, TFT_BLACK);

  tft.fillRect(176, 52, 132, 84, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("Demo image:", 182, 58, 2);
  tft.drawString("Homer style", 182, 78, 2);
  tft.drawString("Tap BACK or", 182, 104, 2);
  tft.drawString("swipe right", 182, 122, 2);
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
  if (!full && now - lastAnimFrameMs < 20) {
    return;
  }
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
  if (!full && now - lastSystemRefreshMs < 500) {
    return;
  }
  lastSystemRefreshMs = now;

  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("System info");
  }

  tft.fillRect(10, 42, 300, 118, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char line[64];
  String chipModel = ESP.getChipModel();
  snprintf(line, sizeof(line), "Chip: %s rev %d", chipModel.c_str(), ESP.getChipRevision());
  tft.drawString(line, 14, 48, 2);
  snprintf(line, sizeof(line), "CPU: %d MHz", ESP.getCpuFreqMHz());
  tft.drawString(line, 14, 68, 2);
  snprintf(line, sizeof(line), "Heap free: %u KB", (unsigned int)(ESP.getFreeHeap() / 1024));
  tft.drawString(line, 14, 88, 2);
  snprintf(line, sizeof(line), "Flash: %u MB", (unsigned int)(ESP.getFlashChipSize() / (1024 * 1024)));
  tft.drawString(line, 14, 108, 2);
  snprintf(line, sizeof(line), "Uptime: %lu s", (unsigned long)(millis() / 1000));
  tft.drawString(line, 14, 128, 2);
}

const char *wifiModeName(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_MODE_NULL: return "OFF";
    case WIFI_MODE_STA: return "STA";
    case WIFI_MODE_AP: return "AP";
    case WIFI_MODE_APSTA: return "AP+STA";
    default: return "?";
  }
}

void drawWiFiBtScreen(bool full) {
  uint32_t now = millis();
  if (!full && now - lastWiFiRefreshMs < 700) {
    return;
  }
  lastWiFiRefreshMs = now;

  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawTitle("WiFi / Bluetooth");
  }

  if (!wifiConnectAttempted) {
    wifiConnectAttempted = true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  tft.fillRect(10, 42, 300, 118, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char line[80];
  snprintf(line, sizeof(line), "SSID cfg: %s", WIFI_SSID);
  tft.drawString(line, 14, 48, 2);
  snprintf(line, sizeof(line), "WiFi mode: %s", wifiModeName(WiFi.getMode()));
  tft.drawString(line, 14, 68, 2);
  snprintf(line, sizeof(line), "WiFi status: %d", (int)WiFi.status());
  tft.drawString(line, 14, 88, 2);
  String mac = WiFi.macAddress();
  snprintf(line, sizeof(line), "WiFi MAC: %s", mac.c_str());
  tft.drawString(line, 14, 108, 2);
  tft.drawString("BT: ESP32-S3 BLE supported", 14, 128, 2);
}

void pushActivityValue(uint8_t value) {
  activityGraph[activityHead] = value;
  activityHead = (activityHead + 1) % 60;
}

void drawActivityScreen(bool full) {
  uint32_t now = millis();
  if (!full && now - lastActivityRefreshMs < 1000) {
    return;
  }

  uint32_t elapsed = now - lastActivityRefreshMs;
  if (elapsed > 0) {
    fps = (loopCounter * 1000UL) / elapsed;
  } else {
    fps = 0;
  }
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
    int16_t x = 16 + (i * 4);
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
    case Screen::WiFiBt: drawWiFiBtScreen(full); break;
    case Screen::Activity: drawActivityScreen(full); break;
  }
}

void openSelectedMenu() {
  currentScreen = static_cast<Screen>(selectedMenuIndex + 1);
  needFullRedraw = true;
}

void goBackToMenu() {
  currentScreen = Screen::Menu;
  needFullRedraw = true;
}

bool isBackTap(int16_t x, int16_t y) {
  return (x >= 6 && x <= (6 + kBackBtnW) && y >= 6 && y <= (6 + kBackBtnH));
}

void onTap(int16_t x, int16_t y) {
  if (currentScreen == Screen::Menu) {
    if (x > 20 && x < 300 && y > 40 && y < 132) {
      openSelectedMenu();
    }
    return;
  }

  if (isBackTap(x, y)) {
    goBackToMenu();
    return;
  }

  if (currentScreen == Screen::Timer) {
    if (x >= 20 && x <= 150 && y >= 108 && y <= 150) {
      if (!timerRunning) {
        timerRunning = true;
        timerStartMs = millis();
      } else {
        timerAccumulatedMs += millis() - timerStartMs;
        timerRunning = false;
      }
      drawTimerControls();
      lastTimerDrawMs = UINT32_MAX;
    } else if (x >= 170 && x <= 300 && y >= 108 && y <= 150) {
      timerRunning = false;
      timerAccumulatedMs = 0;
      lastTimerDrawMs = UINT32_MAX;
      drawTimerControls();
    }
  }
}

void onSwipeLeft() {
  if (currentScreen != Screen::Menu) {
    return;
  }
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
  goBackToMenu();
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

  if (!touchState.active) {
    return;
  }

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
  digitalWrite(PIN_LCD_BL, HIGH);

  Serial.begin(115200);
  Serial.println("Wizard menu boot");

  tft.begin();
#if defined(LCD_MODULE_CMD_1)
  for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++) {
    tft.writecommand(lcd_st7789v[i].cmd);
    for (uint8_t j = 0; j < (lcd_st7789v[i].len & 0x7F); j++) {
      tft.writedata(lcd_st7789v[i].data[j]);
    }
    if (lcd_st7789v[i].len & 0x80) {
      delay(120);
    }
  }
#endif

  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
  if (!touch.init()) {
    tft.drawString("Touch init failed", 10, 10, 2);
    Serial.println("Touch IC not found");
  } else {
    touch.setRotation(1);
  }

  lastActivityRefreshMs = millis();
  drawCurrentScreen(true);
}

void loop() {
  loopCounter++;
  processTouch();

  if (needFullRedraw) {
    drawCurrentScreen(true);
    needFullRedraw = false;
  } else {
    drawCurrentScreen(false);
  }

  delay(5);
}

// TFT pin safety checks.
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
#error "Please select TFT_eSPI/User_Setups/Setup206_LilyGo_T_Display_S3.h in TFT_eSPI/User_Setup_Select.h"
#endif




