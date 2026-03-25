#pragma once

#include <Arduino.h>

enum class AppId : uint8_t {
  Timer,
  HomerPicture,
  Arkanoid,
  SystemInfo,
  WiFiManager,
  Bluetooth,
  Web,
  Projects,
  Settings,
  ActivityMonitor,
  Fun
};

struct AppDescriptor {
  AppId id;
  const char *title;
};

static constexpr AppDescriptor kAppRegistry[] = {
  {AppId::Timer, "Timer"},
  {AppId::HomerPicture, "Homer picture"},
  {AppId::Arkanoid, "Arkanoid"},
  {AppId::SystemInfo, "System info"},
  {AppId::WiFiManager, "WiFi manager"},
  {AppId::Bluetooth, "Bluetooth"},
  {AppId::Web, "Web"},
  {AppId::Projects, "Projects"},
  {AppId::Settings, "Settings"},
  {AppId::ActivityMonitor, "Activity monitor"},
  {AppId::Fun, "Fun"},
};

static constexpr uint8_t kAppRegistryCount = sizeof(kAppRegistry) / sizeof(kAppRegistry[0]);
