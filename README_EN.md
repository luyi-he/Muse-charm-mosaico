# Muse Charm · Mosaico

A voice AI companion for ESP-Mosaico, powered by Muse Gadget SDK, with push-to-talk conversation, Qwen speech, eight animated character states, Chinese/English UI and phone Wi-Fi setup. Generate and replace characters through Muse.

[中文](README.md) · [Illustrated setup guide (Chinese)](docs/SETUP_CN.md) · [Build](docs/BUILDING.md)

## Device demo

[![Watch the device demo](docs/assets/setup/12-muse-online.png)](https://github.com/samyeei/Muse-charm-mosaico/raw/refs/heads/main/docs/assets/demo/muse-charm-demo.mp4)

[Watch / download the demo](https://github.com/samyeei/Muse-charm-mosaico/raw/refs/heads/main/docs/assets/demo/muse-charm-demo.mp4): about 39 seconds, with audio. Shows conversation, character motion and settings. Recording location metadata was removed and the background header was blurred.

Features: a black background and original cream plush character with eight animated states; Qwen3-TTS-Flash-Realtime / Cherry; optional SiliconFlow and OpenAI-compatible PCM services; Chinese/English settings; live volume and brightness sliders; adjustable speech rate, auto pagination and screen timeout; touch and motion wake-up; phone provisioning over a password-free device hotspot; Muse-generated character previews and resource updates.

Bring your own Muse SDK token and TTS API key. The source and included firmware contain no personal credentials. Provision through the device hotspot at http://192.168.4.1; saving closes the hotspot and restarts the device. Use the Muse app for Wi-Fi pairing.

## Ask a local agent to install

Connect your ESP-Mosaico with a USB data cable, then send this prompt to Codex or another local agent with access to your computer and USB device:

> Install the latest firmware from https://github.com/samyeei/Muse-charm-mosaico onto my USB-connected ESP-Mosaico: read the repository instructions, download the code, prepare dependencies and use mosaico.py to perform a system-update with the complete firmware bundle provided in the repository; preserve device identity, existing credentials and pairing, do not erase the whole flash, verify that the device starts normally after installation, and explain how to connect to its setup hotspot and open 192.168.4.1 for first-time configuration.

The agent needs local file, network and device access. It may request permissions or ask you to enter recovery mode physically. After installation, enter your own credentials on the phone configuration page; see the [illustrated guide (Chinese)](docs/SETUP_CN.md).

## Manual installation

Install Python 3.10+ and Git, run `python tools/bootstrap.py`, then follow the installation commands in the Chinese README. Prebuilt retained-Recovery firmware is in `firmware/`. Device operations use `mosaico.py`; do not erase the whole flash. Video and accessory light-sensor integration are not implemented. The simulator mocks cloud/audio services.

Application code: Apache-2.0. Bundled SDK and fonts retain their licenses; see NOTICE. Dependency revisions are pinned in DEPENDENCIES.json. The internal firmware project remains `muse_companion`.
