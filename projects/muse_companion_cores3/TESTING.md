# CoreS3 USB Codex validation

Validation date: 2026-10-11. M5Stack CoreS3, ESP32-S3 revision 0.2,
16 MB flash, 8 MB quad PSRAM at 80 MHz. ESP-IDF 6.0.3, CoreS3 BSP 3.0.0,
GSP 1.4.0 and gspc 0.5.0. Codex CLI 0.162.0-alpha.17.2 on Windows.

## Completed on the connected device

- Both 320x240 scenes compile. Deployable UI bundle: 126,352 bytes.
- Firmware fits the 0xaf0000 application partition with about 24% free.
  Application SHA256:
  `243ed7200990840f177768b545284b0ac566f5da30b9ea9e6f1e90e22a8e4121`.
- Flash writes verify, preserving NVS and old factory bytes. Complete 16 MB
  backup retained locally and not published.
- Startup reaches `USB Codex ready` without panic. PSRAM and both
  16 kHz/16-bit mono codecs initialize successfully.
- Owner confirms character no longer flickers and volume slider works.
  Touch logs show settings scene changes and slider events.
- USB heartbeats return battery percentage 100 from AXP2101 registers.
  This verifies live readings, not fuel-gauge accuracy over discharge.
- Existing CLI login starts an independent App Server voice thread. Typed
  test uses device press/release state and returns the expected Chinese answer.
  Speaker chunks receive successful device acknowledgements.
- Physical microphone input travels through USB, local Whisper base CPU/int8,
  Codex and Windows Chinese TTS. Owner confirms hearing the response.
  One utterance contained recognition errors; quality depends on distance,
  noise and the small speech model.
- Speaker also passed the earlier 320 ms tone test with owner confirmation.

## Scope and remaining checks

Owner selected Qwen Cherry and reported slower playback than Windows TTS.
The bridge now forwards Qwen audio deltas immediately instead of waiting for
the whole response. Regression tests verify partial USB JSON framing and
playback callback before Qwen completion. Live latency after this streaming
change is awaiting owner verification. Keys are not included in firmware
or the repository.

Long-run USB reconnect, prolonged battery discharge and extended conversations
have not been validated. The bridge now retries the configured USB port after disconnect.
Motion wake is not implemented. Talk input uses the bottom touchscreen button;
there is no mapped physical top AI button.

This port no longer starts Muse pairing, Wi-Fi provisioning or Muse services.
Codex uses the installed CLI/account and requires its normal network access;
Whisper and default Windows TTS run locally.

## Sessions, display and cloud audio update

- Rebuilt and flashed app/UI only, hashes verified. Sleep timeout is forced to
  zero in the CoreS3 model; the timeout slider is hidden. Connected label is
  `Codex 已连接`. Startup reaches ready without panic.
- App Server lists real local chats and reads recent messages. A real test
  chat was created, remembered a code word, and recalled it after client restart.
  Browser selection/history controls also returned its four messages correctly.
- The voice controller dynamically called the chat-list tool and returned real
  chat titles. Selected chat ID is persisted; credentials are not persisted.
- Existing ChatGPT CLI login cannot start native realtime audio: the server
  reports that realtime requires API key authentication. Separate OpenAI audio
  tests returned HTTP 429 `credit_balance_exhausted`; OpenAI audio remains
  implemented but live playback is unverified with this account.
- Gemini credentials passed live TTS and transcription tests. TTS first PCM
  arrived in about 5.3–7.4 seconds for a short sentence. Returned PCM is 16 kHz.
  Transcription reproduced the short Chinese test sentence. Earlier use of the
  larger transcription model took 66.9 seconds in a device conversation;
  switched to Flash Lite with minimal thinking, then tested transcription again.
- Cloud latency and quota vary; intermittent Gemini HTTP 429 occurred in earlier
  device tests. Full physical microphone quality with the final Lite model and
  owner preference for the new voice remain to be checked.
- Regression coverage includes OpenAI streaming/resampling, quota errors,
  Gemini sample framing/completion, partial USB JSON and Qwen early playback.

- Owner requested local speech again. Windows native SpeechSynthesizer enumerated
  Huihui, Yaoyao and Kangkang. Each synthesized a Chinese sentence successfully
  in about one second; WAV decoding/resampling produced 16 kHz mono PCM.
  Page exposes all three installed voices. Current mode is local STT/Windows TTS.

## Desktop writer ownership

- Reproduced `already has an active writer` on a desktop-owned mail chat.
  Selection/read succeeds; resume fails before turn/start, so no mail request
  was executed by the bridge. App idle status does not guarantee writer release.
- Added a distinct Chinese device/page/spoken notice without duplicating or
  taking over the chat. Added latest-reply reading without thread/resume.
- Regression tests verify no turn is started after ownership rejection and
  latest-reply reading does not try to resume the desktop chat.
- Direct submission to desktop-owned chats remains unsupported by the standalone
  bridge. Do not claim that listing and reading imply write access.

## Persistent writer release

- Found three numbered desktop forks of the original CoreS3 chat. Rollout
  metadata points each fork at the same original ID; actual voice messages
  remained in the original. These were not new bridge turn targets.
- Unsubscribe retains the server/thread for a 30-minute grace period, so the
  bridge now terminates only its own App Server after each answer and reconnects
  lazily. IDs and history are persisted and reused; no fallback chat creation
  occurs if the voice controller is owned by the desktop.
- HTTP chat creation also releases its idle server. Transport lifecycle is
  serialized against RPCs. Tests cover releasing after success and failure
  while retaining the selected chat ID.
- The desktop and bridge still cannot hold the same writer concurrently.
  Opening the desktop chat during an active voice turn may still conflict.

## USB recovery

- A real transient COM4 disconnect stopped the old reader/heartbeat while the
  HTTP page stayed alive. Restart restored live battery and microphone events.
- Reader now reopens the configured port until available; heartbeat keeps
  running. A disconnect clears partial audio/text and cancels the old generation.
- Regression test simulates a split JSON record, USB failure, one failed reopen
  and a successful heartbeat. Old recording/request is not replayed.
