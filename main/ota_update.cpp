#include "ota_update.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_pthread.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "miniz.h" // the ROM's tinfl
#include "psa/crypto.h"
#include "sdkconfig.h"

#include "dispatcher.hpp"
#include "logger.hpp"
#include "ota.hpp"
#include "ota_service.hpp"
#include "stream_frame.hpp"

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using MessageType = espp::detail::ota_stream::MessageType;

namespace {

espp::Logger logger({.tag = "ota", .level = espp::Logger::Verbosity::INFO});

// The app description sits right after the image header and the first segment's header.
constexpr size_t kAppDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
constexpr size_t kRxChunk = 4096;
constexpr auto kSendTimeout = 5s;
constexpr auto kPoll = 250ms;

// Shared by the RTPS task (commands, info) and the session thread.
std::mutex mutex;
rammp::OtaState state = rammp::OtaState::IDLE;
uint32_t nonce = 0;
uint8_t progress_pct = 0;
std::string last_error;
bool session_running = false;
std::atomic<bool> abort_requested{false};

std::function<std::string()> start_guard;
std::string own_mac;
bool pending_verify = false;
std::atomic<bool> confirmed{false};
esp_timer_handle_t confirm_timer = nullptr;

///////////////////////////////////////////////////////////////////////////////
// Helpers

std::string mac_string() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_ETH);
  return fmt::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", mac[0], mac[1], mac[2], mac[3],
                     mac[4], mac[5]);
}

std::string to_hex(std::span<const uint8_t> bytes) {
  std::string out;
  for (uint8_t b : bytes) {
    out += fmt::format("{:02x}", b);
  }
  return out;
}

std::optional<std::vector<uint8_t>> from_hex(std::string_view hex) {
  if (hex.size() % 2 != 0) {
    return std::nullopt;
  }
  auto nibble = [](char c) -> int {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
  };
  std::vector<uint8_t> out;
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int hi = nibble(hex[i]);
    const int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) {
      return std::nullopt;
    }
    out.push_back(static_cast<uint8_t>(hi << 4 | lo));
  }
  return out;
}

// major.minor.patch from a git-describe version ("v2.1.0-alpha-5-g5c5baee"); an
// untagged build ("5c5baee") reads 0.0.0.
std::tuple<long, long, long> version_number(std::string_view text) {
  std::string s(text);
  const size_t start = s.find_first_of("0123456789");
  long v[3] = {0, 0, 0};
  if (start != std::string::npos && (start == 0 || s[start - 1] == 'v')) {
    const char *p = s.c_str() + start;
    for (long &part : v) {
      char *end = nullptr;
      part = std::strtol(p, &end, 10);
      if (*end != '.') {
        break;
      }
      p = end + 1;
    }
  }
  return {v[0], v[1], v[2]};
}

// Why an image of `incoming` may not replace the running one, or "".
std::string version_refusal(std::string_view incoming) {
  const std::string_view running = esp_app_get_description()->version;
  if (version_number(incoming) < version_number(running)) {
    return fmt::format("downgrade refused: {} is older than the running {}", incoming, running);
  }
  return "";
}

bool auth_ok(const rammp::OtaCommand &command) {
  const auto tag = from_hex(command.auth);
  const std::string_view key = CONFIG_HMI_OTA_AUTH_KEY;
  if (!tag || key.empty() || psa_crypto_init() != PSA_SUCCESS) {
    return false;
  }
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_MESSAGE);
  psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
  psa_key_id_t key_id = 0;
  if (psa_import_key(&attributes, reinterpret_cast<const uint8_t *>(key.data()), key.size(),
                     &key_id) != PSA_SUCCESS) {
    return false;
  }
  const std::string message = rammp::ota_auth_message(command);
  // constant-time compare inside
  const bool ok = psa_mac_verify(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                                 reinterpret_cast<const uint8_t *>(message.data()), message.size(),
                                 tag->data(), tag->size()) == PSA_SUCCESS;
  psa_destroy_key(key_id);
  return ok;
}

void set_state(rammp::OtaState next) {
  std::lock_guard<std::mutex> lock(mutex);
  state = next;
}

///////////////////////////////////////////////////////////////////////////////
// The session: one TCP client, its frames checked against the START, then OtaService

// Holds every OTA frame to the image the START authorized before OtaService sees it.
class Gate {
public:
  explicit Gate(const rammp::OtaCommand &command)
      : command_(command) {}
  ~Gate() { psa_hash_abort(&hash_); }

  // Why a request of `type` is refused, or "" to pass it on. DATA is the image
  // itself, uncompressed.
  std::string check(MessageType type, std::span<const uint8_t> payload) {
    switch (type) {
    case MessageType::Begin:
      if (payload.size() != 4 || espp::stream_frame::get_u32(payload) != command_.image_size) {
        return fmt::format("BEGIN is not for the authorized {}-byte image", command_.image_size);
      }
      psa_hash_abort(&hash_);
      if (psa_hash_setup(&hash_, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        return "SHA-256 unavailable";
      }
      received_ = 0;
      began_ = true;
      return "";
    case MessageType::Data:
      if (!began_) {
        return ""; // OtaService refuses DATA without a session itself
      }
      if (received_ + payload.size() > command_.image_size) {
        return "more data than the authorized image";
      }
      if (received_ == 0) {
        if (auto why = check_description(payload); !why.empty()) {
          return why;
        }
      }
      psa_hash_update(&hash_, payload.data(), payload.size());
      received_ += payload.size();
      return "";
    case MessageType::Finish: {
      if (!began_) {
        return "";
      }
      if (received_ != command_.image_size) {
        return fmt::format("image incomplete: {} of {} bytes", received_, command_.image_size);
      }
      std::array<uint8_t, PSA_HASH_LENGTH(PSA_ALG_SHA_256)> digest{};
      size_t length = 0;
      if (psa_hash_finish(&hash_, digest.data(), digest.size(), &length) != PSA_SUCCESS ||
          to_hex(digest) != command_.sha256) {
        return "image SHA-256 is not the authorized one";
      }
      return "";
    }
    case MessageType::MarkValid:
    case MessageType::MarkInvalid:
      return "the device confirms or rolls back its own image";
    default:
      return "";
    }
  }

  void reset() { began_ = false; }

private:
  std::string check_description(std::span<const uint8_t> first) const {
    if (first.size() < kAppDescOffset + sizeof(esp_app_desc_t)) {
      return "first DATA frame too short to hold the app description";
    }
    esp_app_desc_t incoming{};
    std::memcpy(&incoming, first.data() + kAppDescOffset, sizeof(incoming));
    if (incoming.magic_word != ESP_APP_DESC_MAGIC_WORD) {
      return "no app description in the image";
    }
    incoming.version[sizeof(incoming.version) - 1] = '\0';
    incoming.project_name[sizeof(incoming.project_name) - 1] = '\0';
    const esp_app_desc_t *running = esp_app_get_description();
    if (std::strcmp(incoming.project_name, running->project_name) != 0) {
      return fmt::format("image is for project '{}', not '{}'", incoming.project_name,
                         running->project_name);
    }
    if (command_.version != incoming.version) {
      return fmt::format("image is version {}, not the authorized {}", incoming.version,
                         command_.version);
    }
    return version_refusal(incoming.version);
  }

  const rammp::OtaCommand &command_;
  psa_hash_operation_t hash_ = PSA_HASH_OPERATION_INIT;
  size_t received_ = 0;
  bool began_ = false;
};

// OtaEncoding::ZLIB: the DATA frames are one zlib stream, inflated here with the
// ROM's tinfl into its 32 KB window, which is flushed to `sink` whenever it fills
// (and at the end). ~43 KB of PSRAM, for the length of an update.
class Inflater {
public:
  using Sink = std::function<std::string(std::span<const uint8_t>)>;

  Inflater()
      : state_(static_cast<tinfl_decompressor *>(
            heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM)))
      , window_(static_cast<uint8_t *>(heap_caps_malloc(TINFL_LZ_DICT_SIZE, MALLOC_CAP_SPIRAM))) {
    if (state_ != nullptr) {
      tinfl_init(state_);
    }
  }
  ~Inflater() {
    heap_caps_free(state_);
    heap_caps_free(window_);
  }
  Inflater(const Inflater &) = delete;
  Inflater &operator=(const Inflater &) = delete;

  bool ok() const { return state_ != nullptr && window_ != nullptr; }
  bool done() const { return done_; }

  // Inflates `in`, handing whole windows to `sink`. Why it failed, or "".
  std::string feed(std::span<const uint8_t> in, const Sink &sink) {
    if (done_) {
      return in.empty() ? "" : "data after the end of the compressed image";
    }
    while (true) {
      size_t in_size = in.size();
      size_t out_size = TINFL_LZ_DICT_SIZE - used_;
      const tinfl_status status =
          tinfl_decompress(state_, in.data(), &in_size, window_, window_ + used_, &out_size,
                           TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT);
      in = in.subspan(in_size);
      used_ += out_size;
      if (status < TINFL_STATUS_DONE) {
        return "the compressed image is corrupt";
      }
      done_ = status == TINFL_STATUS_DONE;
      if (used_ == TINFL_LZ_DICT_SIZE || (done_ && used_ > 0)) {
        std::string why = sink(std::span<const uint8_t>(window_, used_));
        used_ = 0;
        if (!why.empty()) {
          return why;
        }
      }
      if (done_) {
        return in.empty() ? "" : "data after the end of the compressed image";
      }
      if (status == TINFL_STATUS_NEEDS_MORE_INPUT && in.empty()) {
        return "";
      }
    }
  }

private:
  tinfl_decompressor *state_;
  uint8_t *window_;
  size_t used_ = 0;
  bool done_ = false;
};

void send_all(int sock, std::span<const uint8_t> data) {
  while (!data.empty()) {
    const ssize_t sent = send(sock, data.data(), data.size(), 0);
    if (sent <= 0) {
      return; // the host is gone; the receive side notices
    }
    data = data.subspan(static_cast<size_t>(sent));
  }
}

// Waits for `sock` to be readable, `timeout` at most. -1 on error.
int wait_readable(int sock, std::chrono::milliseconds timeout) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(sock, &fds);
  timeval tv{.tv_sec = static_cast<time_t>(timeout.count() / 1000),
             .tv_usec = static_cast<suseconds_t>(timeout.count() % 1000 * 1000)};
  return select(sock + 1, &fds, nullptr, nullptr, &tv);
}

int open_listener() {
  const int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) {
    return -1;
  }
  const int yes = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(rammp::kOtaPort);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(sock, 1) != 0) {
    close(sock);
    return -1;
  }
  return sock;
}

// Accepts one client within kOtaConnectTimeout, or gives up (-1).
int accept_client(int listener) {
  const auto deadline = Clock::now() + rammp::kOtaConnectTimeout;
  while (Clock::now() < deadline && !abort_requested) {
    if (wait_readable(listener, kPoll) > 0) {
      sockaddr_in peer{};
      socklen_t len = sizeof(peer);
      const int client = accept(listener, reinterpret_cast<sockaddr *>(&peer), &len);
      if (client >= 0) {
        logger.info("Update host {} connected", inet_ntoa(peer.sin_addr));
        return client;
      }
    }
  }
  return -1;
}

// Feeds the client's frames to OtaService until the update ends. Returns why it
// failed, or "" once the image is written and activated.
std::string serve(int client, const rammp::OtaCommand &command) {
  const timeval send_timeout{
      .tv_sec = static_cast<time_t>(std::chrono::seconds(kSendTimeout).count()), .tv_usec = 0};
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));

  bool finished = false;
  bool mute = false; // an abort the gate makes must not answer the host
  auto send = [&](std::span<const uint8_t> frame) {
    if (!mute) {
      send_all(client, frame);
    }
  };
  espp::Ota ota({.progress_callback =
                     [&command](size_t written, size_t) {
                       std::lock_guard<std::mutex> lock(mutex);
                       progress_pct = static_cast<uint8_t>(written * 100 / command.image_size);
                     },
                 .log_level = espp::Logger::Verbosity::INFO});
  espp::OtaService service(ota, {.send = send,
                                 .auto_restart = true,
                                 .restart_delay = 1500ms, // the OK, then REBOOTING on RTPS
                                 .on_update_finished =
                                     [&finished] {
                                       finished = true;
                                       set_state(rammp::OtaState::REBOOTING);
                                     },
                                 .log_level = espp::Logger::Verbosity::INFO});

  Gate gate(command);
  std::string refusal;
  auto refuse = [&](std::string why) {
    logger.error("Refused: {}", why);
    if (service.owns_session()) {
      mute = true;
      service.handle_frame(static_cast<uint8_t>(MessageType::Abort), {});
      mute = false;
    }
    gate.reset();
    send(espp::detail::ota_stream::make_error(static_cast<uint32_t>(EPERM), why));
    refusal = std::move(why);
  };

  const bool compressed = command.encoding == rammp::OtaEncoding::ZLIB;
  std::optional<Inflater> inflater;
  if (compressed) {
    inflater.emplace();
    if (!inflater->ok()) {
      return "no memory to inflate the image";
    }
  }
  uint32_t compressed_received = 0;
  bool stream_failed = false;
  // One inflated window: through the gate, into the engine. The engine's own reply
  // is swallowed; the host gets one reply per DATA frame it sent.
  auto write_window = [&](std::span<const uint8_t> window) -> std::string {
    if (std::string why = gate.check(MessageType::Data, window); !why.empty()) {
      return why;
    }
    mute = true;
    service.handle_frame(static_cast<uint8_t>(MessageType::Data), window);
    mute = false;
    return service.owns_session() ? "" : "writing the image failed";
  };

  espp::Dispatcher dispatcher;
  dispatcher.register_module(
      service.module_id(),
      [&](const espp::stream_frame::Frame &frame) {
        if (frame.is_reply()) {
          return;
        }
        const auto type = static_cast<MessageType>(frame.type);
        if (compressed && type == MessageType::Data) {
          if (stream_failed || !service.owns_session()) {
            send(espp::detail::ota_stream::make_error(static_cast<uint32_t>(EPERM),
                                                      "no update session (send BEGIN first)"));
            return;
          }
          compressed_received += static_cast<uint32_t>(frame.payload.size());
          if (std::string why = inflater->feed(frame.payload, write_window); !why.empty()) {
            stream_failed = true;
            refuse(std::move(why));
            return;
          }
          send(espp::detail::ota_stream::make_ok(compressed_received));
          return;
        }
        if (compressed && type == MessageType::Finish && !inflater->done() &&
            service.owns_session()) {
          refuse("the compressed image ended early");
          return;
        }
        if (std::string why = gate.check(type, frame.payload); !why.empty()) {
          refuse(std::move(why));
          return;
        }
        service.handle(frame);
      },
      service.module_info());
  const esp_app_desc_t *app = esp_app_get_description();
  dispatcher.set_device_info(app->project_name, app->version);
  dispatcher.serve_discovery(send);

  std::vector<uint8_t> buffer(kRxChunk);
  auto last_rx = Clock::now();
  std::string error;
  while (!finished) {
    if (abort_requested) {
      error = "aborted by an ABORT command";
      break;
    }
    const int ready = wait_readable(client, kPoll);
    if (ready < 0) {
      error = "socket error";
      break;
    }
    if (ready == 0) {
      if (Clock::now() - last_rx > rammp::kOtaIdleTimeout) {
        error = "the host went silent";
        break;
      }
      continue;
    }
    const ssize_t n = recv(client, buffer.data(), buffer.size(), 0);
    if (n <= 0) {
      error = refusal.empty() ? "the host closed the connection before FINISH" : refusal;
      break;
    }
    last_rx = Clock::now();
    dispatcher.feed(std::span<const uint8_t>(buffer.data(), static_cast<size_t>(n)));
  }
  if (service.owns_session()) {
    mute = true;
    service.handle_frame(static_cast<uint8_t>(MessageType::Abort), {});
  }
  if (finished) {
    // let the OK drain before the close; the restart is already scheduled
    std::this_thread::sleep_for(200ms);
  }
  return finished ? "" : error;
}

void run_session(rammp::OtaCommand command) {
  std::string error;
  const int listener = open_listener();
  if (listener < 0) {
    error = fmt::format("could not listen on port {}", rammp::kOtaPort);
  } else {
    logger.info("Waiting {} s for the update host on port {}",
                std::chrono::duration_cast<std::chrono::seconds>(rammp::kOtaConnectTimeout).count(),
                rammp::kOtaPort);
    const int client = accept_client(listener);
    close(listener); // one host per update
    if (client < 0) {
      error = abort_requested ? "aborted by an ABORT command" : "the host never connected";
    } else {
      set_state(rammp::OtaState::RECEIVING);
      error = serve(client, command);
      shutdown(client, SHUT_RDWR);
      close(client);
    }
  }
  logger.info("Session over (stack never used: {} B)", uxTaskGetStackHighWaterMark(nullptr));
  std::lock_guard<std::mutex> lock(mutex);
  session_running = false;
  if (!error.empty()) {
    logger.error("Update failed: {}", error);
    state = rammp::OtaState::FAILED;
    last_error = error;
  }
}

std::string start_refusal(const rammp::OtaCommand &command) {
  if (command.encoding != rammp::OtaEncoding::RAW && command.encoding != rammp::OtaEncoding::ZLIB) {
    return "unknown image encoding";
  }
  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) {
    return "no OTA partition to update";
  }
  if (command.image_size == 0 || command.image_size > target->size) {
    return fmt::format("image size {} does not fit {} ({} bytes)", command.image_size,
                       target->label, target->size);
  }
  if (command.sha256.size() != 64 || !from_hex(command.sha256)) {
    return "sha256 is not 64 lowercase hex digits";
  }
  return version_refusal(command.version);
}

void confirm_timeout(void *) {
  if (!confirmed) {
    logger.error(
        "Update not confirmed within {} s: rolling back",
        std::chrono::duration_cast<std::chrono::seconds>(rammp::kOtaConfirmTimeout).count());
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// Public API

void ota_boot_check() {
  own_mac = mac_string();
  nonce = esp_random();
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t img_state{};
  pending_verify = esp_ota_get_state_partition(running, &img_state) == ESP_OK &&
                   img_state == ESP_OTA_IMG_PENDING_VERIFY;
  logger.info("Running {} from {}{}", esp_app_get_description()->version, running->label,
              pending_verify ? ", first boot of an update" : "");
  if (!pending_verify) {
    return;
  }
  esp_timer_create_args_t args{};
  args.callback = confirm_timeout;
  args.name = "ota_confirm";
  if (esp_timer_create(&args, &confirm_timer) == ESP_OK) {
    esp_timer_start_once(confirm_timer,
                         std::chrono::microseconds(rammp::kOtaConfirmTimeout).count());
  }
}

void ota_boot_confirm() {
  if (!pending_verify || confirmed) {
    return;
  }
#if CONFIG_HMI_OTA_TEST_NEVER_CONFIRM
  logger.warn("TEST IMAGE: not confirming; expect a rollback");
  return;
#endif
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK) {
    logger.error("Could not confirm the update: {}", esp_err_to_name(err));
    return;
  }
  confirmed = true;
  esp_timer_stop(confirm_timer);
  logger.info("Update confirmed: Ethernet and RTPS are up");
}

void ota_handle_command(const rammp::OtaCommand &command) {
  if (command.mac != own_mac) {
    return; // for another device
  }
  std::lock_guard<std::mutex> lock(mutex);
  if (command.nonce != nonce) {
    logger.warn("Ignoring an OTA command with a stale nonce");
    return;
  }
  nonce = esp_random(); // each nonce buys one command, accepted or not
  auto refuse = [](std::string why) {
    logger.error("OTA command refused: {}", why);
    last_error = std::move(why);
    if (!session_running) {
      state = rammp::OtaState::FAILED;
    }
  };
  if (!auth_ok(command)) {
    refuse("authentication failed");
    return;
  }
  if (command.action == rammp::OtaAction::ABORT) {
    if (session_running) {
      logger.warn("ABORT: ending the update session");
      abort_requested = true;
    }
    return;
  }
  if (command.action != rammp::OtaAction::START) {
    refuse("unknown action");
    return;
  }
  if (session_running) {
    refuse("an update is already in progress");
    return;
  }
  if (std::string why = start_guard ? start_guard() : ""; !why.empty()) {
    refuse(std::move(why));
    return;
  }
  if (auto why = start_refusal(command); !why.empty()) {
    refuse(std::move(why));
    return;
  }
  logger.info("START: {} ({} bytes{}, sha256 {}...)", command.version, command.image_size,
              command.encoding == rammp::OtaEncoding::ZLIB ? ", zlib" : "",
              command.sha256.substr(0, 12));
  session_running = true;
  abort_requested = false;
  state = rammp::OtaState::LISTENING;
  progress_pct = 0;
  last_error.clear();
  // The config applies to threads this task creates, so put back whatever it had.
  esp_pthread_cfg_t previous = esp_pthread_get_default_config();
  esp_pthread_get_cfg(&previous);
  // Internal-RAM stack: esp_ota_write runs here, with the flash cache off. Detached,
  // so it frees itself when the session ends: nothing of it stays while idle.
  auto cfg = esp_pthread_get_default_config();
  cfg.stack_size = 12 * 1024;
  cfg.prio = 4;
  cfg.thread_name = "ota_session";
  cfg.stack_alloc_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  esp_pthread_set_cfg(&cfg);
  std::thread(run_session, command).detach();
  esp_pthread_set_cfg(&previous);
}

void ota_set_start_guard(std::function<std::string()> guard) { start_guard = std::move(guard); }

OtaProgress ota_progress() {
  std::lock_guard<std::mutex> lock(mutex);
  return {.state = state, .percent = progress_pct, .last_error = last_error};
}

rammp::OtaDeviceInfo ota_device_info() {
  rammp::OtaDeviceInfo info{};
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t img_state{};
  if (esp_ota_get_state_partition(running, &img_state) != ESP_OK) {
    info.image_state = rammp::OtaImageState::UNKNOWN;
  } else if (img_state == ESP_OTA_IMG_VALID) {
    info.image_state = rammp::OtaImageState::CONFIRMED;
  } else if (img_state == ESP_OTA_IMG_PENDING_VERIFY) {
    info.image_state =
        confirmed ? rammp::OtaImageState::CONFIRMED : rammp::OtaImageState::PENDING_VERIFY;
  } else {
    info.image_state = rammp::OtaImageState::UNKNOWN;
  }
  const esp_app_desc_t *app = esp_app_get_description();
  esp_chip_info_t chip{};
  esp_chip_info(&chip);
  info.mac = own_mac;
  info.project = app->project_name;
  info.version = app->version;
  info.hw_rev =
      fmt::format("{} rev {}.{}", CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100);
  info.slot = running->label;
  info.features = rammp::kOtaFeatureZlib;
  std::lock_guard<std::mutex> lock(mutex);
  info.state = state;
  info.nonce = nonce;
  info.progress_pct = progress_pct;
  info.ota_port = (state == rammp::OtaState::LISTENING || state == rammp::OtaState::RECEIVING)
                      ? rammp::kOtaPort
                      : 0;
  info.last_error = last_error;
  return info;
}
