#include "channels/ChannelManager.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "network/RemoteProtocol.h"

namespace {

struct TypeName {
  const char* name;
  ChannelType type;
};

constexpr TypeName TYPE_NAMES[] = {
    {"local", ChannelType::Local},       {"internal", ChannelType::Internal},
    {"remote", ChannelType::Remote},     {"hls-proxy", ChannelType::HlsProxy},
    {"tunarr", ChannelType::Tunarr},     {"stream", ChannelType::Stream},
};

bool parseType(const char* s, ChannelType& out) {
  for (const TypeName& t : TYPE_NAMES) {
    if (strcmp(s, t.name) == 0) {
      out = t.type;
      return true;
    }
  }
  return false;
}

// Returns why the entry is unusable, or nullptr when `ch` was filled.
const char* parseChannel(JsonVariantConst v, Channel& ch) {
  if (!v.is<JsonObjectConst>()) return "not an object";
  const char* id = v["id"].as<const char*>();
  const char* name = v["name"].as<const char*>();
  const char* type = v["type"].as<const char*>();
  const char* source = v["source"].as<const char*>();
  const JsonVariantConst enabled = v["enabled"];

  if (id == nullptr || id[0] == '\0') return "missing id";
  if (strlen(id) >= CHANNEL_ID_LEN) return "id too long";
  if (!v["number"].is<int>()) return "missing number";
  const int number = v["number"].as<int>();
  if (number < 0 || number > CHANNEL_NUMBER_MAX) return "number must be 0-999";
  if (name == nullptr || name[0] == '\0') return "missing name";
  if (type == nullptr) return "missing type";
  if (!parseType(type, ch.type)) return "unknown type";
  if (source == nullptr || source[0] == '\0') return "missing source";
  if (strlen(source) >= CHANNEL_SOURCE_LEN) return "source too long";
  if (!enabled.isNull() && !enabled.is<bool>()) return "enabled must be true or false";
  if (ch.type == ChannelType::Local && source[0] != '/') return "local source must start with /";
  if (ch.type == ChannelType::Remote && !isRemoteChannelUrl(source)) {
    return "remote source must be http://<server>/channel/<n>";
  }
  if (ch.type == ChannelType::Internal && strcmp(source, INTERNAL_TESTCARD) != 0 &&
      strcmp(source, INTERNAL_TELETEXT) != 0 && strcmp(source, INTERNAL_REMOTE_QR) != 0) {
    return "unknown internal source";
  }

  snprintf(ch.id, sizeof(ch.id), "%s", id);
  ch.number = static_cast<uint16_t>(number);
  toOsdText(name, ch.name, sizeof(ch.name));
  snprintf(ch.source, sizeof(ch.source), "%s", source);
  ch.enabled = enabled.isNull() || enabled.as<bool>();
  return nullptr;
}

void note(LoadReport& r, const char* fmt, int index, const char* why) {
  if (r.firstProblem[0] == '\0') snprintf(r.firstProblem, sizeof(r.firstProblem), fmt, index, why);
}

// Latin-1 supplement U+00C0..U+00FF (UTF-8 C3 80..C3 BF) folded to one ASCII character.
constexpr char LATIN1_FOLD[] = "AAAAAAACEEEEIIIIDNOOOOOXOUUUUYPSAAAAAAACEEEEIIIIDNOOOOO/OUUUUYPY";

int utf8Length(unsigned char lead) {
  if (lead >= 0xF0) return 4;
  if (lead >= 0xE0) return 3;
  if (lead >= 0xC0) return 2;
  return 1;
}

}  // namespace

const char* channelTypeName(ChannelType t) {
  for (const TypeName& n : TYPE_NAMES) {
    if (n.type == t) return n.name;
  }
  return "?";
}

void toOsdText(const char* utf8, char* out, size_t outLen) {
  if (outLen == 0) return;
  const auto* p = reinterpret_cast<const unsigned char*>(utf8);
  size_t o = 0;
  while (*p != '\0' && o + 1 < outLen) {
    char c = '?';
    if (*p < 0x80) {
      c = (*p >= 'a' && *p <= 'z') ? static_cast<char>(*p - 'a' + 'A') : static_cast<char>(*p);
      p += 1;
    } else if (p[0] == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
      c = LATIN1_FOLD[p[1] - 0x80];
      p += 2;
    } else if (p[0] == 0xC2 && p[1] == 0xB7) {  // middle dot, as in Catalan "l·l"
      c = '.';
      p += 2;
    } else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x98 || p[2] == 0x99)) {  // curly quotes
      c = '\'';
      p += 3;
    } else {  // anything else: one '?' per character, stopping at the end of the string
      const int len = utf8Length(*p);
      ++p;
      for (int i = 1; i < len && (*p & 0xC0) == 0x80; ++i) ++p;
    }
    out[o++] = c;
  }
  out[o] = '\0';
}

bool ChannelManager::load(JsonVariantConst root, LoadReport& report) {
  report = LoadReport{};
  count_ = 0;
  current_ = -1;

  const JsonArrayConst entries = root["channels"].as<JsonArrayConst>();
  if (entries.isNull()) {
    snprintf(report.firstProblem, sizeof(report.firstProblem), "no \"channels\" array");
    return false;
  }

  int index = 0;
  for (const JsonVariantConst entry : entries) {
    ++index;
    Channel ch{};
    const char* why = parseChannel(entry, ch);
    if (why == nullptr && indexOf(ch.number) >= 0) why = "duplicate number";
    if (why == nullptr && count_ >= MAX_CHANNELS) why = "too many channels";
    if (why != nullptr) {
      ++report.skipped;
      note(report, "#%d: %s", index, why);
      continue;
    }
    channels_[count_++] = ch;
  }

  std::sort(channels_, channels_ + count_,
            [](const Channel& a, const Channel& b) { return a.number < b.number; });
  report.loaded = static_cast<uint8_t>(count_);
  if (enabledCount() == 0) {
    if (report.firstProblem[0] == '\0') {
      snprintf(report.firstProblem, sizeof(report.firstProblem), "no enabled channels");
    }
    return false;
  }
  return true;
}

void ChannelManager::loadFallback() {
  Channel ch{};
  snprintf(ch.id, sizeof(ch.id), "%s", INTERNAL_TESTCARD);
  ch.number = 1;
  snprintf(ch.name, sizeof(ch.name), "CARTA DE AJUSTE");
  ch.type = ChannelType::Internal;
  snprintf(ch.source, sizeof(ch.source), "%s", INTERNAL_TESTCARD);
  ch.enabled = true;
  channels_[0] = ch;
  count_ = 1;
  current_ = -1;
}

size_t ChannelManager::enabledCount() const {
  size_t n = 0;
  for (size_t i = 0; i < count_; ++i) n += channels_[i].enabled ? 1 : 0;
  return n;
}

int ChannelManager::indexOf(uint16_t number) const {
  for (size_t i = 0; i < count_; ++i) {
    if (channels_[i].number == number) return static_cast<int>(i);
  }
  return -1;
}

bool ChannelManager::setEnabled(uint16_t number, bool enabled) {
  const int i = indexOf(number);
  if (i < 0) return false;
  channels_[i].enabled = enabled;
  return true;
}

const Channel* ChannelManager::select(uint16_t number) {
  const int i = indexOf(number);
  if (i < 0 || !channels_[i].enabled) return nullptr;
  current_ = i;
  return &channels_[i];
}

const Channel* ChannelManager::selectOrFirst(uint16_t number) {
  if (const Channel* ch = select(number)) return ch;
  current_ = -1;
  return next();
}

const Channel* ChannelManager::step(int direction) {
  const int n = static_cast<int>(count_);
  if (n == 0) return nullptr;
  const int from = current_ >= 0 ? current_ : (direction > 0 ? n - 1 : 0);
  for (int i = 1; i <= n; ++i) {
    const int idx = ((from + direction * i) % n + n) % n;
    if (channels_[idx].enabled) {
      current_ = idx;
      return &channels_[idx];
    }
  }
  return nullptr;
}
