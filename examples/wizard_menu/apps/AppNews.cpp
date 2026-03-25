#include "AppNews.h"

#include <time.h>

static int findJsonObjectEndNews(const String &src, int objStart) {
  if (objStart < 0 || objStart >= (int)src.length() || src[objStart] != '{') return -1;
  int depth = 0;
  bool inString = false;
  bool esc = false;

  for (int i = objStart; i < (int)src.length(); i++) {
    char ch = src[i];
    if (inString) {
      if (esc) esc = false;
      else if (ch == '\\') esc = true;
      else if (ch == '"') inString = false;
      continue;
    }
    if (ch == '"') {
      inString = true;
      continue;
    }
    if (ch == '{') depth++;
    else if (ch == '}') {
      depth--;
      if (depth == 0) return i;
    }
  }
  return -1;
}

static String findQuotedAfterNews(const String &src, int quoteStart) {
  if (quoteStart < 0 || quoteStart >= (int)src.length() || src[quoteStart] != '"') return "";
  String out = "";
  bool esc = false;
  for (int i = quoteStart + 1; i < (int)src.length(); i++) {
    char ch = src[i];
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
  return out;
}

static String findJsonStringByKeyNews(const String &src, const char *key, int fromPos) {
  int seek = fromPos;
  while (true) {
    int kp = src.indexOf(key, seek);
    if (kp < 0) return "";

    int i = kp + (int)strlen(key);
    while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) i++;
    if (i < (int)src.length() && src[i] == ':') {
      i++;
      while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) i++;
      if (i < (int)src.length() && src[i] == '"') return findQuotedAfterNews(src, i);
    }

    seek = kp + 1;
  }
}

static int32_t findJsonIntByKeyNews(const String &src, const char *key, int fromPos, int32_t defVal) {
  int seek = fromPos;
  while (true) {
    int kp = src.indexOf(key, seek);
    if (kp < 0) return defVal;

    int i = kp + (int)strlen(key);
    while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) i++;
    if (i < (int)src.length() && src[i] == ':') {
      i++;
      while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) i++;
      bool neg = false;
      if (i < (int)src.length() && src[i] == '-') {
        neg = true;
        i++;
      }
      int32_t v = 0;
      bool hasDigit = false;
      while (i < (int)src.length() && src[i] >= '0' && src[i] <= '9') {
        v = v * 10 + (src[i] - '0');
        hasDigit = true;
        i++;
      }
      if (hasDigit) return neg ? -v : v;
    }

    seek = kp + 1;
  }
}

String newsJsonUnescape(String s) {
  s.replace("\\n", " ");
  s.replace("\\r", " ");
  s.replace("\\t", " ");
  s.replace("\\\"", "\"");
  s.replace("\\/", "/");
  s.replace("&quot;", "\"");
  s.replace("&#34;", "\"");
  s.replace("&amp;", "&");
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&nbsp;", " ");
  return s;
}

String newsStripHtml(const String &src) {
  String s = newsJsonUnescape(src);
  String out = "";
  bool inTag = false;

  for (int i = 0; i < (int)s.length(); i++) {
    char ch = s[i];
    if (ch == '<') {
      if (i + 3 < (int)s.length()) {
        String t = s.substring(i, min((int)s.length(), i + 6));
        t.toLowerCase();
        if (t.startsWith("<br") || t.startsWith("</p") || t.startsWith("<p")) out += '\n';
      }
      inTag = true;
      continue;
    }
    if (ch == '>') {
      inTag = false;
      continue;
    }
    if (!inTag) out += ch;
  }

  while (out.indexOf("  ") >= 0) out.replace("  ", " ");
  while (out.indexOf("\n\n\n") >= 0) out.replace("\n\n\n", "\n\n");
  out.trim();
  return out;
}

String newsShortDateFromEpoch(int32_t epochUtc) {
  if (epochUtc <= 0) return "--/--";
  time_t t = (time_t)epochUtc;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char b[9];
  snprintf(b, sizeof(b), "%02d/%02d/%02d", tmv.tm_mday, tmv.tm_mon + 1, (tmv.tm_year + 1900) % 100);
  return String(b);
}

String newsShortTimeFromEpoch(int32_t epochUtc) {
  if (epochUtc <= 0) return "--:--";
  time_t t = (time_t)epochUtc;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return String(b);
}

int parseNewsTimelineHot(const String &json, const char *sourceTag, NewsItem *outItems, uint8_t maxItems) {
  if (!outItems || maxItems == 0) return 0;

  uint8_t count = 0;
  int pos = 0;

  while (count < maxItems) {
    int entryPos = json.indexOf("\"type\":\"entry\",\"data\":{", pos);
    if (entryPos < 0) break;

    int dataStart = json.indexOf('{', entryPos + 20);
    if (dataStart < 0) break;
    int dataEnd = findJsonObjectEndNews(json, dataStart);
    if (dataEnd < 0) break;

    String entry = json.substring(dataStart, dataEnd + 1);
    pos = dataEnd + 1;

    int32_t id = findJsonIntByKeyNews(entry, "\"id\"", 0, 0);
    if (id <= 0) continue;

    bool duplicate = false;
    for (uint8_t i = 0; i < count; i++) {
      if (outItems[i].id == (uint32_t)id) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    String title = findJsonStringByKeyNews(entry, "\"title\"", 0);
    if (title.length() == 0) continue;

    int32_t epoch = findJsonIntByKeyNews(entry, "\"date\"", 0, 0);
    int32_t likes = findJsonIntByKeyNews(entry, "\"likes\"", 0, 0);

    String textRaw = findJsonStringByKeyNews(entry, "\"text\"", 0);
    String excerpt = newsStripHtml(textRaw);
    if (excerpt.length() > 96) excerpt = excerpt.substring(0, 96);

    outItems[count].id = (uint32_t)id;
    outItems[count].epoch = epoch;
    outItems[count].likes = likes;
    outItems[count].source = sourceTag ? String(sourceTag) : String("-");
    outItems[count].title = newsJsonUnescape(title);
    outItems[count].date = newsShortDateFromEpoch(epoch);
    outItems[count].time = newsShortTimeFromEpoch(epoch);
    outItems[count].excerpt = excerpt;
    count++;
  }

  return count;
}

String parseNewsContentText(const String &json, String &outTitle) {
  outTitle = newsJsonUnescape(findJsonStringByKeyNews(json, "\"title\"", 0));

  String full = "";
  int pos = 0;
  while (true) {
    int kp = json.indexOf("\"text\":\"", pos);
    if (kp < 0) break;
    int q = json.indexOf('"', kp + 8);
    if (q < 0) break;
    String raw = findQuotedAfterNews(json, q);
    String clean = newsStripHtml(raw);
    if (clean.length() > 0) {
      if (full.length() > 0) full += "\n\n";
      full += clean;
      if (full.length() > 2200) break;
    }
    pos = q + raw.length() + 2;
  }

  if (full.length() == 0) full = "(no text)";
  if (full.length() > 2200) full = full.substring(0, 2200);
  return full;
}

