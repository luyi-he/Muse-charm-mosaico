# CoreS3 USB Codex validation

Validation date: 2026-10-11. M5Stack CoreS3, ESP32-S3 revision 0.2,
16 MB flash, 8 MB quad PSRAM at 80 MHz. ESP-IDF 6.0.3, CoreS3 BSP 3.0.0,
GSP 1.4.0 and gspc 0.5.0. Codex CLI 0.144.1 on Windows.

## Completed on the connected device

- Both 320x240 scenes compile. Deployable UI bundle: 126,224 bytes.
- Firmware fits the 0xaf0000 application partition with about 24% free.
  Application SHA256:
  `e5b15c14c8833dd341e072648db0abe2abf40a3151a1c486651edba3d2a37695`.
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
have not been validated. Restart the bridge after USB disconnect.
Motion wake is not implemented. Talk input uses the bottom touchscreen button;
there is no mapped physical top AI button.

This port no longer starts Muse pairing, Wi-Fi provisioning or Muse services.
Codex uses the installed CLI/account and requires its normal network access;
Whisper and default Windows TTS run locally.
