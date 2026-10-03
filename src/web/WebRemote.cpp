#include "web/WebRemote.h"

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <string.h>

#include "app_types.h"
#include "config.h"
#include "network/Mdns.h"
#include "web/RemotePage.h"

namespace {

constexpr size_t CHANNELS_JSON_CAP = 4096;  // 48 channels x ~60 bytes, with room
constexpr size_t STATE_JSON_CAP = 256;
constexpr size_t GUIDE_JSON_CAP = 16 * 1024;  // 48 channels x (name + 4 airings of ~90 bytes)
constexpr size_t QUERY_CAP = 48;
constexpr size_t VALUE_CAP = 16;
constexpr const char* COMMAND_HEADER = "X-RETROTV";
constexpr const char* TOKEN_HEADER = "X-RETROTV-Token";
constexpr size_t REPLY_CAP = 6144;       // the longest settings answer: the channel list
constexpr size_t BODY_CAP = 256;         // settings bodies are a few fields
constexpr uint32_t CONFIG_WAIT_MS = 6000;  // an SD write or a Wi-Fi scan answer

// Every response closes its connection. With keep-alive, a phone keeps several connections
// open; past max_open_sockets the server drops the oldest, and a browser that then sends a
// POST on it gets no answer (it retries a GET by itself, never a POST): every button failed.
esp_err_t respond(httpd_req_t* req, const char* type, const char* body, ssize_t len,
                  const char* cache = "no-store") {
  if (type != nullptr) httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Cache-Control", cache);
  httpd_resp_set_hdr(req, "Connection", "close");
  const esp_err_t sent = httpd_resp_send(req, body, len);
  httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
  return sent;
}

esp_err_t sendJson(httpd_req_t* req, const char* json, size_t len) {
  return respond(req, "application/json", json, static_cast<ssize_t>(len));
}

esp_err_t sendStatus(httpd_req_t* req, const char* status) {
  httpd_resp_set_status(req, status);
  return respond(req, nullptr, nullptr, 0);
}

// The value of `key` in the query string, bounded; false when missing or too long.
bool queryValue(httpd_req_t* req, const char* key, char* value, size_t cap) {
  char query[QUERY_CAP];
  return httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
         httpd_query_key_value(query, key, value, cap) == ESP_OK;
}

}  // namespace

WebRemote::WebRemote() : gate_(esp_random) {}

void WebRemote::addLogo(uint16_t number, LogoInk ink, const uint8_t* png, size_t len) {
  if (server_ != nullptr) return;
  Logo* l = nullptr;
  for (size_t i = 0; i < logoCount_ && l == nullptr; ++i) {
    if (logos_[i].number == number) l = &logos_[i];
  }
  if (l == nullptr) {
    if (logoCount_ >= MAX_CHANNELS) return;
    l = &logos_[logoCount_++];
    *l = Logo{number, {}, {}};
  }
  l->png[static_cast<size_t>(ink)] = png;
  l->len[static_cast<size_t>(ink)] = len;
}

void WebRemote::setChannels(const ChannelManager& channels, uint64_t logoMask) {
  if (server_ != nullptr) return;  // from begin() on, updateChannels()
  for (char*& buf : channelsBuf_) {
    if (buf == nullptr) buf = static_cast<char*>(heap_caps_malloc(CHANNELS_JSON_CAP, MALLOC_CAP_SPIRAM));
  }
  if (channelsBuf_[0] == nullptr || channelsBuf_[1] == nullptr) return;
  writeChannels(channels, logoMask, channelsBuf_[0]);
  channelsVersion_ = esp_random();  // a new boot is a new list for a page left open
  channelsJson_ = channelsBuf_[0];
}

void WebRemote::updateChannels(const ChannelManager& channels, uint64_t logoMask) {
  char* idle = channelsJson_.load() == channelsBuf_[0] ? channelsBuf_[1] : channelsBuf_[0];
  if (idle == nullptr) return;
  writeChannels(channels, logoMask, idle);
  channelsJson_ = idle;
  channelsVersion_ = channelsVersion_.load() + 1;
}

void WebRemote::writeChannels(const ChannelManager& channels, uint64_t logoMask, char* into) {
  if (writeChannelsJson(channels, logoMask, into, CHANNELS_JSON_CAP) == 0) {
    snprintf(into, CHANNELS_JSON_CAP, "{\"channels\":[]}");
    PLOG("WEB", "channel list too long for the remote");
  }
}

bool WebRemote::begin() {
  if (server_ != nullptr) return true;
  commands_ = xQueueCreate(WEB_COMMAND_QUEUE, sizeof(RemoteCommand));
  configQueue_ = xQueueCreate(1, sizeof(ConfigRequest));
  configDone_ = xSemaphoreCreateBinary();
  reply_ = static_cast<char*>(heap_caps_malloc(REPLY_CAP, MALLOC_CAP_SPIRAM));
  for (char*& buf : guideBuf_) buf = static_cast<char*>(heap_caps_malloc(GUIDE_JSON_CAP, MALLOC_CAP_SPIRAM));
  if (commands_ == nullptr || configQueue_ == nullptr || configDone_ == nullptr || reply_ == nullptr ||
      channelsJson_.load() == nullptr) {
    return false;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = WEB_TASK_PRIO;
  config.core_id = WEB_TASK_CORE;
  config.stack_size = WEB_TASK_STACK;
  config.max_open_sockets = 5;  // a phone loading the page opens a few at once; lwIP has 16
  config.lru_purge_enable = true;
  config.max_uri_handlers = 10;
  config.uri_match_fn = httpd_uri_match_wildcard;  // "/api/config/*"
  if (httpd_start(&server_, &config) != ESP_OK) {
    server_ = nullptr;
    return false;
  }
  struct Route {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t*);
  };
  const Route routes[] = {
      {"/", HTTP_GET, onPage},
      {"/api/state", HTTP_GET, onState},
      {"/api/channels", HTTP_GET, onChannels},
      {"/api/key", HTTP_POST, onKey},
      {"/api/channel", HTTP_POST, onChannel},
      {"/api/logo", HTTP_GET, onLogo},
      {"/api/guide", HTTP_GET, onGuide},
      {"/api/config/*", HTTP_GET, onConfig},
      {"/api/config/*", HTTP_POST, onConfig},
  };
  for (const Route& r : routes) {
    httpd_uri_t uri = {};  // the websocket fields stay off
    uri.uri = r.uri;
    uri.method = r.method;
    uri.handler = r.handler;
    uri.user_ctx = this;
    httpd_register_uri_handler(server_, &uri);
  }
  return true;
}

void WebRemote::announce(const char* ip) {
  if (announced_ || server_ == nullptr || !mdnsStarted()) return;
  mdns_hostname_set(WEB_HOSTNAME);
  mdns_service_add("RETROTV", "_http", "_tcp", 80, nullptr, 0);
  announced_ = true;
  PLOG("WEB", "remote at http://%s.local (http://%s)", WEB_HOSTNAME, ip);
}

bool WebRemote::poll(RemoteCommand& command) {
  return commands_ != nullptr && xQueueReceive(commands_, &command, 0) == pdTRUE;
}

void WebRemote::publish(const RemoteState& state) {
  portENTER_CRITICAL(&lock_);
  state_ = state;
  portEXIT_CRITICAL(&lock_);
}

esp_err_t WebRemote::onPage(httpd_req_t* req) {
  return respond(req, "text/html; charset=utf-8", REMOTE_PAGE, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebRemote::onState(httpd_req_t* req) {
  auto* self = static_cast<WebRemote*>(req->user_ctx);
  portENTER_CRITICAL(&self->lock_);
  const RemoteState state = self->state_;
  portEXIT_CRITICAL(&self->lock_);
  char json[STATE_JSON_CAP];
  const size_t len = writeStateJson(state, json, sizeof(json));
  if (len == 0) return sendStatus(req, "500 Internal Server Error");
  return sendJson(req, json, len);
}

esp_err_t WebRemote::onChannels(httpd_req_t* req) {
  auto* self = static_cast<WebRemote*>(req->user_ctx);
  const char* json = self->channelsJson_.load();
  return sendJson(req, json, strlen(json));
}

// The guide is made only while a phone asks for it: the first request marks it wanted and gets
// {"pending":true}; the page asks again and App has it ready a moment later.
esp_err_t WebRemote::onGuide(httpd_req_t* req) {
  auto* self = static_cast<WebRemote*>(req->user_ctx);
  self->guideAskedMs_ = millis() | 1u;
  const char* json = self->guideJson_.load();
  if (json == nullptr) {
    static constexpr char PENDING[] = "{\"pending\":true}";
    return sendJson(req, PENDING, sizeof(PENDING) - 1);
  }
  return sendJson(req, json, strlen(json));
}

char* WebRemote::guideScratch(size_t& cap) {
  cap = GUIDE_JSON_CAP;
  return guideJson_.load() == guideBuf_[0] ? guideBuf_[1] : guideBuf_[0];
}

void WebRemote::publishGuide() { guideJson_ = guideJson_.load() == guideBuf_[0] ? guideBuf_[1] : guideBuf_[0]; }

bool WebRemote::guideWanted(uint32_t nowMs) const {
  const uint32_t asked = guideAskedMs_.load();
  return asked != 0 && nowMs - asked < WEB_GUIDE_IDLE_MS;
}

// Logos never change while the TV runs: the phone keeps them a day.
esp_err_t WebRemote::onLogo(httpd_req_t* req) {
  auto* self = static_cast<WebRemote*>(req->user_ctx);
  char value[VALUE_CAP];
  uint16_t number = 0;
  if (queryValue(req, "n", value, sizeof(value)) && parseChannelNumber(value, number)) {
    const size_t ink = static_cast<size_t>(parseLogoInk(queryValue(req, "c", value, sizeof(value)) ? value : nullptr));
    for (size_t i = 0; i < self->logoCount_; ++i) {
      const Logo& l = self->logos_[i];
      const size_t use = l.png[ink] != nullptr ? ink : 0;
      if (l.number == number && l.png[use] != nullptr) {
        return respond(req, "image/png", reinterpret_cast<const char*>(l.png[use]), static_cast<ssize_t>(l.len[use]),
                       "max-age=86400");
      }
    }
  }
  return sendStatus(req, "404 Not Found");
}

esp_err_t WebRemote::onKey(httpd_req_t* req) {
  char value[VALUE_CAP];
  RemoteCommand command;
  if (!queryValue(req, "k", value, sizeof(value)) || !parseRemoteKey(value, command.key)) {
    return sendStatus(req, "400 Bad Request");
  }
  return queue(req, command);
}

esp_err_t WebRemote::onChannel(httpd_req_t* req) {
  char value[VALUE_CAP];
  RemoteCommand command;
  if (!queryValue(req, "n", value, sizeof(value)) || !parseChannelNumber(value, command.channel)) {
    return sendStatus(req, "400 Bad Request");
  }
  command.tune = true;
  return queue(req, command);
}

esp_err_t WebRemote::queue(httpd_req_t* req, const RemoteCommand& command) {
  if (httpd_req_get_hdr_value_len(req, COMMAND_HEADER) == 0) return sendStatus(req, "403 Forbidden");
  auto* self = static_cast<WebRemote*>(req->user_ctx);
  if (xQueueSend(self->commands_, &command, 0) != pdTRUE) return sendStatus(req, "503 Service Unavailable");
  return sendStatus(req, "204 No Content");
}

// --- Settings --------------------------------------------------------------------------------
// Pairing: the phone asks for a code, the TV shows it, the phone sends it back and gets a token
// for X-RETROTV-Token. Everything else under /api/config/ needs that token, and runs in the loop.

namespace {

const char* statusLine(int status) {
  switch (status) {
    case 200: return "200 OK";
    case 202: return "202 Accepted";
    case 204: return "204 No Content";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 503: return "503 Service Unavailable";
    case 504: return "504 Gateway Timeout";
    default: return "500 Internal Server Error";
  }
}

esp_err_t sendError(httpd_req_t* req, int status, const char* message) {
  char body[96];
  snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message);
  httpd_resp_set_status(req, statusLine(status));
  return respond(req, "application/json", body, static_cast<ssize_t>(strlen(body)));
}

// The request body, NUL-terminated; -1 when too long or broken.
int readBody(httpd_req_t* req, char* buf, size_t cap) {
  if (req->content_len >= cap) return -1;
  size_t got = 0;
  while (got < req->content_len) {
    const int n = httpd_req_recv(req, buf + got, req->content_len - got);
    if (n <= 0) return -1;
    got += static_cast<size_t>(n);
  }
  buf[got] = '\0';
  return static_cast<int>(got);
}

bool pathIs(const httpd_req_t* req, const char* path) {
  const size_t n = strlen(path);
  return strncmp(req->uri, path, n) == 0 && (req->uri[n] == '\0' || req->uri[n] == '?');
}

}  // namespace

size_t WebRemote::replyCap() const { return REPLY_CAP; }

bool WebRemote::pollConfig(ConfigRequest& request) {
  return configQueue_ != nullptr && xQueueReceive(configQueue_, &request, 0) == pdTRUE;
}

void WebRemote::finishConfig(uint32_t seq, int status, size_t len) {
  replyStatus_ = status;
  replyLen_ = len;
  replySeq_ = seq;
  xSemaphoreGive(configDone_);
}

esp_err_t WebRemote::onPairStart(httpd_req_t* req) {
  auto* self = static_cast<WebRemote*>(req->user_ctx);
  RemoteCommand show;
  show.pairCode = self->gate_.start(millis());
  xQueueSend(self->commands_, &show, 0);
#if PAUTV_DEBUG_STATS  // the USB log is only readable with the board on a cable: tests read it there
  PLOG("WEB", "settings code on screen: %04u", static_cast<unsigned>(show.pairCode));
#else
  PLOG("WEB", "settings code on screen");
#endif
  return sendStatus(req, "204 No Content");
}

esp_err_t WebRemote::onPair(httpd_req_t* req) {
  auto* self = static_cast<WebRemote*>(req->user_ctx);
  char body[BODY_CAP];
  char code[5];
  char token[PAIR_TOKEN_LEN + 1];
  const int len = readBody(req, body, sizeof(body));
  if (len < 0 || !parseCode(body, static_cast<size_t>(len), code, sizeof(code))) return sendError(req, 400, "bad code");
  if (!self->gate_.verify(code, millis(), token, sizeof(token))) {
    PLOG("WEB", "wrong settings code");
    return sendError(req, 403, "wrong or expired code");
  }
  RemoteCommand done;
  done.paired = true;
  xQueueSend(self->commands_, &done, 0);
  PLOG("WEB", "phone paired for settings");
  char answer[64];
  snprintf(answer, sizeof(answer), "{\"token\":\"%s\"}", token);
  return sendJson(req, answer, strlen(answer));
}

esp_err_t WebRemote::onConfig(httpd_req_t* req) {
  auto* self = static_cast<WebRemote*>(req->user_ctx);
  const bool post = req->method == HTTP_POST;
  if (post && httpd_req_get_hdr_value_len(req, COMMAND_HEADER) == 0) return sendError(req, 403, "missing header");
  if (post && pathIs(req, "/api/config/pair/start")) return onPairStart(req);
  if (post && pathIs(req, "/api/config/pair")) return onPair(req);

  char token[PAIR_TOKEN_LEN + 2] = "";
  if (httpd_req_get_hdr_value_str(req, TOKEN_HEADER, token, sizeof(token)) != ESP_OK ||
      !self->gate_.allowed(token, millis())) {
    return sendError(req, 401, "pair first");
  }

  ConfigRequest r;
  char body[BODY_CAP] = "";
  int len = 0;
  if (post) {
    len = readBody(req, body, sizeof(body));
    if (len < 0) return sendError(req, 413, "body too long");
  }
  bool ok = true;
  if (!post && pathIs(req, "/api/config/info")) {
    r.op = ConfigOp::Info;
  } else if (!post && pathIs(req, "/api/config/wifi")) {
    r.op = ConfigOp::WifiList;
  } else if (post && pathIs(req, "/api/config/wifi/add")) {
    r.op = ConfigOp::WifiAdd;
    ok = parseWifiAdd(body, static_cast<size_t>(len), r.ssid, r.password);
  } else if (post && pathIs(req, "/api/config/wifi/remove")) {
    r.op = ConfigOp::WifiRemove;
    ok = parseSsid(body, static_cast<size_t>(len), r.ssid);
  } else if (post && pathIs(req, "/api/config/wifi/scan")) {
    r.op = ConfigOp::WifiScanStart;
  } else if (!post && pathIs(req, "/api/config/wifi/scan")) {
    r.op = ConfigOp::WifiScanResults;
  } else if (!post && pathIs(req, "/api/config/display")) {
    r.op = ConfigOp::DisplayGet;
  } else if (post && pathIs(req, "/api/config/display")) {
    r.op = ConfigOp::DisplaySet;
    ok = parseDisplay(body, static_cast<size_t>(len), r.brightness, r.volume);
  } else if (!post && pathIs(req, "/api/config/channels")) {
    r.op = ConfigOp::ChannelsList;
  } else if (post && pathIs(req, "/api/config/channels")) {
    r.op = ConfigOp::ChannelSet;
    ok = parseChannelToggle(body, static_cast<size_t>(len), r.number, r.enabled);
  } else if (!post && pathIs(req, "/api/config/voice")) {
    r.op = ConfigOp::VoiceGet;
  } else if (post && pathIs(req, "/api/config/voice")) {
    r.op = ConfigOp::VoiceSet;
    ok = parseVoice(body, static_cast<size_t>(len), r.voice);
  } else if (post && pathIs(req, "/api/config/reboot")) {
    r.op = ConfigOp::Reboot;
  } else {
    return sendError(req, 404, "no such setting");
  }
  memset(body, 0, sizeof(body));  // a password may have been in it
  if (!ok) return sendError(req, 400, "invalid values");
  return self->runConfig(req, r);
}

// Hands the request to the loop and waits for its answer. httpd runs one handler at a time, so
// there is never more than one request in flight; seq drops a late answer to an earlier one.
esp_err_t WebRemote::runConfig(httpd_req_t* req, ConfigRequest& r) {
  r.seq = ++nextSeq_;
  xSemaphoreTake(configDone_, 0);
  const bool queued = xQueueSend(configQueue_, &r, 0) == pdTRUE;
  memset(r.password, 0, sizeof(r.password));
  if (!queued) return sendError(req, 503, "busy");
  const uint32_t t0 = millis();
  while (millis() - t0 < CONFIG_WAIT_MS) {
    if (xSemaphoreTake(configDone_, pdMS_TO_TICKS(100)) == pdTRUE && replySeq_.load() == r.seq) {
      httpd_resp_set_status(req, statusLine(replyStatus_));
      return respond(req, "application/json", reply_, static_cast<ssize_t>(replyLen_));
    }
  }
  return sendError(req, 504, "the TV did not answer");
}
