# Muse Companion for M5Stack CoreS3

CoreS3-specific project sharing the original Muse communication, TTS, credentials,
settings and character model. The original ESP-Mosaico project remains available
under `projects/muse_companion`.

## Hardware and sources

- [CoreS3 hardware manual](https://docs.m5stack.com/en/core/CoreS3)
- [Official ESP-IDF BSP tutorial](https://docs.m5stack.com/en/esp_idf/m5cores3/bsp)
- [Espressif CoreS3 BSP](https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_core_s3)

ESP32-S3, 16 MB flash, 8 MB PSRAM; 320x240 LCD, FT6336 touch,
ES7210 microphone and AW88298 speaker. The BSP owns peripheral pins and power rails.
GSP owns the UI; no second LVGL display is started. The LCD's staging buffer uses
internal DMA memory because the S31 PSRAM DMA configuration does not apply to S3.
Task stacks remain in internal RAM; the complete CJK font stays in flash instead
of exhausting the CoreS3's 8 MB PSRAM by copying the application and font there.
The display uses one 12-line internal DMA buffer. Its 64 KB render stack is
allocated before Wi-Fi starts, avoiding fragmented internal memory at startup.

Hold the on-screen talk button to record; release to send. During Muse pairing,
pressing this control confirms the active pairing session. The hardware reset
button is not used as an AI input. Motion wake and battery percentage are not
connected in this initial port; touch wake and brightness controls remain available.

## Build

Use ESP-IDF 6.0.3 and its ESP32-S3 tools. GSP 1.4.0 requires IDF >=6.0, so the
older IDF version used in the CoreS3 tutorial cannot build this application.
Initialize the pinned submodules from
the repository root, then activate your IDF environment:

```powershell
python tools/bootstrap.py
python -m pip install -r requirements.txt
python projects/muse_companion_cores3/tools/prepare_ui.py
python submodule/esp-mosaico-utils/mosaico-tools/tools/gsp-sim/fetch_gspc.py
. C:/Espressif/frameworks/esp-idf-v6.0.3/export.ps1
idf.py -C projects/muse_companion_cores3 build
```

The component manager pins CoreS3 BSP 3.0.0 and GSP 1.4.0. `prepare_ui.py`
generates 320x240 scenes and scaled image assets while preserving the original
scene names, event bindings and dynamic properties.
Project-local CMake compatibility settings add the codec's concrete driver
dependencies and map removed LCD field names to IDF 6 names. Managed dependency
sources and the original submodules are not patched.

## Flash and configure

Back up the existing flash before installation. Use this project's generated
flash arguments; do not use the original S31 `.irisfw` package or whole-chip erase.

```powershell
idf.py -C projects/muse_companion_cores3 -p COM4 flash monitor
```

The partition layout retains `sysmeta` at 0xc000, NVS at 0x200000 and the original
factory bytes as `legacy_fw`. The new factory application runs at 0x210000 and
the GSP assets at 0xd00000. This is a CoreS3 layout, not a Mosaico Recovery layout.
Firmware updates use ESP-IDF/esptool. Iris is available over paired TCP after
network configuration; USB remains the normal Serial/JTAG console.

On first boot, connect to the open `Mochi-Setup-XXXX` Wi-Fi and visit
`http://192.168.4.1`. Enter your Muse token and TTS credentials, then save and
pair/configure Wi-Fi through Muse. No credentials are included in the repository.

## Validation

See [device validation](TESTING.md) for build and hardware evidence and the
remaining account-dependent checks. On first-time setup, a short tone tests
the speaker and a 16 kHz capture reports microphone read status without saving
or transmitting the captured samples.
