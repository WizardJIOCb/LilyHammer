#pragma once

#include <Arduino.h>

struct ProjectCommitItem {
  String repo;
  String date;
  String time;
  String message;
  String sha;
};

constexpr uint8_t kProjectCommitMax = 8;

struct ProjectsFeedState {
  ProjectCommitItem items[kProjectCommitMax];
  uint8_t count = 0;
  String status = "No data";
  bool loading = false;
  uint8_t loadingDots = 0;
  uint32_t lastFetchMs = 0;
};

String projectsShortDate(const String &isoUtc);
String projectsShortTime(const String &isoUtc);
String projectsJsonUnescape(String s);
int parseGitHubPushEvents(const String &json, ProjectCommitItem *outItems, uint8_t maxItems);
