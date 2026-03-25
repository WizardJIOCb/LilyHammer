#include "AppProjects.h"

String projectsShortDate(const String &isoUtc) {
  if (isoUtc.length() < 10) return "--/--";
  String yy = isoUtc.substring(2, 4);
  String mm = isoUtc.substring(5, 7);
  String dd = isoUtc.substring(8, 10);
  return dd + "/" + mm + "/" + yy;
}

String projectsShortTime(const String &isoUtc) {
  if (isoUtc.length() < 19) return "--:--";
  return isoUtc.substring(11, 16);
}

String projectsJsonUnescape(String s) {
  s.replace("\\n", " ");
  s.replace("\\r", " ");
  s.replace("\\t", " ");
  s.replace("\\\"", "\"");
  s.replace("\\/", "/");
  return s;
}

static int findJsonObjectEnd(const String &src, int objStart) {
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

static String findQuotedAfter(const String &src, int quoteStart) {
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

static String findJsonStringByKey(const String &src, const char *key, int fromPos) {
  int seek = fromPos;
  while (true) {
    int kp = src.indexOf(key, seek);
    if (kp < 0) return "";

    int i = kp + (int)strlen(key);
    while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) i++;
    if (i < (int)src.length() && src[i] == ':') {
      i++;
      while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) i++;
      if (i < (int)src.length() && src[i] == '"') return findQuotedAfter(src, i);
    }

    seek = kp + 1;
  }
}

static String findRepoName(const String &event) {
  int repoObj = event.indexOf("\"repo\"");
  if (repoObj < 0) return "";
  return findJsonStringByKey(event, "\"name\"", repoObj);
}

int parseGitHubPushEvents(const String &json, ProjectCommitItem *outItems, uint8_t maxItems) {
  if (!outItems || maxItems == 0) return 0;

  int pos = 0;
  uint8_t count = 0;

  while (count < maxItems) {
    int objStart = json.indexOf('{', pos);
    if (objStart < 0) break;
    int objEnd = findJsonObjectEnd(json, objStart);
    if (objEnd < 0) break;

    String event = json.substring(objStart, objEnd + 1);
    pos = objEnd + 1;

    if (event.indexOf("\"type\":\"PushEvent\"") < 0) continue;

    String repo = findRepoName(event);
    String created = findJsonStringByKey(event, "\"created_at\"", 0);

    int payloadAnchor = event.indexOf("\"payload\"");
    String msg = "";
    String sha = "";
    if (payloadAnchor >= 0) {
      int payloadObjStart = event.indexOf('{', payloadAnchor);
      int payloadObjEnd = findJsonObjectEnd(event, payloadObjStart);
      String payload = "";
      if (payloadObjStart >= 0 && payloadObjEnd > payloadObjStart) {
        payload = event.substring(payloadObjStart, payloadObjEnd + 1);
      }

      int commitsAnchor = payload.indexOf("\"commits\"");
      if (commitsAnchor >= 0) {
        int commitObjStart = payload.indexOf('{', commitsAnchor);
        int commitObjEnd = findJsonObjectEnd(payload, commitObjStart);
        if (commitObjStart >= 0 && commitObjEnd > commitObjStart) {
          String firstCommit = payload.substring(commitObjStart, commitObjEnd + 1);
          msg = findJsonStringByKey(firstCommit, "\"message\"", 0);
          sha = findJsonStringByKey(firstCommit, "\"sha\"", 0);
        }
      }
      if (msg.length() == 0) msg = findJsonStringByKey(payload, "\"message\"", 0);
      if (sha.length() == 0) sha = findJsonStringByKey(payload, "\"head\"", 0);
    }
    if (msg.length() == 0) msg = findJsonStringByKey(event, "\"message\"", 0);

    if (repo.length() == 0) repo = "(unknown repo)";
    if (msg.length() == 0) msg = "(no message)";
    else msg = projectsJsonUnescape(msg);
    if (msg.length() > 64) msg = msg.substring(0, 64);

    outItems[count].repo = repo;
    outItems[count].date = projectsShortDate(created);
    outItems[count].time = projectsShortTime(created);
    outItems[count].message = msg;
    outItems[count].sha = sha;
    count++;
  }

  return count;
}

