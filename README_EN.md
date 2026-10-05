# Muse Charm · Mosaico

A voice AI companion for ESP-Mosaico, powered by Muse Gadget SDK and configurable TTS. Hold the top AI button to talk and release to send.

[中文](README.md) · [Build](docs/BUILDING.md)

Features: a black background and original cream plush character with eight animated states; Qwen3-TTS-Flash-Realtime / Cherry; optional SiliconFlow and OpenAI-compatible PCM services; Chinese/English settings; live volume and brightness sliders; adjustable speech rate, auto pagination and screen timeout; touch and motion wake-up; phone provisioning over a password-free device hotspot; Muse-generated character previews and resource updates.

Bring your own Muse SDK token and TTS API key. The source and included firmware contain no personal credentials. Provision through the device hotspot at http://192.168.4.1; saving closes the hotspot and restarts the device. Use the Muse app for Wi-Fi pairing.

Install Python 3.10+ and Git, run `python tools/bootstrap.py`, then follow the installation commands in the Chinese README. Prebuilt retained-Recovery firmware is in `firmware/`. Device operations use `mosaico.py`; do not erase the whole flash. Video and accessory light-sensor integration are not implemented. The simulator mocks cloud/audio services.

Application code: Apache-2.0. Bundled SDK and fonts retain their licenses; see NOTICE. Dependency revisions are pinned in DEPENDENCIES.json. The internal firmware project remains `muse_companion`.
