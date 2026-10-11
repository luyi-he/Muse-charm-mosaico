# CoreS3 · Codex 桌面语音伙伴

将 M5Stack CoreS3 通过 USB 连接电脑，使用板载触摸屏、麦克风和喇叭与本机 Codex 对话。保留 Muse Charm 的角色界面，本版本无需 Muse 配对、设备 Wi-Fi 或 Muse token。

![角色形象](projects/muse_companion/ui/assets/idle.png)

本分支为 `cores3-port`，源自 [samyeei/Muse-charm-mosaico](https://github.com/samyeei/Muse-charm-mosaico)。[原 ESP-Mosaico 项目说明](README_MOSAICO.md)及其工程保留供参考；其中的顶部 AI 键、Muse 配网和自动熄屏流程不适用于 CoreS3。

## 当前功能

- 底部绿色触摸区域按住录音、松开发送；右上角可静音和进入设置，音量、亮度可调。
- 320×240 角色界面，修复频闪及局部显示问题；屏幕保持常亮，连接状态显示「Codex 已连接」。
- 使用 AXP2101 读取电池状态和电量；缺少有效读数时显示 `--`。
- 通过 Codex App Server 列出、读取、选择、继续和创建本机聊天。新聊天会出现在 Codex 中，选中的聊天 ID 可在重启后恢复。
- 默认使用本地 Whisper 识别和 Windows 电脑朗读。页面可选择本机已安装的中文声音；当前测试电脑有慧慧、瑶瑶、康康。
- 可选 Gemini、OpenAI 或 Qwen 云端音频服务，密钥仅留在桥接程序内存中；默认电脑朗读不需要语音 API Key。

## 使用

需要 M5Stack CoreS3、USB 数据线、Windows 电脑、Python 3.11 及以上和已登录的 Codex CLI。CoreS3 固件构建与烧录见 [详细说明](projects/muse_companion_cores3/README.md)。

首次准备电脑桥接程序，在仓库根目录运行：

```powershell
./projects/muse_companion_cores3/bridge/Setup-Bridge.ps1
```

启动并保持程序运行，端口请按设备实际情况填写：

```powershell
./projects/muse_companion_cores3/bridge/Start-Bridge.ps1 -Port COM4
```

打开 [本机设置页面](http://127.0.0.1:8765/)，确认连接状态后按住设备底部绿色区域说话。可以说「列出最近三个任务」「创建一个聊天，叫桌面助手」，也可在页面选择聊天、读取近期记录、输入文字和切换声音。

**CoreS3 没有本项目所说的顶部 AI 键。** 电源和复位键不是录音键，GPIO0 用于音频时钟。

## 运行范围

语音识别和电脑朗读在本机运行，Codex 对话仍需要其正常网络连接。云端音频服务需要各自的 API 密钥、余额及配额，不与 Codex 订阅共享；它们会处理录音和需要朗读的回答。

语音回合使用只读沙箱并拒绝审批请求。当前支持聊天管理与对话，不能直接执行需要批准的电脑操作。页面只监听本机回环地址。断开 USB 后请重启桥接程序。

烧录前关闭串口占用程序并备份设备。不要整片擦除，不要将 ESP-Mosaico / S31 固件包用于 CoreS3。

## 开发与验证

- [CoreS3 使用、语音配置、引脚说明及构建烧录](projects/muse_companion_cores3/README.md)
- [实机验证记录与尚未验证的项目](projects/muse_companion_cores3/TESTING.md)
- [M5Stack CoreS3 开发手册](https://docs.m5stack.com/en/core/CoreS3)
- [原项目英文说明](README_EN.md)

原工程位于 `projects/muse_companion`，CoreS3 独立工程位于 `projects/muse_companion_cores3`。凭据、个人录音、设备备份和本机运行数据不提交到 Git。
