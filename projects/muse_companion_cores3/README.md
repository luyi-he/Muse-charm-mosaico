# M5Stack CoreS3 · USB Codex 语音伙伴

保留原项目的角色和界面，使用 USB 数据线连接本机 Codex CLI。
CoreS3 版本不连接 Muse 服务，不需要 Muse token、手机配网或设备 Wi-Fi。
Codex 使用电脑已有的 CLI 登录；推理仍通过该账号连接云端服务。

## 操作

- 录音键是屏幕最底部的绿色「按住说话」区域。按住录音、松开发送，单次最长 20 秒。
- 右上角喇叭图标切换静音；齿轮打开设置。设置里的音量和亮度支持拖动。
- CoreS3 的电源/复位键不是 AI 键；GPIO0 是音频 MCLK，不作为录音输入。
- 电量来自 AXP2101 的电池存在位和 SOC 寄存器；无有效读数时显示 `--`。
- 屏幕保持常亮；保留回复字幕翻页，设置页不再提供熄屏时间。
- 「Codex 未连接」表示电脑桥接程序未启动、尚未就绪或心跳中断。

## 电脑桥接程序

需要 Windows、Python 3.11 及以上、已登录的 Codex CLI。首次安装：

```powershell
./projects/muse_companion_cores3/bridge/Setup-Bridge.ps1
```

启动并保持运行：

```powershell
./projects/muse_companion_cores3/bridge/Start-Bridge.ps1 -Port COM4
```

打开 `http://127.0.0.1:8765` 查看状态、识别文字、回答和耗时，
也可以输入文字测试。页面支持列出、选择、读取、创建本机 Codex 聊天。
选择已有聊天后，后续输入尝试继续该聊天；「返回语音助手」恢复管理模式。
也可以说「列出最近三个任务」「创建一个聊天，叫桌面助手」。
新聊天由 App Server 创建，会出现在 Codex 的聊天列表里。
程序保存选中的聊天 ID，重启后可继续；列表显示最近 50 个聊天。
若 Codex 桌面程序持有该聊天的写入权，App Server 会拒绝继续，即使界面上聊天已闲置。
桥接会明确显示并朗读「指令尚未发送」，不会复制会话、强制接管或重复提交。
可在桌面聊天中操作，再说「读取当前会话最新回复」听取结果；这条指令只读取记录，无需接管聊天。

语音设置支持：

- Gemini：`gemini-3.5-flash-lite` 识别，`gemini-3.8-flash-lite-tts` 流式朗读，默认 Aoede，页面可换声音。
- OpenAI：`gpt-4o-mini-transcribe` 识别，`gpt-4o-mini-tts` 流式朗读，默认 Marin。
- Qwen：本地 Whisper 识别，Qwen3-TTS-Flash-Realtime / Cherry 流式朗读。
- 默认：本地 Whisper base（CPU/int8）和 Windows 中文朗读。页面列出本机已安装的中文声音。
  此电脑可用慧慧、瑶瑶（女声）和康康（男声），通过 Windows 原生语音接口生成。

云端音频服务使用各自的 API 密钥，受余额和配额限制，不能共用 Codex 订阅。
音频收到后即通过 USB 播放；OpenAI PCM 从 24 kHz 重采样到设备的 16 kHz，
Gemini 直接请求 16 kHz PCM。诊断日志记录识别、Codex 回答及首段音频耗时。
密钥仅保存在程序内存，关闭程序后需重新填写；也可使用进程环境变量
`DASHSCOPE_API_KEY`、`OPENAI_API_KEY`、`GEMINI_API_KEY`。
密钥不发送给 Codex，不写入仓库或固件。云端语音处理录音与需要朗读的回答。

桥接程序使用官方 Codex **App Server**（`codex app-server --listen stdio://`），
继承已登录 CLI 的账号和默认模型。会话管理通过 App Server 完成。
语音回合使用只读沙箱并拒绝审批请求，不支持执行需要批准的电脑操作。
状态页仅监听电脑回环地址，不向局域网开放。

## 硬件与开发手册

- [CoreS3 手册](https://docs.m5stack.com/en/core/CoreS3)
- [官方 BSP 教程](https://docs.m5stack.com/en/esp_idf/m5cores3/bsp)
- [Espressif CoreS3 BSP](https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_core_s3)
- [M5Unified AXP2101 电量读取](https://github.com/m5stack/M5Unified/blob/master/src/utility/power/AXP2101_Class.inl)
- [Codex App Server](https://learn.chatgpt.com/docs/app-server)

ESP32-S3、16 MB Flash、8 MB quad PSRAM，320×240 LCD、FT6336 触摸、
ES7210 麦克风和 AW88298 喇叭。外设引脚和供电使用官方 BSP。
屏幕采用两块 32 行内部 DMA 缓冲、全宽分块刷新；角色姿态固定位置，
避免每 100 ms 移动透明图片产生可见的局部重绘。
触摸显式轮询；UI 不再受 Muse 首次配置模式锁定。

## 构建与烧录

使用 ESP-IDF 6.0.3，GSP 1.4.0 要求 IDF >=6.0。
初始化仓库固定版本的子模块并进入 IDF 环境：

```powershell
python tools/bootstrap.py
python -m pip install -r requirements.txt
python projects/muse_companion_cores3/tools/prepare_ui.py
python submodule/esp-mosaico-utils/mosaico-tools/tools/gsp-sim/fetch_gspc.py
. C:/Espressif/frameworks/esp-idf-v6.0.3/export.ps1
idf.py -C projects/muse_companion_cores3 build
idf.py -C projects/muse_companion_cores3 -p COM4 flash
```

烧录前备份设备；关闭占用 COM4 的桥接程序或串口监视器。
使用本工程生成的烧录参数，不整片擦除，也不使用 S31 的 `.irisfw`。
保留 `sysmeta`（0xc000）、NVS（0x200000）和旧 factory 字节（`legacy_fw`），
新应用位于 0x210000，UI 位于 0xd00000。旧 Muse 凭据留在 NVS，但本版本不读取它们。
项目局部 CMake 兼容 IDF6 的 LCD 字段和 codec 驱动依赖，未修改托管组件或子模块。

验证记录见 [TESTING.md](TESTING.md)。原 ESP-Mosaico 工程仍在 `projects/muse_companion`。
