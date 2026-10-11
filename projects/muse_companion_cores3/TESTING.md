# CoreS3 validation

Validation date: 2026-10-11. Connected M5Stack CoreS3, ESP32-S3 revision 0.2,
16 MB flash, 8 MB quad PSRAM at 80 MHz. ESP-IDF 6.0.3, CoreS3 BSP 3.0.0,
GSP 1.4.0 and gspc 0.5.0.

## Completed

- Both 320x240 scenes compile and `gspc diagnose` reports no issues.
  The deployable bundle is 123,920 bytes; generated APIs cover the common UI.
- Full firmware compilation and link succeed. Image chip ID is 9 (ESP32-S3);
  the application fits the 0xaf0000-byte partition with approximately 14% free.
- Flash writes verify successfully. The initial installation writes only
  bootloader 0x0, partition table 0x8000, app 0x210000 and UI 0xd00000.
  A complete 16 MB backup is retained locally and is not published.
- PSRAM memory test passes. GSP reports two live scenes at 320x240.
  The owner confirms a visible character/configuration screen.
- The touchscreen produces UI callbacks when the owner presses the screen.
- ES7210 capture at 16 kHz/16-bit mono succeeds (`read=0`) with nonzero peak.
  This proves local capture, not cloud speech recognition quality.
- AW88298 opens and accepts PCM writes (`write=0`). The owner confirms hearing
  the 320 ms setup tone after allowing the DMA queue to drain before closing.
- The setup hotspot starts with DHCP at 192.168.4.1; the application reaches
  its ready message and returns from `app_main` without a startup panic.

## Remaining checks

The owner must supply their Muse and TTS credentials in the setup page.
End-to-end Muse pairing, Wi-Fi provisioning, cloud recognition, reply generation,
TTS streaming, long Chinese replies and generated-character updates have not
been validated on this CoreS3. They reuse the upstream implementations.

Battery percentage and motion wake are not connected in this port. Firmware
updates use USB/esptool; the original S31 Recovery package is not applicable.
