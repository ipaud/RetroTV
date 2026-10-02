// Host tests: teletext rows, episode titles, page order, and the guide's airings.

#include <string>

#include "check.h"
#include "media/OnAir.h"
#include "teletext/Teletext.h"

namespace {

// What a row shows, control bytes left out.
std::string cellsOf(const char* row) {
  tt::Cell cells[tt::COLS];
  tt::decodeRow(row, cells);
  std::string s;
  for (const tt::Cell& c : cells) s += c.c;
  return s;
}

std::string title(const char* path) {
  char out[48];
  tt::episodeTitle(path, out, sizeof(out));
  return out;
}

void testRowNeverOverflows() {
  char buf[tt::ROW_BYTES];
  tt::Row r(buf);
  r.fg(tt::Color::Yellow).text("0123456789012345678901234567890123456789");
  CHECK(r.cols() == tt::COLS);
  CHECK(cellsOf(buf) == "01234567890123456789012345");
  tt::Row two(buf);  // the limit holds across calls too
  two.text("ABCDEFGHIJKLMNOPQRST").padTo(24).text("UVWXYZ");
  CHECK(two.cols() == tt::COLS && cellsOf(buf).substr(20) == "    UV");

  tt::Row codes(buf);  // colour codes take no cell, and never run past the buffer
  for (int i = 0; i < 100; ++i) codes.fg(tt::Color::Red);
  CHECK(strlen(buf) < tt::ROW_BYTES);
  codes.text("AB");  // no room left: nothing is written past the end
  CHECK(strlen(buf) < tt::ROW_BYTES);

  tt::Row odd(buf);
  odd.text("A\tB\xC3\xA9");  // tab and UTF-8 bytes are not printable here
  CHECK(cellsOf(buf).substr(0, 5) == "A?B??");
}

void testRowAlignment() {
  char buf[tt::ROW_BYTES];
  tt::Row(buf).text(" ARA").fg(tt::Color::Cyan).right("12'");
  const std::string s = cellsOf(buf);
  CHECK(s.substr(0, 4) == " ARA");
  CHECK(s.substr(tt::COLS - 3) == "12'");

  tt::Row(buf).text("ABC", 2).padTo(5).text("D");
  CHECK(cellsOf(buf).substr(0, 7) == "AB   D ");
}

void testDecodeColoursAndHeight() {
  char buf[tt::ROW_BYTES];
  tt::Row(buf).doubleHeight().bg(tt::Color::Blue).fg(tt::Color::Yellow).text("PAU");
  tt::Cell cells[tt::COLS];
  CHECK(tt::decodeRow(buf, cells));
  CHECK(cells[0].c == 'P' && cells[0].fg == tt::Color::Yellow && cells[0].bg == tt::Color::Blue);
  CHECK(cells[tt::COLS - 1].c == ' ' && cells[tt::COLS - 1].bg == tt::Color::Blue);  // band to the edge

  tt::Row(buf).text("X");
  CHECK(!tt::decodeRow(buf, cells));
  CHECK(cells[0].fg == tt::Color::White && cells[0].bg == tt::Color::Black);  // every row starts afresh
}

void testMinutesLabel() {
  char out[12];
  tt::minutesLabel(0, out, sizeof(out));
  CHECK(std::string(out) == "0'");
  tt::minutesLabel(1, out, sizeof(out));
  CHECK(std::string(out) == "1'");  // rounded up: never "0'" while something is left
  tt::minutesLabel(60000, out, sizeof(out));
  CHECK(std::string(out) == "1'");
  tt::minutesLabel(60001, out, sizeof(out));
  CHECK(std::string(out) == "2'");
  tt::minutesLabel(65ull * 60000, out, sizeof(out));
  CHECK(std::string(out) == "1H05'");
}

// Names as tools/convert_video.sh leaves them on the card.
void testEpisodeTitles() {
  CHECK(title("/m/cowboy_bebop/cowboy_bebop_-01_el_blues_de_l_asteroide_per_somesite_com.mjpeg") ==
        "01 EL BLUES DE L ASTEROIDE");
  CHECK(title("/m/cowboy_bebop/cowboy_bebop_-02-_el_gos_del_carrer_per_somesite.com.mjpeg") ==
        "02 EL GOS DEL CARRER");
  CHECK(title("/m/hattori_el_ninja/hattori_-008-_l_alumne_nou_tdtrip_by_someone.mjpeg") == "08 L ALUMNE NOU");
  CHECK(title("/m/hattori_el_ninja/hattori_-001.mjpeg") == "01");
  CHECK(title("/m/dr_slump/dr_slump_01_per_somesite.com.mjpeg") == "01");
  CHECK(title("/m/bola_de_drac/db-003_per_somesite.mjpeg") == "03");
  CHECK(title("/m/musculman/musculman_-_001_dvdrip_cat_ver2_by_someone.mjpeg") == "01");
  CHECK(title("/m/samurai_champloo/samurai_champloo_01_caracters_tempestuosos_cat_cast_jap.mjpeg") ==
        "01 CARACTERS TEMPESTUOSOS");
  CHECK(title("/m/sakura/12_card_captor_sakura.mjpeg") == "12 CARD CAPTOR SAKURA");
  CHECK(title("/m/channel01/dragon_ball_kai_123_x.mjpeg") == "123 X");
  CHECK(title("/retrotv/media/demo/demo.mjpeg") == "DEMO");  // no number: the name itself
  CHECK(title("") == "");

  char tiny[5];
  tt::episodeTitle("/m/ranma_002_-_a_l_escola.mjpeg", tiny, sizeof(tiny));
  CHECK(std::string(tiny) == "02 A");  // cut, always terminated
}

void testPageOrder() {
  const uint16_t numbers[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  CHECK(tt::viewCount(15) == 1 + 3 + 15);
  tt::View v = tt::viewAt(0, numbers, 15);
  CHECK(v.page == 100 && v.channel == -1);
  v = tt::viewAt(1, numbers, 15);
  CHECK(v.page == 101 && v.sub == 1 && v.subs == 3);
  v = tt::viewAt(3, numbers, 15);
  CHECK(v.page == 101 && v.sub == 3);
  v = tt::viewAt(4, numbers, 15);
  CHECK(v.page == 201 && v.channel == 0);
  v = tt::viewAt(18, numbers, 15);
  CHECK(v.page == 215 && v.channel == 14);
  CHECK(tt::viewAt(19, numbers, 15).page == 100);  // wraps
  CHECK(tt::viewForPage(203, numbers, 15) == 6);
  CHECK(tt::viewForPage(101, numbers, 15) == 1);
  CHECK(tt::viewForPage(299, numbers, 15) == -1);

  CHECK(tt::viewCount(0) == 2);  // no local channels: index + an empty "ara en emissio"
  CHECK(tt::viewAt(1, numbers, 0).page == 101 && tt::viewAt(2, numbers, 0).page == 100);
  CHECK(tt::viewCount(5) == 1 + 1 + 5);
}

void testUpcomingAirings() {
  const uint32_t d[] = {1000, 2000, 3000};  // programme: 6 s
  Airing a[4];
  CHECK(upcomingAirings(6000u * 1000 + 1500, d, 3, a, 4) == 4);  // 1.5 s into a loop
  CHECK(a[0].episode == 1 && a[0].startMs == 6000u * 1000 + 1000 && a[0].durationMs == 2000);
  CHECK(a[1].episode == 2 && a[1].startMs == 6000u * 1000 + 3000);
  CHECK(a[2].episode == 0 && a[2].startMs == 6000u * 1000 + 6000);  // the loop starts again
  CHECK(a[3].episode == 1 && a[3].startMs == 6000u * 1000 + 7000);

  const uint32_t gap[] = {1000, 0, 500};  // zero-length episodes never air
  CHECK(upcomingAirings(0, gap, 3, a, 3) == 3);
  CHECK(a[0].episode == 0 && a[1].episode == 2 && a[1].startMs == 1000 && a[2].episode == 0 &&
        a[2].startMs == 1500);

  CHECK(upcomingAirings(5, d, 0, a, 4) == 0);
  const uint32_t none[] = {0, 0};
  CHECK(upcomingAirings(5, none, 2, a, 4) == 0);
}

}  // namespace

void runTeletextTests() {
  testRowNeverOverflows();
  testRowAlignment();
  testDecodeColoursAndHeight();
  testMinutesLabel();
  testEpisodeTitles();
  testPageOrder();
  testUpcomingAirings();
}
