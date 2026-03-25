#pragma once

#include <Arduino.h>

struct NewsItem {
  uint32_t id = 0;
  int32_t epoch = 0;
  int32_t likes = 0;
  String source;
  String title;
  String date;
  String time;
  String excerpt;
};

constexpr uint8_t kNewsItemMax = 12;

struct NewsFeedState {
  NewsItem items[kNewsItemMax];
  uint8_t count = 0;
  String status = "No data";
  bool loading = false;
  uint8_t loadingDots = 0;
  uint32_t lastFetchMs = 0;
};

String newsShortDateFromEpoch(int32_t epochUtc);
String newsShortTimeFromEpoch(int32_t epochUtc);
String newsJsonUnescape(String s);
String newsStripHtml(const String &src);
int parseNewsTimelineHot(const String &json, const char *sourceTag, NewsItem *outItems, uint8_t maxItems);
String parseNewsContentText(const String &json, String &outTitle);

