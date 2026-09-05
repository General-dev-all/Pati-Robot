// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "conversation/gemini_live_client.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>
#include <utility>

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>
#include <esp_heap_caps.h>

#include "base64.hpp"
#include "conversation/metrics.hpp"
#include "psram_allocator.hpp"
#include "ws_extra_headers.hpp"

namespace stackchan::conversation {

namespace {

constexpr const char* kTag = "gemini-live";

// Endpoint. The API key rides in the URL query string — Gemini Live does
// not accept an Authorization header. Model is set via the setup message,
// not the URL, so the host string is constant.
constexpr const char* kHost = "generativelanguage.googleapis.com";
constexpr const char* kPath = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";

// Per-send timeout for the sender task's WS write. The mic loop never blocks
// on this (push_audio() enqueues), so a long write here just delays the
// sender. esp_websocket_client aborts the connection on a 0-byte write
// when poll_write times out, so this is effectively our "give up and let
// recovery reconnect" threshold.
constexpr TickType_t kSendTimeout = pdMS_TO_TICKS(10000);

constexpr UBaseType_t kSenderTaskPrio = 5;
constexpr std::size_t kSenderTaskStack = 6144;
constexpr BaseType_t kSenderTaskCore = 1;

// Audio tx ring. PCM16 @ 16 kHz mono = 32 KB/s; 40 ms chunks = 640 samples
// (1280 bytes) each. 96 slots ≈ 3.8 s buffer for hiccups before evictions.
// Worst-case ~120 KiB sitting in PSRAM.
constexpr std::size_t kAudioTxQueueLen = 96;

// Hot-path scratch ceiling for the sender's base64 / JSON wrap. 2048 PCM16
// samples × 2 B = 4096 B raw; base64 + JSON envelope ~5800 B at peak.
constexpr std::size_t kMaxChunkSamples = 2048;

struct AudioChunk {
    std::uint16_t len_samples;  // PCM16 little-endian samples
    std::int16_t data[];        // [len_samples]
};

} // namespace

class GeminiLiveClient::Impl {
public:
    explicit Impl(std::string api_key) : api_key_{std::move(api_key)} {}

    ~Impl() { teardown(); }

    tl::expected<void, ConversationError> start(const ConversationConfig& config)
    {
        if (client_ != nullptr) {
            return tl::unexpected{ConversationError::InvalidState};
        }
        config_ = config;

        // The URL includes the API key as ?key=... — esp_websocket_client
        // hands it through to the transport unchanged.
        const std::string uri = std::string{"wss://"} + kHost + kPath + "?key=" + api_key_;

        esp_websocket_client_config_t cfg{};
        cfg.uri = uri.c_str();
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.buffer_size = 8192;
        cfg.task_stack = 6144;
        cfg.task_prio = 5;
        cfg.disable_auto_reconnect = true;
        cfg.network_timeout_ms = 10000;
        cfg.ping_interval_sec = 15;
        cfg.pingpong_timeout_sec = 25;
        cfg.keep_alive_enable = true;
        cfg.keep_alive_idle = 10;
        cfg.keep_alive_interval = 5;
        cfg.keep_alive_count = 3;

        const std::size_t rx_cap = static_cast<std::size_t>(CONFIG_STACKCHAN_CONV_WS_RX_BUFFER);
        rx_buffer_ = static_cast<char*>(heap_caps_malloc(rx_cap, MALLOC_CAP_SPIRAM));
        if (rx_buffer_ == nullptr) {
            return tl::unexpected{ConversationError::OutOfMemory};
        }
        rx_capacity_ = rx_cap;
        rx_len_ = 0;
        rx_op_code_ = 0;

        const std::size_t b64_cap = base64::encoded_size(kMaxChunkSamples * sizeof(std::int16_t));
        b64_scratch_.assign(b64_cap, '\0');
        json_scratch_.assign(b64_cap + 256, '\0');

        client_ = esp_websocket_client_init(&cfg);
        if (client_ == nullptr) {
            teardown();
            return tl::unexpected{ConversationError::TransportInit};
        }
        apply_extra_ws_headers(client_, config_.extra_headers);
        esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY,
                                       &Impl::websocket_event_trampoline, this);

        setup_sent_ = false;
        audio_seq_ = 0;
        tx_evicted_total_.store(0, std::memory_order_relaxed);
        last_evict_log_ms_ = 0;
        set_state(ConversationState::Connecting);

        audio_tx_queue_ = xQueueCreate(kAudioTxQueueLen, sizeof(AudioChunk*));
        if (audio_tx_queue_ == nullptr) {
            teardown();
            return tl::unexpected{ConversationError::OutOfMemory};
        }
        sender_should_exit_.store(false, std::memory_order_relaxed);
        if (xTaskCreatePinnedToCoreWithCaps(&Impl::sender_trampoline, "gemini_tx",
                                            kSenderTaskStack, this, kSenderTaskPrio,
                                            &sender_task_, kSenderTaskCore,
                                            MALLOC_CAP_SPIRAM) != pdPASS) {
            teardown();
            return tl::unexpected{ConversationError::TransportInit};
        }

        if (esp_websocket_client_start(client_) != ESP_OK) {
            teardown();
            return tl::unexpected{ConversationError::TransportInit};
        }
        return {};
    }

    void stop() { teardown(); }

    tl::expected<void, ConversationError> push_audio(std::span<const std::int16_t> pcm)
    {
        if (state_.load(std::memory_order_relaxed) != ConversationState::Listening) {
            return {};
        }
        if (pcm.empty()) {
            return {};
        }
        if (audio_tx_queue_ == nullptr) {
            return tl::unexpected{ConversationError::NotConnected};
        }
        if (pcm.size() > kMaxChunkSamples) {
            return tl::unexpected{ConversationError::InvalidState};
        }

        // Metric: push-interval. Skip the first push (no prior reference) and
        // any monotonic-clock wraparound (esp_timer is 64-bit but we project
        // it down to 32-bit ms here; the diff stays meaningful as long as the
        // gap is < 49 days, which is fine).
        const std::uint32_t now_ms =
            static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
        if (last_push_ms_ != 0) {
            push_interval_ms_.record(static_cast<float>(now_ms - last_push_ms_));
        }
        last_push_ms_ = now_ms;
        // Metric: queue depth at the moment we attempt the enqueue. Recorded
        // BEFORE the send so a full queue shows up as depth == capacity.
        queue_depth_.record(
            static_cast<float>(uxQueueMessagesWaiting(audio_tx_queue_)));

        // Allocate the chunk in PSRAM, copy raw PCM16 samples (LE on this
        // chip already — no conversion needed; Gemini wants exactly that).
        const std::size_t alloc =
            sizeof(AudioChunk) + pcm.size() * sizeof(std::int16_t);
        auto* chunk = static_cast<AudioChunk*>(
            heap_caps_malloc(alloc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (chunk == nullptr) {
            return tl::unexpected{ConversationError::OutOfMemory};
        }
        chunk->len_samples = static_cast<std::uint16_t>(pcm.size());
        std::memcpy(chunk->data, pcm.data(), pcm.size() * sizeof(std::int16_t));

        if (xQueueSend(audio_tx_queue_, &chunk, 0) != pdTRUE) {
            AudioChunk* old = nullptr;
            if (xQueueReceive(audio_tx_queue_, &old, 0) == pdTRUE && old != nullptr) {
                heap_caps_free(old);
            }
            if (xQueueSend(audio_tx_queue_, &chunk, 0) != pdTRUE) {
                heap_caps_free(chunk);
                ESP_LOGW(kTag, "audio tx queue stuck; dropping chunk");
                return tl::unexpected{ConversationError::SendFailed};
            }
            ++eviction_count_;
            note_tx_eviction(now_ms);
        }
        return {};
    }

    tl::expected<void, ConversationError> commit_audio()
    {
        // "Ses akisim bitti, sira sende."
        //
        // 🔴 BU FONKSIYON UZUN SURE BOSTU ve gerekcesi soyleydi: "Server
        // VAD detects turn boundaries automatically." Dogruydu — ta ki
        // sunucu VAD'i hic tetiklenemeyene kadar.
        //
        // 05.09.2026'da olculdu: hoparlorle mikrofon 5 cm arayla duran bu
        // govdede ortam sesi hic kesilmiyor (mikrofon tepesi 235-4938
        // arasinda surekli dalgalaniyor). Sunucu tur kapatmak icin 500 ms
        // sessizlik bekliyor, bulamiyor, cevabi HIC uretmiyor. Disaridan
        // gorunusu "duyuyor ama konusmuyor": girdi transkripsiyonu VAD'den
        // bagimsiz calistigi icin cocugun soyledigi dogru yaziya
        // dokuluyordu, sadece cevap gelmiyordu.
        //
        // Artik turu Pati kapatiyor: kendi yerel VAD'i sessizligi gorunce
        // burayi cagiriyor (pati_sohbet.cpp, mik_gorevi). Sunucu VAD'i
        // acik kalmaya devam ediyor — ikisi celismiyor, hangisi once
        // gorurse tur orada kapaniyor.
        std::lock_guard lock{send_mutex_};
        if (client_ == nullptr || !esp_websocket_client_is_connected(client_)) {
            return tl::unexpected{ConversationError::NotConnected};
        }
        if (!setup_sent_) {
            return tl::unexpected{ConversationError::NotConnected};
        }
        static constexpr char kEnd[] =
            "{\"realtimeInput\":{\"audioStreamEnd\":true}}";
        const int rc = esp_websocket_client_send_text(
            client_, kEnd, static_cast<int>(std::strlen(kEnd)), kSendTimeout);
        if (rc <= 0) {
            return tl::unexpected{ConversationError::TransportInit};
        }
        return {};
    }

    tl::expected<void, ConversationError> submit_tool_result(std::string_view call_id,
                                                              std::string_view output_json)
    {
        std::lock_guard lock{send_mutex_};
        if (client_ == nullptr || !esp_websocket_client_is_connected(client_)) {
            return tl::unexpected{ConversationError::NotConnected};
        }

        // Gemini expects:
        //   { "toolResponse":
        //     { "functionResponses":
        //       [ { "id": <id>, "name": <name>, "response": <object> } ] } }
        // We don't have the function name here — the caller usually returns
        // the result with the call_id alone, so leave name empty and let the
        // server look up by id. Parse output_json into a cJSON object; if it
        // isn't valid JSON, wrap it as {"output": "<string>"} so the server
        // still gets something coherent.
        cJSON* root = cJSON_CreateObject();
        cJSON* tr = cJSON_AddObjectToObject(root, "toolResponse");
        cJSON* arr = cJSON_AddArrayToObject(tr, "functionResponses");
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", std::string{call_id}.c_str());

        cJSON* parsed = cJSON_Parse(std::string{output_json}.c_str());
        if (parsed != nullptr && cJSON_IsObject(parsed)) {
            cJSON_AddItemToObject(item, "response", parsed);
        } else {
            if (parsed != nullptr) cJSON_Delete(parsed);
            cJSON* resp = cJSON_AddObjectToObject(item, "response");
            cJSON_AddStringToObject(resp, "output", std::string{output_json}.c_str());
        }
        cJSON_AddItemToArray(arr, item);

        const bool ok = send_json(root);
        cJSON_Delete(root);
        return ok ? tl::expected<void, ConversationError>{}
                  : tl::unexpected{ConversationError::SendFailed};
    }

    tl::expected<void, ConversationError> cancel_response()
    {
        // Gemini Live triggers barge-in server-side from incoming audio
        // (interrupted event). No client-initiated cancel exists in the
        // protocol; the local conv-task already stops draining the audio
        // queue on barge-in, so this is a documented no-op here.
        return {};
    }

    ConversationState state() const { return state_.load(std::memory_order_relaxed); }

    std::uint32_t tx_evicted_chunks() const { return tx_evicted_total_.load(std::memory_order_relaxed); }

    void set_event_callback(EventCallback cb) { event_callback_ = std::move(cb); }

private:
    // ---- lifecycle ---------------------------------------------------------

    // Count a mic-uplink eviction and emit at most one warning per
    // kEvictLogIntervalMs — under sustained congestion push_audio evicts on
    // every chunk (~10 Hz), and a per-chunk ESP_LOGW floods the serial log.
    // The cumulative total is published per turn via tx_evicted_chunks().
    // Runs only on the push_audio caller's task, so last_evict_log_ms_ needs
    // no synchronisation; the total is atomic because it is read cross-task.
    void note_tx_eviction(std::uint32_t now_ms)
    {
        static constexpr std::uint32_t kEvictLogIntervalMs = 5000;
        const std::uint32_t total = tx_evicted_total_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (last_evict_log_ms_ == 0 || now_ms - last_evict_log_ms_ >= kEvictLogIntervalMs) {
            ESP_LOGD(kTag, "audio tx queue full; evicting oldest chunks (%u evicted this session)",
                     static_cast<unsigned>(total));
            last_evict_log_ms_ = now_ms;
        }
    }

    void teardown()
    {
        if (sender_task_ != nullptr) {
            sender_should_exit_.store(true, std::memory_order_release);
            AudioChunk* sentinel = nullptr;
            xQueueSend(audio_tx_queue_, &sentinel, 0);
            for (int i = 0; i < 200; ++i) {
                if (eTaskGetState(sender_task_) == eDeleted) break;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            sender_task_ = nullptr;
        }
        if (audio_tx_queue_ != nullptr) {
            AudioChunk* chunk = nullptr;
            while (xQueueReceive(audio_tx_queue_, &chunk, 0) == pdTRUE) {
                if (chunk != nullptr) heap_caps_free(chunk);
            }
            vQueueDelete(audio_tx_queue_);
            audio_tx_queue_ = nullptr;
        }

        if (client_ != nullptr) {
            esp_websocket_client_stop(client_);
            esp_websocket_client_destroy(client_);
            client_ = nullptr;
        }
        if (rx_buffer_ != nullptr) {
            heap_caps_free(rx_buffer_);
            rx_buffer_ = nullptr;
        }
        b64_scratch_.clear();
        b64_scratch_.shrink_to_fit();
        json_scratch_.clear();
        json_scratch_.shrink_to_fit();
        rx_capacity_ = 0;
        rx_len_ = 0;
        set_state(ConversationState::Idle);
    }

    void set_state(ConversationState s)
    {
        const ConversationState prev = state_.exchange(s, std::memory_order_relaxed);
        if (prev != s) {
            ConversationEvent ev{};
            ev.type = ConversationEventType::StateChanged;
            ev.state = s;
            emit(ev);
        }
    }

    void emit(const ConversationEvent& ev)
    {
        if (event_callback_) {
            // Stamp emit time so the conv-task can attribute pipeline latency
            // to either the WS task (ev.emit_us - decode_start) or the queue
            // hop (consumer's now - ev.emit_us).
            ConversationEvent stamped = ev;
            if (stamped.emit_us == 0) stamped.emit_us = esp_timer_get_time();
            event_callback_(stamped);
        }
    }

    void emit_error(ConversationError code, std::string message)
    {
        ConversationEvent ev{};
        ev.type = ConversationEventType::Error;
        ev.error = code;
        ev.text = std::move(message);
        emit(ev);
    }

    // ---- WebSocket transport ----------------------------------------------

    static void websocket_event_trampoline(void* arg, esp_event_base_t /*base*/,
                                            std::int32_t event_id, void* data)
    {
        static_cast<Impl*>(arg)->on_websocket_event(
            event_id, static_cast<esp_websocket_event_data_t*>(data));
    }

    void on_websocket_event(std::int32_t event_id, const esp_websocket_event_data_t* data)
    {
        switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(kTag, "WEBSOCKET_EVENT_CONNECTED");
            send_setup();
            break;
        case WEBSOCKET_EVENT_DATA:
            on_websocket_data(data);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(kTag, "WEBSOCKET_EVENT_DISCONNECTED");
            set_state(ConversationState::Error);
            emit_error(ConversationError::NotConnected, "websocket disconnected");
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(kTag, "WEBSOCKET_EVENT_ERROR");
            set_state(ConversationState::Error);
            emit_error(ConversationError::TransportInit, "websocket error");
            break;
        case WEBSOCKET_EVENT_CLOSED:
            // Either a setup-time rejection (Gemini silently closes the WS on
            // unknown model / unsupported field — no DISCONNECTED, just
            // CLOSED) or a mid-turn server close (code 1011 etc). Surface the
            // close frame info captured in on_websocket_data so the trace
            // shows the actual cause; the conv-task recovery path takes over
            // from here. setup_sent_ disambiguates the two phases in the log.
            if (last_close_code_ != 0) {
                ESP_LOGW(kTag, "WEBSOCKET_EVENT_CLOSED phase=%s code=%u reason='%s'",
                         setup_sent_ ? "post-setup" : "pre-setup",
                         last_close_code_, last_close_reason_.c_str());
            } else {
                ESP_LOGW(kTag, "WEBSOCKET_EVENT_CLOSED phase=%s (no close frame seen)",
                         setup_sent_ ? "post-setup" : "pre-setup");
            }
            set_state(ConversationState::Error);
            emit_error(ConversationError::NotConnected, "websocket closed");
            // Reset for the next session — the new client (re-init in
            // conv-task) will see fresh frames.
            last_close_code_ = 0;
            last_close_reason_.clear();
            break;
        default:
            break;
        }
    }

    void on_websocket_data(const esp_websocket_event_data_t* data)
    {
        if (data == nullptr || data->data_len < 0) return;
        if (data->payload_offset == 0) {
            rx_len_ = 0;
            rx_op_code_ = data->op_code;
        }
        // Gemini Live can ship server messages as either text (0x01) or
        // binary (0x02 — JSON bytes, not protobuf). Everything else is a
        // WS control frame: log close-frame contents so server-initiated
        // disconnects show their code + reason in the trace.
        if (rx_op_code_ != 0x01 && rx_op_code_ != 0x02) {
            if (rx_op_code_ == 0x08 && data->data_len >= 2) {
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(data->data_ptr);
                const std::uint16_t code = (bytes[0] << 8) | bytes[1];
                const int reason_len = data->data_len - 2;
                const char* reason = reinterpret_cast<const char*>(bytes + 2);
                ESP_LOGW(kTag, "ws close: code=%u reason='%.*s'", code, reason_len, reason);
                // Stash for the WEBSOCKET_EVENT_CLOSED handler (fires after
                // this) so it can log the actual code/reason.
                last_close_code_ = code;
                last_close_reason_.assign(reason, reason_len);

                // Gemini returns code=1008 (RFC 6455 "Policy Violation") for
                // a few different reasons. The two we've observed in the
                // wild that ALL indicate a dead resumption handle are:
                //   - "BidiGenerateContent session not found"
                //   - "Requested entity was not found."
                // A third 1008 cause exists ("Operation is not implemented,
                // or supported, or enabled." — sent when we accidentally
                // post audio frames mid-turn) and that one technically
                // leaves the handle valid. But on close the connection's
                // gone either way, and the cost of needlessly clearing the
                // handle is just losing one turn's worth of conversation
                // context on the reconnect. The cost of NOT clearing when
                // we should is an infinite reconnect storm (the same dead
                // handle gets replayed on every attempt → another 1008 →
                // backoff cap → unrecoverable). So we treat every 1008 as
                // handle-invalidating; the reason gets logged for any
                // future Google wording changes.
                //
                // We deliberately do NOT clear on 1011 ("Internal error
                // occurred."). Per the official docs the resumption token is
                // valid for 2h after session termination — there's no
                // documented relationship between close code and token
                // validity. LiveKit's Google plugin (the reference impl)
                // also keeps the handle across 1011 and just retries with
                // backoff.
                if (code == 1008 && !session_handle_.empty()) {
                    ESP_LOGW(kTag, "1008 reason='%.*s' → discarding resumption handle (%u bytes)",
                             reason_len, reason,
                             static_cast<unsigned>(session_handle_.size()));
                    session_handle_.clear();
                }

                // ...but a REPEATED 1011 is a different animal.
                //
                // 🔴 05.09.2026, measured on hardware. After the model was
                // switched the device fell into a closed loop:
                //
                //     resuming session via cached handle
                //     setup sent (1 tools)
                //     setupComplete
                //     ws close: code=1011 'Internal error occurred.'
                //     ...reconnect, same handle, same 1011, forever
                //
                // The reasoning above stays correct for a ONE-OFF 1011 — the
                // token really does outlive the session and the reference
                // implementations keep it. What that reasoning assumes,
                // silently, is that the handle still belongs to the model we
                // are connecting to. Change the model and it does not: the
                // server chokes on it and the failure is indistinguishable
                // from a transient server hiccup.
                //
                // Proven not to be the setup's content: the exact same setup
                // (tools, transcription, VAD, system prompt) connected from a
                // PC with an EMPTY sessionResumption and replied normally.
                // The only difference was the cached handle.
                //
                // So keep the first 1011 — it is usually transient and the
                // handle is worth keeping. Drop it on the second in a row,
                // which is the loop announcing itself. The cost is one
                // turn's context; the cost of not doing it is a robot that
                // looks completely healthy and never speaks again.
                if (code == 1011) {
                    if (++consecutive_1011_ >= 2 && !session_handle_.empty()) {
                        ESP_LOGW(kTag, "1011 twice in a row → discarding resumption handle (%u bytes)",
                                 static_cast<unsigned>(session_handle_.size()));
                        session_handle_.clear();
                        consecutive_1011_ = 0;
                    }
                } else {
                    consecutive_1011_ = 0;
                }
            }
            return;
        }
        const std::size_t total = static_cast<std::size_t>(data->payload_len);
        const std::size_t chunk = static_cast<std::size_t>(data->data_len);
        if (rx_len_ + chunk > rx_capacity_) {
            ESP_LOGW(kTag, "rx overflow: total=%u capacity=%u",
                     static_cast<unsigned>(total), static_cast<unsigned>(rx_capacity_));
            rx_len_ = 0;
            return;
        }
        std::memcpy(rx_buffer_ + rx_len_, data->data_ptr, chunk);
        rx_len_ += chunk;
        // Require total > 0: a payload_len==0 fragment (with data_len>0) would
        // otherwise satisfy rx_len_ >= total immediately and parse an
        // incomplete JSON frame. Wait for the framed total to be known.
        if (rx_len_ < total || total == 0) return; // wait for the rest
        parse_server_event(rx_buffer_, rx_len_);
        rx_len_ = 0;
    }

    // ---- outbound ----------------------------------------------------------

    bool send_json(cJSON* root)
    {
        // send_mutex_ must be held by the caller.
        char* str = cJSON_PrintUnformatted(root);
        if (str == nullptr) return false;
        const int rc = esp_websocket_client_send_text(client_, str, std::strlen(str), kSendTimeout);
        cJSON_free(str);
        return rc > 0;
    }

    void send_setup()
    {
        // BidiGenerateContentSetup. Tools, system prompt, audio modality,
        // optional session resumption handle from a prior connection.
        cJSON* root = cJSON_CreateObject();
        cJSON* setup = cJSON_AddObjectToObject(root, "setup");

        // Default model if caller didn't override.
        const std::string model = config_.model.empty()
            ? std::string{"models/gemini-2.0-flash-live-001"}
            : (config_.model.starts_with("models/") ? config_.model
                                                    : std::string{"models/"} + config_.model);
        cJSON_AddStringToObject(setup, "model", model.c_str());

        cJSON* gen_cfg = cJSON_AddObjectToObject(setup, "generationConfig");
        cJSON* mods = cJSON_AddArrayToObject(gen_cfg, "responseModalities");
        cJSON_AddItemToArray(mods, cJSON_CreateString("AUDIO"));
        if (!config_.voice.empty()) {
            cJSON* speech_cfg = cJSON_AddObjectToObject(gen_cfg, "speechConfig");
            cJSON* voice_cfg = cJSON_AddObjectToObject(speech_cfg, "voiceConfig");
            cJSON* prebuilt = cJSON_AddObjectToObject(voice_cfg, "prebuiltVoiceConfig");
            cJSON_AddStringToObject(prebuilt, "voiceName", config_.voice.c_str());
        }

        if (!config_.instructions.empty()) {
            cJSON* sys = cJSON_AddObjectToObject(setup, "systemInstruction");
            cJSON* parts = cJSON_AddArrayToObject(sys, "parts");
            cJSON* part = cJSON_CreateObject();
            cJSON_AddStringToObject(part, "text", config_.instructions.c_str());
            cJSON_AddItemToArray(parts, part);
        }

        // Input / output transcription: server sends the recognised user
        // utterance and the synthesised reply text — handy for the UI and
        // for the balloon. Empty object enables the capability.
        if (config_.enable_input_transcription) {
            cJSON_AddObjectToObject(setup, "inputAudioTranscription");
            cJSON_AddObjectToObject(setup, "outputAudioTranscription");
        }

        if (!config_.tools.empty()) {
            cJSON* tools = cJSON_AddArrayToObject(setup, "tools");
            cJSON* tool = cJSON_CreateObject();
            cJSON* decls = cJSON_AddArrayToObject(tool, "functionDeclarations");
            for (const auto& t : config_.tools) {
                cJSON* d = cJSON_CreateObject();
                cJSON_AddStringToObject(d, "name", t.name.c_str());
                cJSON_AddStringToObject(d, "description", t.description.c_str());
                cJSON* params = cJSON_Parse(t.parameters_json.c_str());
                if (params != nullptr) {
                    // `parameters` expects Google's OpenAPI 3.0.3 subset
                    // (uppercase `"OBJECT"`/`"STRING"` etc). Our tool
                    // factories use lowercase JSON Schema for OpenAI
                    // compatibility, so feed them through the mutually-
                    // exclusive `parametersJsonSchema` field instead.
                    cJSON_AddItemToObject(d, "parametersJsonSchema", params);
                }
                cJSON_AddItemToArray(decls, d);
            }
            cJSON_AddItemToArray(tools, tool);
        }

        // ---- Server VAD sensitivity -------------------------------------
        //
        // 🔴 THIS BLOCK WAS MISSING AND IT COST A DAY (05.09.2026).
        //
        // ConversationConfig has carried vad_silence_ms / prefix_padding /
        // threshold all along, and the panel even shows them — but nothing
        // ever sent them to Gemini, so the session ran on the server's
        // default (most sensitive) turn detection.
        //
        // On this hardware that default is wrong. Speaker and microphone
        // sit ~5 cm apart in one 48x24 mm body, so the moment Pati starts
        // answering, its own voice comes back through the mic. The server
        // reads that as "the child interrupted" and cancels the reply:
        //
        //     gemini-live: serverContent.interrupted - AssistantAudioDone
        //
        // and it cancels BEFORE the first audio packet, so the local
        // barge-in guard in pati_sohbet.cpp never engages either — that
        // guard waits for g_konusuyor, which never becomes true. The two
        // deadlock and the robot goes permanently silent while looking
        // completely healthy: mic works, audio uploads, transcripts come
        // back correct, websocket stays open, no error is ever raised.
        //
        // 🔴 THE TWO ENDS PULL IN OPPOSITE DIRECTIONS — don't set both LOW.
        //
        // First attempt used LOW for both and traded one failure for
        // another. `interrupted` stopped (good) but no turn ever closed,
        // so no reply was produced at all (worse): the child stops
        // talking, the server still thinks they are mid-sentence, and it
        // waits forever. Measured on the device — interrupted count went
        // 4+ to 0, replies stayed at 0.
        //
        //   startOfSpeech LOW   -> hard to call speech START.
        //                          This is the one that matters here:
        //                          Pati's own voice returning through its
        //                          mic must NOT read as the child talking.
        //
        //   endOfSpeech HIGH    -> easy to call speech END.
        //                          The child finishes, the turn closes,
        //                          the answer comes. Setting this LOW is
        //                          what silenced it.
        //
        // So: reluctant to start, eager to finish.
        cJSON* rt = cJSON_AddObjectToObject(setup, "realtimeInputConfig");
        cJSON* aad = cJSON_AddObjectToObject(rt, "automaticActivityDetection");
        // 🔴 startOfSpeechSensitivity BILEREK GONDERILMIYOR.
        //
        // LOW denendi (05.09.2026) ve sonucu daha kotuydu: sunucu
        // konusmanin BASLADIGINI hic kabul etmedi, yani tur hic acilmadi
        // ve cevap hic uretilmedi. Transkript yine geliyordu — girdi
        // yaziya dokme VAD'den bagimsiz calisiyor — bu yuzden disaridan
        // "duyuyor ama cevap vermiyor" gibi gorunuyordu.
        //
        // Bu kartta mikrofon zaten kisik: konusma tepesi 1342/32767
        // olculdu (testlerde 21857 gorulmustu). Kisik girise bir de
        // "zor tetiklen" demek, kapiyi buakiyla kapatmak oluyor.
        //
        // Varsayilan birakiliyor; yanki sorunu geri gelirse cozum
        // hassasiyeti dusurmek DEGIL, Pati konusurken mikrofonu
        // susturmak (pati_sohbet.cpp'de o koruma zaten var).
        cJSON_AddStringToObject(aad, "endOfSpeechSensitivity",
                                "END_SENSITIVITY_HIGH");

        // Barge-in: sunucu, konusma algilayinca cevabini kessin mi.
        //
        // 🔴 05.09.2026'da olculdu — kapatilmasi SART oldu. Cevap
        // uretiliyordu ama her seferinde soyle bitiyordu:
        //
        //     gemini-live: serverContent.interrupted - AssistantAudioDone
        //
        // Kesen cocuk degildi, Pati'nin KENDI SESIYDI: hoparlorle mikrofon
        // ayni kucuk govdede. Cocuk hic konusmasa bile robot konusmaya
        // baslar baslamaz kendini kesiyordu.
        //
        // pati_sohbet.cpp'de "robot konusurken mikrofonu sustur" korumasi
        // zaten var ama tek basina yetmiyor: iptal ILK ses paketinden once
        // gelebiliyor, o an "konusuyor" bayragi henuz kalkmamis oluyor.
        // Iki tarafi birden kapatmak gerekiyordu.
        // ⚠️ BURADA "NO_INTERRUPTION" DENENDI VE GERI ALINDI (05.09.2026).
        //
        // Amac dogruydu: yanki yuzunden kesilmeyi durdurmak. Sonuc tersine
        // cikti — olculdu:
        //
        //     NO_INTERRUPTION yok : serverContent 4 kez, interrupted 1
        //                           (cevap uretiliyor ama kesiliyor)
        //     NO_INTERRUPTION var : serverContent 0
        //                           (cevap HIC uretilmiyor)
        //
        // Sebep ortam sesi: mikrofon tepesi 235-4938 arasinda surekli
        // dalgalaniyor, yani gercek sessizlik hic olmuyor. Sunucu 500 ms
        // sessizlik bekliyor, bulamiyor, turu hic kapatmiyor. Kesmeyi
        // yasaklamak bu durumda "hic konusma" anlamina geliyor.
        //
        // Yankinin dogru caresi sunucuya "kesme" demek DEGIL, Pati'nin
        // konusurken mikrofonu gercekten susturmasi (pati_sohbet.cpp) ve
        // hoparlor seviyesini yankiyi doyurmayacak yerde tutmak.
        (void)config_.allow_interruption;
        cJSON_AddNumberToObject(aad, "prefixPaddingMs",
                                static_cast<double>(config_.vad_prefix_padding_ms));
        cJSON_AddNumberToObject(aad, "silenceDurationMs",
                                static_cast<double>(config_.vad_silence_ms));

        // Session resumption: if we have a handle from a prior goAway, ask
        // the server to continue from there. Always include the field so
        // the server emits resumption updates; an empty handle is the
        // documented way to opt in to fresh-session resumption tokens.
        cJSON* rs = cJSON_AddObjectToObject(setup, "sessionResumption");
        if (!session_handle_.empty()) {
            cJSON_AddStringToObject(rs, "handle", session_handle_.c_str());
            ESP_LOGI(kTag, "resuming session via cached handle");
        }

        {
            std::lock_guard lock{send_mutex_};
            (void)send_json(root);
        }
        cJSON_Delete(root);
        ESP_LOGI(kTag, "setup sent (%u tools)", static_cast<unsigned>(config_.tools.size()));
        setup_sent_ = true;
    }

    // ---- inbound: server event dispatch -----------------------------------

    static const char* json_str(const cJSON* obj, const char* key)
    {
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
        return cJSON_IsString(v) ? v->valuestring : nullptr;
    }

    void parse_server_event(const char* json, std::size_t len)
    {
        // Temporary diagnostic for the first few messages so we can see what
        // the server is actually sending during setup negotiation.
        // One-shot peek at the first few non-audio frames so we can spot
        // unexpected setup rejections / new event shapes in the logs.
        // Audio (inlineData) is skipped — it's multi-KB base64 that dwarfs
        // everything else and is already captured by the per-100-chunk
        // audio_send heartbeat.
        if (rx_log_count_ < 5 && std::strstr(json, "\"inlineData\"") == nullptr) {
            const std::size_t snip = std::min<std::size_t>(len, 384);
            ESP_LOGI(kTag, "rx[%u/%uB]: %.*s%s",
                     rx_log_count_, static_cast<unsigned>(len),
                     static_cast<int>(snip), json, snip < len ? "…" : "");
            ++rx_log_count_;
        }
        cJSON* root = cJSON_ParseWithLength(json, len);
        if (root == nullptr) {
            ESP_LOGW(kTag, "json parse failed");
            return;
        }

        // setupComplete: empty object, marks session ready.
        if (cJSON_GetObjectItemCaseSensitive(root, "setupComplete") != nullptr) {
            ESP_LOGI(kTag, "setupComplete");
            // 🔴 SAYAC BURADA SIFIRLANMIYOR — ve bu satirin yoklugu
            // kasitli.
            //
            // Ilk yazilisinda burada sifirlaniyordu ve zinciri OLDURUYORDU:
            // sorunlu durumda setupComplete HER SEFERINDE geliyor, kopma
            // ondan sonra oluyor. Yani sayac her turda sifirlanip 2'ye hic
            // ulasamiyor, handle da hic atilmiyordu — tam kirmak istedigimiz
            // dongu.
            //
            // Oturumun gercekten saglikli oldugunu soyleyen sey setup degil,
            // sunucudan VERI gelmesi; sayac orada sifirlaniyor.
            set_state(ConversationState::Listening);
        }

        if (cJSON* sc = cJSON_GetObjectItemCaseSensitive(root, "serverContent"); sc != nullptr) {
            // Sunucudan GERCEK VERI geldi — oturum saglikli, 1011 zinciri
            // kirildi. Sayacin setupComplete'te DEGIL burada sifirlanmasinin
            // gerekcesi orada yaziyor: sorunlu dongude setup her seferinde
            // basariyla tamamlaniyor, ayiran sey veri gelip gelmemesi.
            consecutive_1011_ = 0;
            handle_server_content(sc);
        }
        if (cJSON* tc = cJSON_GetObjectItemCaseSensitive(root, "toolCall"); tc != nullptr) {
            handle_tool_call(tc);
        }
        if (cJSON* up = cJSON_GetObjectItemCaseSensitive(root, "sessionResumptionUpdate");
            up != nullptr) {
            handle_session_resumption_update(up);
        }
        if (cJSON* gw = cJSON_GetObjectItemCaseSensitive(root, "goAway"); gw != nullptr) {
            // Gemini docs name this field `timeLeft`; older snippets called
            // it `timeBeforeClose`. Read whichever the server sent.
            const cJSON* tl = cJSON_GetObjectItemCaseSensitive(gw, "timeLeft");
            if (tl == nullptr) tl = cJSON_GetObjectItemCaseSensitive(gw, "timeBeforeClose");
            const char* tl_str = cJSON_IsString(tl) ? tl->valuestring : "?";
            ESP_LOGW(kTag, "goAway received: timeLeft=%s", tl_str);
            // Surface to the conv-task so it can reconnect BEFORE the server
            // closes — otherwise we eat a hard ABORTED close at the deadline
            // and any in-flight audio gets cut off. The existing handle in
            // session_handle_ (cached from the latest sessionResumptionUpdate)
            // survives the reconnect; send_setup() replays it.
            ConversationEvent ev{};
            ev.type = ConversationEventType::GoingAway;
            ev.text = tl_str;
            emit(ev);
        }
        cJSON_Delete(root);
    }

    void handle_server_content(const cJSON* sc)
    {
        // inputTranscription / outputTranscription: text only.
        if (const cJSON* it = cJSON_GetObjectItemCaseSensitive(sc, "inputTranscription");
            it != nullptr) {
            const char* text = json_str(it, "text");
            if (text != nullptr) {
                ConversationEvent ev{};
                ev.type = ConversationEventType::UserTranscript;
                ev.text = text;
                emit(ev);
            }
        }
        if (const cJSON* ot = cJSON_GetObjectItemCaseSensitive(sc, "outputTranscription");
            ot != nullptr) {
            const char* text = json_str(ot, "text");
            if (text != nullptr) {
                ConversationEvent ev{};
                ev.type = ConversationEventType::AssistantTextDelta;
                ev.text = text;
                emit(ev);
            }
        }

        // First serverContent.modelTurn of a turn implies the server has
        // decided the user's utterance ended and the model is now
        // responding. OpenAI Realtime fires an explicit
        // input_audio_buffer.speech_stopped that the conv-task hooks
        // into to switch Local::Listening → Thinking (which is what
        // gates AssistantAudioChunk processing). Gemini gives us no
        // direct equivalent, so synthesise SpeechStopped here exactly
        // once per turn, before any audio is emitted.
        if (cJSON_GetObjectItemCaseSensitive(sc, "modelTurn") != nullptr && !turn_speech_stopped_emitted_) {
            ConversationEvent ev{};
            ev.type = ConversationEventType::SpeechStopped;
            emit(ev);
            turn_speech_stopped_emitted_ = true;
        }

        // modelTurn.parts[].inlineData = base64 PCM 24 kHz audio.
        if (const cJSON* mt = cJSON_GetObjectItemCaseSensitive(sc, "modelTurn"); mt != nullptr) {
            const cJSON* parts = cJSON_GetObjectItemCaseSensitive(mt, "parts");
            if (cJSON_IsArray(parts)) {
                const cJSON* part = nullptr;
                cJSON_ArrayForEach(part, parts) {
                    const cJSON* inline_data = cJSON_GetObjectItemCaseSensitive(part, "inlineData");
                    if (inline_data == nullptr) continue;
                    const char* mime = json_str(inline_data, "mimeType");
                    const char* b64 = json_str(inline_data, "data");
                    if (b64 == nullptr) continue;
                    // Time the base64 decode + memcpy as one "decode" measurement.
                    // base64 dominates (PCM16 is 4:3 expanded on the wire), memcpy
                    // is included so the metric tracks the full cost the conv-task
                    // pays before AssistantAudioChunk leaves this client.
                    const std::int64_t decode_t0 = esp_timer_get_time();
                    auto decoded = base64::decode(b64);
                    if (!decoded) {
                        emit_error(decoded.error(), "audio base64 decode failed");
                        continue;
                    }
                    auto pcm = std::make_shared<std::vector<std::int16_t>>(
                        decoded->size() / sizeof(std::int16_t));
                    std::memcpy(pcm->data(), decoded->data(),
                                pcm->size() * sizeof(std::int16_t));
                    const std::int64_t decode_us = esp_timer_get_time() - decode_t0;
                    decode_us_.record(static_cast<float>(decode_us));
                    if (state_.load(std::memory_order_relaxed) != ConversationState::Speaking) {
                        set_state(ConversationState::Speaking);
                        // Drop any mic chunks that were buffered between
                        // the last enqueue and the server's response —
                        // sending them now triggers a 1008 close on Gemini.
                        drop_pending_audio();
                    }
                    ConversationEvent ev{};
                    ev.type = ConversationEventType::AssistantAudioChunk;
                    ev.audio = std::move(pcm);
                    emit(ev);
                    (void)mime; // currently we trust the server's "audio/pcm;rate=24000"
                }
            }
        }

        // interrupted: the server cut the model's audio stream short (server-
        // side VAD detected the user speaking over the reply, or another
        // back-pressure signal). The model will NOT emit generationComplete
        // for this turn, so we need to drive the same downstream cleanup as
        // generationComplete — otherwise conversation_task stays stuck in
        // Local::Speaking (audio_complete_ never flips to true) and mouth_open
        // freezes at 0 because update_mouth() exhausts assistant_pcm_. See
        // GitHub issue #2.
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(sc, "interrupted"))) {
            ESP_LOGI(kTag, "serverContent.interrupted → AssistantAudioDone");
            ConversationEvent ev{};
            ev.type = ConversationEventType::AssistantAudioDone;
            emit(ev);
        }

        // generationComplete: model has finished generating audio. Mirror
        // OpenAI's AssistantAudioDone so the conv-task knows playback can
        // finish naturally.
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(sc, "generationComplete"))) {
            ConversationEvent ev{};
            ev.type = ConversationEventType::AssistantAudioDone;
            emit(ev);
        }
        // turnComplete: server ready for the next user turn. Drop back to
        // Listening so push_audio starts enqueueing mic chunks again — its
        // hot-path check (state != Listening → drop) is what kept us mute
        // after the first reply. Re-arm the SpeechStopped synthesiser too.
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(sc, "turnComplete"))) {
            if (decode_us_.count > 0) {
                ESP_LOGI(kTag,
                         "metrics(decode): chunks=%u  us avg=%.0f min=%.0f max=%.0f",
                         static_cast<unsigned>(decode_us_.count),
                         decode_us_.mean(), static_cast<double>(decode_us_.min()),
                         static_cast<double>(decode_us_.max()));
                decode_us_.reset();
            }
            // TX pipeline summary for this turn. Logged when ANY of the
            // counters is non-zero so quiet turns don't spam the log.
            // Pairs with main/conversation_task's metrics(play) line printed
            // shortly after on the same turn — together they cover the full
            // mic → WS / WS → speaker pipeline. queue_depth/send_dt spikes
            // localise the bottleneck (CPU / WS link / Gemini RX), and the
            // counters surface silently-dropped or evicted chunks that
            // wouldn't otherwise show up in the trace.
            if (send_dt_ms_.count > 0 || eviction_count_ > 0 ||
                send_fail_count_ > 0 || silent_drop_count_ > 0) {
                ESP_LOGI(kTag,
                         "metrics(mic): sent=%u  send_dt_ms avg=%.1f min=%.0f max=%.0f  "
                         "queue_depth avg=%.1f min=%.0f max=%.0f  evicted=%u send_fail=%u silent_drop=%u  "
                         "push_interval_ms avg=%.1f min=%.0f max=%.0f  encode_us avg=%.0f min=%.0f max=%.0f",
                         static_cast<unsigned>(send_dt_ms_.count),
                         send_dt_ms_.mean(), static_cast<double>(send_dt_ms_.min()),
                         static_cast<double>(send_dt_ms_.max()),
                         queue_depth_.mean(), static_cast<double>(queue_depth_.min()),
                         static_cast<double>(queue_depth_.max()),
                         static_cast<unsigned>(eviction_count_),
                         static_cast<unsigned>(send_fail_count_),
                         static_cast<unsigned>(silent_drop_count_),
                         push_interval_ms_.mean(), static_cast<double>(push_interval_ms_.min()),
                         static_cast<double>(push_interval_ms_.max()),
                         encode_us_.mean(), static_cast<double>(encode_us_.min()),
                         static_cast<double>(encode_us_.max()));
                send_dt_ms_.reset();
                queue_depth_.reset();
                push_interval_ms_.reset();
                encode_us_.reset();
                eviction_count_ = 0;
                send_fail_count_ = 0;
                silent_drop_count_ = 0;
                // Don't reset last_push_ms_: push_interval is meaningful
                // across turns (a long Speaking gap will show up as a single
                // huge interval, which is expected and informative).
            }
            ConversationEvent ev{};
            ev.type = ConversationEventType::ResponseDone;
            emit(ev);
            turn_speech_stopped_emitted_ = false;
            set_state(ConversationState::Listening);
        }
    }

    void handle_tool_call(const cJSON* tc)
    {
        const cJSON* calls = cJSON_GetObjectItemCaseSensitive(tc, "functionCalls");
        if (!cJSON_IsArray(calls)) return;
        const cJSON* call = nullptr;
        cJSON_ArrayForEach(call, calls) {
            const char* id = json_str(call, "id");
            const char* name = json_str(call, "name");
            const cJSON* args = cJSON_GetObjectItemCaseSensitive(call, "args");
            if (id == nullptr || name == nullptr) continue;
            char* args_str = (args != nullptr) ? cJSON_PrintUnformatted(args) : nullptr;
            ConversationEvent ev{};
            ev.type = ConversationEventType::ToolCallRequested;
            ev.tool_call = ToolCall{
                .call_id = id,
                .name = name,
                .arguments_json = args_str != nullptr ? std::string{args_str} : std::string{"{}"},
            };
            emit(ev);
            if (args_str != nullptr) cJSON_free(args_str);
        }
    }

    void handle_session_resumption_update(const cJSON* up)
    {
        // Gemini emits the new handle on every turn; the latest one wins.
        // We don't persist across reboots — the cached handle survives only
        // for in-process reconnects (e.g., goAway 15 min cap).
        const char* handle = json_str(up, "newHandle");
        const cJSON* resumable = cJSON_GetObjectItemCaseSensitive(up, "resumable");
        if (handle != nullptr && cJSON_IsTrue(resumable)) {
            session_handle_ = handle;
            ESP_LOGD(kTag, "cached new resumption handle (%u chars)",
                     static_cast<unsigned>(session_handle_.size()));
        }
    }

    // Drain the audio_tx_queue, freeing every queued chunk. Called when
    // the server begins responding so we don't keep dribbling stale
    // pre-turn mic samples that trigger Gemini's 1008 policy close.
    void drop_pending_audio()
    {
        if (audio_tx_queue_ == nullptr) return;
        AudioChunk* c = nullptr;
        std::size_t dropped = 0;
        while (xQueueReceive(audio_tx_queue_, &c, 0) == pdTRUE) {
            if (c != nullptr) heap_caps_free(c);
            ++dropped;
        }
        if (dropped > 0) {
            ESP_LOGI(kTag, "dropped %u stale mic chunks at turn boundary",
                     static_cast<unsigned>(dropped));
        }
    }

    // ---- audio sender task -------------------------------------------------

    static void sender_trampoline(void* arg)
    {
        static_cast<Impl*>(arg)->sender_loop();
        vTaskDeleteWithCaps(nullptr);
    }

    void sender_loop()
    {
        AudioChunk* chunk = nullptr;
        while (!sender_should_exit_.load(std::memory_order_acquire)) {
            if (xQueueReceive(audio_tx_queue_, &chunk, pdMS_TO_TICKS(100)) != pdTRUE) {
                continue;
            }
            if (chunk == nullptr) break;
            send_one_chunk(chunk);
            heap_caps_free(chunk);
            chunk = nullptr;
        }
    }

    void send_one_chunk(AudioChunk* chunk)
    {
        // All four early-return paths are "silently dropped at the sender" —
        // the chunk is freed by the caller. Count them so the per-turn log
        // can show how many mic samples never reached Gemini and why this
        // isn't a Gemini-side issue.
        if (client_ == nullptr) { ++silent_drop_count_; return; }
        if (!esp_websocket_client_is_connected(client_)) { ++silent_drop_count_; return; }
        if (!setup_sent_) { ++silent_drop_count_; return; }
        // Don't stream mic audio while the server is mid-response. Gemini's
        // server closes the WS with `code=1008 'Operation is not
        // implemented, or supported, or enabled.'` when it receives audio
        // frames during its own turn. push_audio drops at the
        // enqueue side once state flips to Speaking, but stale chunks
        // queued just before the transition still arrive here — silently
        // discard them too.
        if (state_.load(std::memory_order_relaxed) != ConversationState::Listening) {
            ++silent_drop_count_;
            return;
        }

        const auto* raw_ptr = reinterpret_cast<const std::uint8_t*>(chunk->data);
        const std::size_t raw_len = chunk->len_samples * sizeof(std::int16_t);
        const std::size_t needed_b64 = base64::encoded_size(raw_len);
        if (needed_b64 > b64_scratch_.size()) { ++silent_drop_count_; return; }

        // Metric: base64 + JSON wrap time. CPU-bound, useful for splitting
        // "client slow" vs "network slow" when the queue overflows.
        const std::int64_t encode_t0 = esp_timer_get_time();
        auto enc = base64::encode_into({raw_ptr, raw_len}, b64_scratch_);
        if (!enc) { ++silent_drop_count_; return; }
        const int n = std::snprintf(
            json_scratch_.data(), json_scratch_.size(),
            "{\"realtimeInput\":{\"audio\":{\"data\":\"%.*s\",\"mimeType\":\"audio/pcm;rate=16000\"}}}",
            static_cast<int>(*enc), b64_scratch_.data());
        if (n <= 0 || static_cast<std::size_t>(n) >= json_scratch_.size()) {
            ++silent_drop_count_;
            return;
        }
        encode_us_.record(static_cast<float>(esp_timer_get_time() - encode_t0));

        const std::uint32_t t0 = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
        int send_rc;
        {
            std::lock_guard lock{send_mutex_};
            send_rc = esp_websocket_client_send_text(client_, json_scratch_.data(), n, kSendTimeout);
        }
        const std::uint32_t dt = static_cast<std::uint32_t>(esp_timer_get_time() / 1000) - t0;
        send_dt_ms_.record(static_cast<float>(dt));
        ++audio_seq_;
        if (send_rc <= 0) {
            ++send_fail_count_;
            ESP_LOGW(kTag, "audio send failed seq=%lu dt=%lums size=%dB rc=%d",
                     static_cast<unsigned long>(audio_seq_),
                     static_cast<unsigned long>(dt), n, send_rc);
        } else if (dt >= 100 || (audio_seq_ % 100) == 1) {
            // Debug-only: per-send lines flood the log on a congested uplink.
            // The per-turn metrics(mic) summary carries the aggregate stats.
            const UBaseType_t queued = uxQueueMessagesWaiting(audio_tx_queue_);
            ESP_LOGD(kTag, "audio send seq=%lu dt=%lums queued=%u",
                     static_cast<unsigned long>(audio_seq_),
                     static_cast<unsigned long>(dt),
                     static_cast<unsigned>(queued));
        }
    }

    // ---- members -----------------------------------------------------------

    std::string api_key_;
    ConversationConfig config_;
    EventCallback event_callback_;

    esp_websocket_client_handle_t client_{nullptr};
    std::atomic<ConversationState> state_{ConversationState::Idle};
    std::mutex send_mutex_;

    bool setup_sent_{false};
    bool turn_speech_stopped_emitted_{false};
    std::uint32_t audio_seq_{0};
    unsigned rx_log_count_{0};
    // Per-turn rolling stats for base64 decode + memcpy in handle_serverContent.
    // Logged + reset on turnComplete so the line shows up alongside the conv-task
    // playback metrics for the same turn (correlatable by timestamp).
    Stats<float> decode_us_;  // base64+memcpy time per inlineData chunk (us)

    // Per-turn TX-side metrics, logged on turnComplete next to decode_us_.
    // Mirrors RX-side `metrics(play)` (see main/conversation_task.cpp
    // log_playback_metrics) so a single turn yields metrics(mic) +
    // metrics(decode) + metrics(play) lines in chronological order.
    Stats<float> send_dt_ms_;       // WS send wall-clock per chunk (ms)
    Stats<float> queue_depth_;      // tx queue messages waiting at push time
    Stats<float> push_interval_ms_; // wall-clock between push_audio calls (ms)
    Stats<float> encode_us_;        // base64 + JSON wrap time per chunk (us)
    std::size_t eviction_count_{0};   // push_audio dropped oldest on full queue
    std::size_t send_fail_count_{0};  // esp_websocket_client_send_text rc <= 0
    std::size_t silent_drop_count_{0};// send_one_chunk silently dropped (state != Listening etc.)
    std::uint32_t last_push_ms_{0};   // for push_interval_ms_ deltas; 0 = no prior
    // Session-cumulative eviction count (vs the per-turn eviction_count_
    // above). Read cross-task by tx_evicted_chunks() → conv-task metrics.
    std::atomic<std::uint32_t> tx_evicted_total_{0};
    std::uint32_t last_evict_log_ms_{0}; // note_tx_eviction rate limiter; 0 = never logged

    // Resumption handle from goAway / sessionResumptionUpdate. Empty means
    // no prior session.
    //
    // THREADING INVARIANT: session_handle_ is only ever touched on the WS
    // event task. The three access sites — the 1008 clear in the CLOSED
    // handler, the read in send_setup() (invoked from WEBSOCKET_EVENT_CONNECTED)
    // and the assign in handle_session_resumption_update() (reached via
    // WEBSOCKET_EVENT_DATA) — all run on that single task and are thus
    // serialized. Do NOT read or write it from the audio TX / conversation
    // tasks; if that ever becomes necessary, add explicit synchronization
    // (a dedicated mutex, not send_mutex_, to avoid ordering issues with the
    // send path).
    std::string session_handle_;

    // Art arda kac 1011 geldi. Basarili bir oturum (setupComplete) ve
    // 1011 disindaki her kapanis bunu sifirliyor; gerekce yukarida.
    int consecutive_1011_{0};

    // Stash of the last WS close frame (opcode 0x08) we saw on this client.
    // The control-frame DATA event fires before WEBSOCKET_EVENT_CLOSED, so
    // we save what we saw and surface it accurately in the CLOSED handler
    // — previously CLOSED logged a misleading "(setup rejected?)" hint that
    // was wrong for any post-setup close.
    std::uint16_t last_close_code_{0};
    std::string last_close_reason_;

    char* rx_buffer_{nullptr};
    std::size_t rx_capacity_{0};
    std::size_t rx_len_{0};
    std::uint8_t rx_op_code_{0};

    std::vector<char, PsramAllocator<char>> b64_scratch_;
    std::vector<char, PsramAllocator<char>> json_scratch_;

    QueueHandle_t audio_tx_queue_{nullptr};
    TaskHandle_t sender_task_{nullptr};
    std::atomic<bool> sender_should_exit_{false};
};

// ---- public class plumbing ------------------------------------------------

GeminiLiveClient::GeminiLiveClient(std::string api_key)
    : impl_{std::make_unique<Impl>(std::move(api_key))}
{
}

GeminiLiveClient::~GeminiLiveClient() = default;

void GeminiLiveClient::set_event_callback(EventCallback cb)
{
    impl_->set_event_callback(std::move(cb));
}

std::uint32_t GeminiLiveClient::tx_evicted_chunks() const
{
    return impl_->tx_evicted_chunks();
}

tl::expected<void, ConversationError>
GeminiLiveClient::start(const ConversationConfig& config)
{
    return impl_->start(config);
}

void GeminiLiveClient::stop() { impl_->stop(); }

tl::expected<void, ConversationError>
GeminiLiveClient::push_audio(std::span<const std::int16_t> pcm)
{
    return impl_->push_audio(pcm);
}

tl::expected<void, ConversationError> GeminiLiveClient::commit_audio()
{
    return impl_->commit_audio();
}

tl::expected<void, ConversationError>
GeminiLiveClient::submit_tool_result(std::string_view call_id, std::string_view output_json)
{
    return impl_->submit_tool_result(call_id, output_json);
}

tl::expected<void, ConversationError> GeminiLiveClient::cancel_response()
{
    return impl_->cancel_response();
}

ConversationState GeminiLiveClient::state() const { return impl_->state(); }

} // namespace stackchan::conversation
