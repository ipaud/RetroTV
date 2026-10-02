// Teletext channel (internal source "teletext"): P100 index, P101 "ara en emissio" (what every
// channel is showing and how long is left) and P2NN, the guide of channel NN. Local channels:
// from their on-air schedules, so the guide matches what they play. Live channels: from the
// server's guide (GET /api/guide, for the sources that publish one), "EN DIRECTE" otherwise.
// Internal channels: what they are. Pages turn by themselves; VOLUME or a tap turns them by
// hand. Silent, like the real thing.

#include <time.h>

#include "app/App.h"
#include "config.h"
#include "media/OnAir.h"

namespace {

using tt::Color;

constexpr size_t PAGE_AIRINGS = 5;  // channel page: on now + the next four
constexpr int TITLE_ROW = 1;         // double-height band over rows 1-2
constexpr int FOOTER_ROW = tt::ROWS - 1;
constexpr int PAIR_CODE_ROW = FOOTER_ROW - 2;  // double height, just over the footer
constexpr int PROGRESS_CELLS = tt::COLS - 2;
constexpr size_t TITLE_LEN = 48;

constexpr const char* DAYS[] = {"DIUMENGE", "DILLUNS", "DIMARTS", "DIMECRES", "DIJOUS", "DIVENDRES", "DISSABTE"};
constexpr const char* MONTHS[] = {"DE GENER",   "DE FEBRER", "DE MARC",     "D'ABRIL",   "DE MAIG",     "DE JUNY",
                                  "DE JULIOL",  "D'AGOST",   "DE SETEMBRE", "D'OCTUBRE", "DE NOVEMBRE", "DE DESEMBRE"};
constexpr Color RAINBOW[] = {Color::Red, Color::Yellow, Color::Green, Color::Cyan, Color::Blue, Color::Magenta};
constexpr int RAINBOW_COUNT = sizeof(RAINBOW) / sizeof(RAINBOW[0]);

// When an airing starts: "21:55" with the real time; before NTP, "+12'" from now.
void startLabel(uint64_t startMs, uint64_t nowMs, bool ntp, char* out, size_t len) {
  if (!ntp) {
    out[0] = '+';
    tt::minutesLabel(startMs > nowMs ? startMs - nowMs : 0, out + 1, len - 1);
    return;
  }
  const time_t t = static_cast<time_t>(startMs / 1000);
  tm local;
  localtime_r(&t, &local);
  strftime(out, len, "%H:%M", &local);
}

void titleBand(tt::Page& page, const char* text) {
  tt::Row(page.rows[TITLE_ROW]).doubleHeight().bg(Color::Blue).fg(Color::Yellow).text(" ").text(text);
}

// One programme of a channel page, local or live.
struct GuideLine {
  uint64_t startMs;
  uint64_t endMs;
  char title[TITLE_LEN];
};

// The programme on now (minutes left, progress bar) and what follows.
void composeGuide(tt::Page& page, const GuideLine* lines, size_t count, uint64_t nowMs, bool ntp) {
  const GuideLine& now = lines[0];
  char label[12];
  char left[20];
  tt::minutesLabel(now.endMs - nowMs, label, sizeof(label));
  snprintf(left, sizeof(left), "QUEDEN %s", label);
  tt::Row(page.rows[4]).fg(Color::Cyan).text(" ARA").fg(Color::White).right(left);
  tt::Row(page.rows[5]).text(" ").text(now.title);
  const uint64_t into = nowMs > now.startMs ? nowMs - now.startMs : 0;
  const int done = static_cast<int>(into * PROGRESS_CELLS / (now.endMs - now.startMs));
  tt::Row(page.rows[6]).text(" ").fg(Color::Green).repeat(tt::BLOCK, done).fg(Color::Blue).repeat(tt::BLOCK, PROGRESS_CELLS - done);

  if (count < 2) return;
  tt::Row(page.rows[8]).fg(Color::Cyan).text(" A CONTINUACIO");
  for (size_t k = 1; k < count; ++k) {
    startLabel(lines[k].startMs, nowMs, ntp, label, sizeof(label));
    tt::Row(page.rows[8 + k]).text(" ").fg(Color::Yellow).text(label).padTo(8).fg(Color::White).text(lines[k].title);
  }
}

bool isTeletext(const Channel& c) { return c.type == ChannelType::Internal && strcmp(c.source, INTERNAL_TELETEXT) == 0; }

// What an internal channel is, for the web remote's guide (Spanish, like the page).
const char* webDescription(const Channel& c) {
  if (strcmp(c.source, INTERNAL_TESTCARD) == 0) return "CARTA DE AJUSTE";
  if (strcmp(c.source, INTERNAL_REMOTE_QR) == 0) return "QR DEL MANDO";
  return "CANAL DEL SISTEMA";
}

// What an internal channel is, for the guide.
const char* internalDescription(const Channel& c) {
  if (strcmp(c.source, INTERNAL_TESTCARD) == 0) return "CARTA D'AJUST";
  if (strcmp(c.source, INTERNAL_REMOTE_QR) == 0) return "QR DEL COMANDAMENT";
  return "CANAL DEL SISTEMA";
}

#if PAUTV_DEBUG_STATS
// The page as text on the serial log, so the guide can be checked without looking at the panel.
void logPage(const tt::Page& page) {
  for (int r = 0; r < tt::ROWS; ++r) {
    tt::Cell cells[tt::COLS];
    tt::decodeRow(page.rows[r], cells);
    char text[tt::COLS + 1];
    for (int c = 0; c < tt::COLS; ++c) text[c] = cells[c].c == tt::BLOCK ? '#' : cells[c].c;
    text[tt::COLS] = '\0';
    PLOG("TELETEXT", "|%s|", text);
  }
}
#endif

}  // namespace

void App::startTeletext() {
  playMode_ = PlayMode::Teletext;
  guideAsked_ = false;  // the live channels' guide: asked again at once
  teletextView_ = 0;
  teletextByHand_ = false;
  teletextTurnedMs_ = millis();
  publishTeletext(true);  // the page first: the new screen finds it ready
  ui_.publish(UiState(Screen::Teletext));
}

void App::updateTeletext(uint32_t nowMs) {
  const bool built = pollRemoteGuide(nowMs) | buildNextSchedule();
  const uint32_t dwell = teletextByHand_ ? TELETEXT_BY_HAND_MS : TELETEXT_PAGE_MS;
  // Signed: a page turned by hand earlier in this loop pass is stamped after nowMs.
  if (static_cast<int32_t>(nowMs - teletextTurnedMs_) >= static_cast<int32_t>(dwell)) {
    turnPage(1, false);
  } else if (built || static_cast<int32_t>(nowMs - teletextPublishedMs_) >= static_cast<int32_t>(TELETEXT_REFRESH_MS)) {
    publishTeletext(false);
  }
}

bool App::onTeletextInput(InputEvent e) {
  switch (e) {
    case InputEvent::VolUp:
    case InputEvent::ToggleOsd:  // a tap
      turnPage(1, true);
      return true;
    case InputEvent::VolDown:
      turnPage(-1, true);
      return true;
    case InputEvent::ChNext:
    case InputEvent::ChPrev:
    case InputEvent::Mute:
    case InputEvent::Menu:
    case InputEvent::Power:  // App::onInput: standby, before any state
    case InputEvent::None:
      break;
  }
  return false;
}

void App::turnPage(int step, bool byHand) {
  uint8_t slots[MAX_CHANNELS];
  uint16_t numbers[MAX_CHANNELS];
  const size_t count = tt::viewCount(guideChannels(slots, numbers));
  const size_t view = teletextView_ % count;
  teletextView_ = step > 0 ? (view + 1) % count : (view + count - 1) % count;
  teletextByHand_ = byHand;
  teletextTurnedMs_ = millis();
  publishTeletext(true);
}

void App::teletextGoTo(uint16_t page) {
  if (playMode_ != PlayMode::Teletext) {
    PLOG("TELETEXT", "not on the teletext channel");
    return;
  }
  uint8_t slots[MAX_CHANNELS];
  uint16_t numbers[MAX_CHANNELS];
  const int view = tt::viewForPage(page, numbers, guideChannels(slots, numbers));
  if (view < 0) {
    PLOG("TELETEXT", "no page %u", page);
    return;
  }
  teletextView_ = static_cast<size_t>(view);
  teletextByHand_ = true;
  teletextTurnedMs_ = millis();
  publishTeletext(true);
}

// The local channels' programmes for the guides, one at a time on the builder's task: the page
// is up at once and the guide fills in behind it. True when one was just adopted. A channel
// tuned in meanwhile builds its own (AppPlayback): then the builder's copy is dropped.
bool App::buildNextSchedule() {
  uint8_t slot = 0;
  if (builder_.done(slot)) {
    const bool wanted = schedules_[slot].state() == ChannelSchedule::State::Unbuilt;
    if (wanted) schedules_[slot].adopt(builder_.result());
    builder_.release();
    return wanted;
  }
  if (builder_.busy() || !storage_.mounted()) return false;  // no card: leave them for when it is back
  for (size_t i = 0; i < channels_.count(); ++i) {
    const Channel& c = channels_.at(i);
    if (!c.enabled || c.type != ChannelType::Local) continue;
    if (schedules_[i].state() != ChannelSchedule::State::Unbuilt) continue;
    builder_.request(static_cast<uint8_t>(i), c);
    return false;
  }
  return false;
}

// The server's guide, asked for through the first live channel (every live channel is on one
// server in practice; others show "EN DIRECTE"). On entering the teletext, then once a minute.
// True when a new one arrived.
bool App::pollRemoteGuide(uint32_t nowMs) {
  if (remoteGuide_ == nullptr) return false;
  bool arrived = false;
  const char* json;
  size_t len;
  if (remote_.takeGuide(json, len)) {
    arrived = true;
    guideAtMs_ = nowMs;
    if (!parseGuide(json, len, *remoteGuide_)) PLOG("TELETEXT", "the server's guide is unreadable");
  }
  if ((guideAsked_ && nowMs - guideAskedMs_ < TELETEXT_GUIDE_MS) || !wifi_.online()) return arrived;
  for (size_t i = 0; i < channels_.count(); ++i) {
    const Channel& c = channels_.at(i);
    if (!c.enabled || c.type != ChannelType::Remote) continue;
    snprintf(guideServer_, sizeof(guideServer_), "%s", c.source);
    remote_.requestGuide(c.source);
    guideAsked_ = true;
    guideAskedMs_ = nowMs;
    break;
  }
  return arrived;
}

// A live channel's airings in the server's guide that have not ended, and the server's now.
size_t App::liveAirings(const Channel& c, const GuideAiring** out, size_t max, uint64_t& nowMs) const {
  uint16_t number = 0;
  if (remoteGuide_ == nullptr || remoteGuide_->count == 0 || !sameServer(c.source, guideServer_) ||
      !serverChannelNumber(c.source, number)) {
    return 0;
  }
  const GuideChannel* g = remoteGuide_->find(number);
  if (g == nullptr) return 0;
  nowMs = remoteGuide_->serverNowMs + (millis() - guideAtMs_);
  size_t n = 0;
  for (size_t k = 0; k < g->count && n < max; ++k) {
    if (g->airings[k].endMs > nowMs) out[n++] = &g->airings[k];
  }
  return n;
}

// Every enabled channel but this one, in channel order: their slot in channels_ and number.
size_t App::guideChannels(uint8_t* slots, uint16_t* numbers) const {
  size_t n = 0;
  for (size_t i = 0; i < channels_.count(); ++i) {
    const Channel& c = channels_.at(i);
    if (!c.enabled || isTeletext(c)) continue;
    slots[n] = static_cast<uint8_t>(i);
    numbers[n] = c.number;
    ++n;
  }
  return n;
}

// `turned`: a new page is up (logged), not just the clock ticking on the same one.
void App::publishTeletext(bool turned) {
  teletextPublishedMs_ = millis();
  uint8_t slots[MAX_CHANNELS];
  uint16_t numbers[MAX_CHANNELS];
  const size_t count = guideChannels(slots, numbers);
  const tt::View v = tt::viewAt(teletextView_, numbers, count);
  uint64_t nowMs = 0;
  const bool ntp = onAirNow(nowMs);

  tt::Page page;
  for (char* row : page.rows) row[0] = '\0';
  char clock[12];
  if (!clockText(clock, sizeof(clock), "%H:%M:%S")) snprintf(clock, sizeof(clock), "--:--:--");
  char number[8];
  snprintf(number, sizeof(number), "P%u", v.page);
  tt::Row(page.rows[0]).text(number).text("  ").fg(Color::Cyan).text("RETROTV").fg(Color::Yellow).right(clock);

  if (v.page == tt::PAGE_INDEX) {
    composeIndexPage(page, numbers, count, ntp);
  } else if (v.page == tt::PAGE_NOW) {
    composeNowPage(page, v, slots, count, nowMs);
  } else {
    composeChannelPage(page, slots[v.channel], nowMs, ntp);
  }

  if (pairCode_ != 0 && static_cast<int32_t>(millis() - pairCodeUntilMs_) < 0) {
    char code[24];
    snprintf(code, sizeof(code), " CODI DEL MANDO %04u", static_cast<unsigned>(pairCode_ % 10000u));
    page.rows[PAIR_CODE_ROW][0] = '\0';
    page.rows[PAIR_CODE_ROW + 1][0] = '\0';
    tt::Row(page.rows[PAIR_CODE_ROW]).doubleHeight().bg(Color::Red).fg(Color::White).text(code);
  }

  tt::Row footer(page.rows[FOOTER_ROW]);
  footer.fg(Color::Red).text("100 INICI").padTo(11).fg(Color::Green).text("101 ARA");
  if (v.subs > 1) {
    char sub[8];
    snprintf(sub, sizeof(sub), "%u/%u", v.sub, v.subs);
    footer.fg(Color::Cyan).right(sub);
  }
  ui_.publishTeletext(page);

  if (!turned) return;
  PLOG("TELETEXT", "P%u %u/%u%s", v.page, v.sub, v.subs, teletextByHand_ ? " (by hand)" : "");
#if PAUTV_DEBUG_STATS
  logPage(page);
#endif
}

void App::composeIndexPage(tt::Page& page, const uint16_t* numbers, size_t count, bool ntp) const {
  tt::Row(page.rows[TITLE_ROW]).doubleHeight().bg(Color::Blue).fg(Color::Yellow).text("  RETROTV").fg(Color::White).text(
      "    TELETEXT");
  tt::Row stripe(page.rows[3]);
  for (int k = 0; k < RAINBOW_COUNT; ++k) {
    stripe.fg(RAINBOW[k]).repeat(tt::BLOCK, (k + 1) * tt::COLS / RAINBOW_COUNT - k * tt::COLS / RAINBOW_COUNT);
  }

  tt::Row date(page.rows[5]);
  date.fg(Color::Cyan);
  const time_t now = time(nullptr);
  tm local;
  if (ntp && localtime_r(&now, &local) != nullptr) {
    char text[40];
    snprintf(text, sizeof(text), " %s %u %s", DAYS[local.tm_wday], static_cast<unsigned>(local.tm_mday),
             MONTHS[local.tm_mon]);
    date.text(text);
  } else {
    date.text(" SENSE HORA (SENSE WI-FI)");
  }

  tt::Row(page.rows[7]).fg(Color::Yellow).text(" 101").fg(Color::White).text(" ARA EN EMISSIO");
  tt::Row(page.rows[8]).fg(Color::Yellow).text(" 2NN").fg(Color::White).text(" GUIA DEL CANAL NN");
  if (count > 0) {
    char example[32];
    snprintf(example, sizeof(example), "     EX: %u = CANAL %u", tt::PAGE_CHANNEL_BASE + numbers[0], numbers[0]);
    tt::Row(page.rows[9]).fg(Color::Green).text(example);
  }
  tt::Row(page.rows[11]).fg(Color::Cyan).text(" VOLUM: CANVIA DE PAGINA");
  tt::Row(page.rows[12]).fg(Color::Cyan).text(" LES PAGINES PASSEN SOLES");
}

// Two rows per channel: number and name, then the programme on now and the minutes left.
void App::composeNowPage(tt::Page& page, const tt::View& v, const uint8_t* slots, size_t count,
                         uint64_t nowMs) const {
  titleBand(page, "ARA EN EMISSIO");
  if (count == 0) {
    tt::Row(page.rows[4]).text(" NO HI HA CANALS");
    return;
  }
  const size_t first = static_cast<size_t>(v.sub - 1) * tt::NOW_PER_PAGE;
  for (size_t j = 0; j < static_cast<size_t>(tt::NOW_PER_PAGE) && first + j < count; ++j) {
    const uint8_t slot = slots[first + j];
    const Channel& ch = channels_.at(slot);
    char number[8];
    snprintf(number, sizeof(number), "%02u ", ch.number);
    tt::Row(page.rows[3 + 2 * j]).fg(Color::Yellow).text(number).fg(Color::White).text(ch.name);

    tt::Row now(page.rows[4 + 2 * j]);
    now.text("   ");
    composeNowLine(now, slot, nowMs);
  }
  tt::Row(page.rows[13]).fg(Color::Cyan).text(" ' = MINUTS QUE QUEDEN");
}

void App::composeNowLine(tt::Row& row, uint8_t slot, uint64_t nowMs) const {
  const Channel& ch = channels_.at(slot);
  char title[TITLE_LEN];
  char left[12];
  if (ch.type == ChannelType::Internal) {
    row.fg(Color::Cyan).text(internalDescription(ch));
    return;
  }
  if (ch.type == ChannelType::Remote) {
    const GuideAiring* a = nullptr;
    uint64_t liveNow = 0;
    if (liveAirings(ch, &a, 1, liveNow) == 0) {
      row.fg(Color::Magenta).text("EN DIRECTE");
      return;
    }
    tt::minutesLabel(a->endMs - liveNow, left, sizeof(left));
    row.fg(Color::Green).text(a->title, tt::COLS - 4 - static_cast<int>(strlen(left))).fg(Color::Cyan).right(left);
    return;
  }
  const ChannelSchedule& s = schedules_[slot];
  Airing a;
  if (s.state() == ChannelSchedule::State::Unbuilt) {
    row.fg(Color::Cyan).text("CERCANT...");
  } else if (upcomingAirings(nowMs, s.durations(), s.count(), &a, 1) == 0) {
    row.fg(Color::Red).text("SENSE GUIA");
  } else {
    tt::episodeTitle(s.path(a.episode), title, sizeof(title));
    tt::minutesLabel(a.startMs + a.durationMs - nowMs, left, sizeof(left));
    row.fg(Color::Green).text(title, tt::COLS - 4 - static_cast<int>(strlen(left))).fg(Color::Cyan).right(left);
  }
}

// The programme on now, how far into it, and what follows.
void App::composeChannelPage(tt::Page& page, uint8_t slot, uint64_t nowMs, bool ntp) const {
  const Channel& ch = channels_.at(slot);
  char band[CHANNEL_NAME_LEN + 8];
  snprintf(band, sizeof(band), "%02u %s", ch.number, ch.name);
  titleBand(page, band);
  GuideLine lines[PAGE_AIRINGS];

  if (ch.type == ChannelType::Internal) {
    tt::Row(page.rows[5]).fg(Color::Cyan).text(" ").text(internalDescription(ch));
    return;
  }
  if (ch.type == ChannelType::Remote) {
    const GuideAiring* a[PAGE_AIRINGS];
    uint64_t liveNow = 0;
    const size_t n = liveAirings(ch, a, PAGE_AIRINGS, liveNow);
    if (n == 0) {
      tt::Row(page.rows[5]).fg(Color::Magenta).text(" EN DIRECTE");
      tt::Row(page.rows[6]).text(" SENSE GUIA DEL PROGRAMA");
      return;
    }
    for (size_t k = 0; k < n; ++k) {
      lines[k].startMs = a[k]->startMs;
      lines[k].endMs = a[k]->endMs;
      snprintf(lines[k].title, sizeof(lines[k].title), "%s", a[k]->title);
    }
    composeGuide(page, lines, n, liveNow, ntp);
    return;
  }

  const ChannelSchedule& s = schedules_[slot];
  Airing a[PAGE_AIRINGS];
  if (s.state() == ChannelSchedule::State::Unbuilt) {
    tt::Row(page.rows[5]).fg(Color::Cyan).text(" CERCANT LA PROGRAMACIO");
    return;
  }
  const size_t n = upcomingAirings(nowMs, s.durations(), s.count(), a, PAGE_AIRINGS);
  if (n == 0) {
    tt::Row(page.rows[5]).fg(Color::Red).text(" SENSE GUIA (FALTA .IDX)");
    tt::Row(page.rows[6]).text(" CAPITOLS A L'ATZAR");
    return;
  }
  for (size_t k = 0; k < n; ++k) {
    lines[k].startMs = a[k].startMs;
    lines[k].endMs = a[k].startMs + a[k].durationMs;
    tt::episodeTitle(s.path(a[k].episode), lines[k].title, sizeof(lines[k].title));
  }
  composeGuide(page, lines, n, nowMs, ntp);
}

// The guide for the web remote, only while a phone looks at it (WebRemote::guideWanted). The
// local channels' programmes are read one per step: each is a short burst of .idx reads on the
// card, which the read-ahead rides out. The JSON is made again when one is ready, when the
// server's guide arrives, or every WEB_GUIDE_REFRESH_MS.
void App::pollWebGuide(uint32_t nowMs) {
  if (guideRows_ == nullptr || !web_.guideWanted(nowMs)) return;
  bool changed = pollRemoteGuide(nowMs);
  if (nowMs - webGuideStepMs_ >= WEB_GUIDE_STEP_MS) {
    webGuideStepMs_ = nowMs;
    changed = buildNextSchedule() || changed;
  }
  if (!changed && webGuideMs_ != 0 && nowMs - webGuideMs_ < WEB_GUIDE_REFRESH_MS) return;
  webGuideMs_ = nowMs | 1u;
  publishWebGuide();
}

// Every enabled channel but the teletext, like its pages: the programme on now and what follows.
void App::publishWebGuide() {
  uint64_t nowMs = 0;
  onAirNow(nowMs);
  size_t n = 0;
  for (size_t i = 0; i < channels_.count() && n < MAX_CHANNELS; ++i) {
    const Channel& c = channels_.at(i);
    if (!c.enabled || isTeletext(c)) continue;
    GuideRow& r = guideRows_[n++];
    r = GuideRow{};
    r.number = c.number;
    snprintf(r.name, sizeof(r.name), "%s", c.name);
    r.logo = i < 64 && ((logoMask_ >> i) & 1ull);
    if (c.type == ChannelType::Internal) {
      r.type = "internal";
      r.note = webDescription(c);
    } else if (c.type == ChannelType::Remote) {
      r.type = "remote";
      const GuideAiring* a[GUIDE_ITEMS];
      uint64_t liveNow = 0;
      const size_t k = liveAirings(c, a, GUIDE_ITEMS, liveNow);
      for (size_t j = 0; j < k; ++j) {
        r.items[j].startS = static_cast<uint32_t>(a[j]->startMs / 1000);
        r.items[j].endS = static_cast<uint32_t>(a[j]->endMs / 1000);
        snprintf(r.items[j].title, sizeof(r.items[j].title), "%s", a[j]->title);
      }
      r.count = static_cast<uint8_t>(k);
      if (k == 0) r.note = "EN DIRECTO";
    } else if (schedules_[i].state() == ChannelSchedule::State::Unbuilt) {
      r.note = "BUSCANDO...";
    } else {
      const ChannelSchedule& s = schedules_[i];
      Airing a[GUIDE_ITEMS];
      const size_t k = upcomingAirings(nowMs, s.durations(), s.count(), a, GUIDE_ITEMS);
      for (size_t j = 0; j < k; ++j) {
        r.items[j].startS = static_cast<uint32_t>(a[j].startMs / 1000);
        r.items[j].endS = static_cast<uint32_t>((a[j].startMs + a[j].durationMs) / 1000);
        tt::episodeTitle(s.path(a[j].episode), r.items[j].title, sizeof(r.items[j].title));
      }
      r.count = static_cast<uint8_t>(k);
      if (k == 0) r.note = "SIN GUIA";
    }
  }
  size_t cap = 0;
  char* out = web_.guideScratch(cap);
  if (out == nullptr) return;
  if (writeGuideJson(guideRows_, n, static_cast<uint32_t>(nowMs / 1000), out, cap) == 0) {
    PLOG("WEB", "the guide does not fit its buffer");
    return;
  }
  web_.publishGuide();
}
