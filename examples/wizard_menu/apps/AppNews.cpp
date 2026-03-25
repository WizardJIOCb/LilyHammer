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

static void appendCyrillicTranslit(String &out, uint16_t cp) {
  switch (cp) {
    case 0x0410: out += "A"; break;  case 0x0430: out += "a"; break;
    case 0x0411: out += "B"; break;  case 0x0431: out += "b"; break;
    case 0x0412: out += "V"; break;  case 0x0432: out += "v"; break;
    case 0x0413: out += "G"; break;  case 0x0433: out += "g"; break;
    case 0x0414: out += "D"; break;  case 0x0434: out += "d"; break;
    case 0x0415: out += "E"; break;  case 0x0435: out += "e"; break;
    case 0x0401: out += "Yo"; break; case 0x0451: out += "yo"; break;
    case 0x0416: out += "Zh"; break; case 0x0436: out += "zh"; break;
    case 0x0417: out += "Z"; break;  case 0x0437: out += "z"; break;
    case 0x0418: out += "I"; break;  case 0x0438: out += "i"; break;
    case 0x0419: out += "Y"; break;  case 0x0439: out += "y"; break;
    case 0x041A: out += "K"; break;  case 0x043A: out += "k"; break;
    case 0x041B: out += "L"; break;  case 0x043B: out += "l"; break;
    case 0x041C: out += "M"; break;  case 0x043C: out += "m"; break;
    case 0x041D: out += "N"; break;  case 0x043D: out += "n"; break;
    case 0x041E: out += "O"; break;  case 0x043E: out += "o"; break;
    case 0x041F: out += "P"; break;  case 0x043F: out += "p"; break;
    case 0x0420: out += "R"; break;  case 0x0440: out += "r"; break;
    case 0x0421: out += "S"; break;  case 0x0441: out += "s"; break;
    case 0x0422: out += "T"; break;  case 0x0442: out += "t"; break;
    case 0x0423: out += "U"; break;  case 0x0443: out += "u"; break;
    case 0x0424: out += "F"; break;  case 0x0444: out += "f"; break;
    case 0x0425: out += "Kh"; break; case 0x0445: out += "kh"; break;
    case 0x0426: out += "Ts"; break; case 0x0446: out += "ts"; break;
    case 0x0427: out += "Ch"; break; case 0x0447: out += "ch"; break;
    case 0x0428: out += "Sh"; break; case 0x0448: out += "sh"; break;
    case 0x0429: out += "Sch"; break;case 0x0449: out += "sch"; break;
    case 0x042A: break;               case 0x044A: break;
    case 0x042B: out += "Y"; break;  case 0x044B: out += "y"; break;
    case 0x042C: break;               case 0x044C: break;
    case 0x042D: out += "E"; break;  case 0x044D: out += "e"; break;
    case 0x042E: out += "Yu"; break; case 0x044E: out += "yu"; break;
    case 0x042F: out += "Ya"; break; case 0x044F: out += "ya"; break;
    default: out += "?"; break;
  }
}

static String newsUtf8ToDisplay(const String &src) {
  String out = "";

  for (int i = 0; i < (int)src.length(); i++) {
    uint8_t b0 = (uint8_t)src[i];
    if (b0 < 0x80) {
      out += (char)b0;
      continue;
    }

    if (i + 1 < (int)src.length()) {
      uint8_t b1 = (uint8_t)src[i + 1];

      if (b0 == 0xC2 && b1 == 0xA0) {
        out += ' ';
        i += 1;
        continue;
      }

      if ((b0 == 0xD0 || b0 == 0xD1) && ((b1 & 0xC0) == 0x80)) {
        uint16_t cp = ((uint16_t)(b0 & 0x1F) << 6) | (uint16_t)(b1 & 0x3F);
        appendCyrillicTranslit(out, cp);
        i += 1;
        continue;
      }

      if (i + 2 < (int)src.length()) {
        uint8_t b2 = (uint8_t)src[i + 2];
        if (b0 == 0xE2 && b1 == 0x80) {
          if (b2 == 0x93 || b2 == 0x94) {
            out += '-';
            i += 2;
            continue;
          }
          if (b2 == 0x98 || b2 == 0x99) {
            out += '\'';
            i += 2;
            continue;
          }
          if (b2 == 0x9C || b2 == 0x9D) {
            out += '"';
            i += 2;
            continue;
          }
          if (b2 == 0xA6) {
            out += "...";
            i += 2;
            continue;
          }
        }
      }
    }

    out += '?';
  }

  return out;
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
  String seen[32];
  uint8_t seenCount = 0;
  int pos = 0;
  while (true) {
    int kp = json.indexOf("\"text\"", pos);
    if (kp < 0) break;

    String raw = findJsonStringByKeyNews(json, "\"text\"", kp);
    if (raw.length() > 0) {
      String clean = newsStripHtml(raw);
      clean.trim();
      if (clean.length() > 0) {
        bool duplicate = false;
        for (uint8_t i = 0; i < seenCount; i++) {
          if (seen[i] == clean) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) {
          if (seenCount < 32) seen[seenCount++] = clean;
          if (full.length() > 0) full += "\n\n";
          full += clean;
          if (full.length() > 2200) break;
        }
      }
    }

    pos = kp + 6;
  }

  if (full.length() == 0) full = "(no text)";
  if (full.length() > 2200) full = full.substring(0, 2200);
  return full;
}

