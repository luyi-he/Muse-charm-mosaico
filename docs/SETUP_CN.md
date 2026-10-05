# Muse 伙伴手机首次配置

2026-10-05：首次配置功能与可配置 TTS 已安装并完成本轮真机验收。用户确认手机热点、中英文配置页、保存重启与恢复 Muse 在线均正常。保存后的 Boot ID 为 `1689776007514271120`，同一 Device ID 在线 idle；日志确认 `setup=done wifi=up ws=up`、收到 Muse 文字回复及 TTS `success=1`。设备配对与偏好保留。断电重开、设置重新配置和其他 TTS 提供方仍是独立待验项。

## 使用

1. 未保存服务配置时，设备显示首次配置，开启 `Mochi-Setup-XXXX` Wi-Fi 热点。热点无需密码，直接连接。
2. 手机连接热点；系统提示无互联网时选择保持连接。浏览器打开 `http://192.168.4.1`，无需电脑输入 token。
3. 页面右上角可切换中文与 English，已输入内容保留。填写 Muse **SDK token**，它与 Muse App 账号配对得到的访问凭据不同。
4. 选择千问默认配置，或硅基流动/OpenAI/自定义兼容服务。千问填写百炼 Key 和完整 WSS 地址，其他服务填写 HTTPS 语音 API 地址、TTS token、模型名称与音色名称。
5. 保存成功后约两秒自动重启。手机切回常用网络，在 Muse App 中完成 Wi-Fi 配置与账号配对；已配对设备继续使用原配对记录。
6. 按住设备顶部 AI 键提问，松开发送，检查文字回答与朗读。设置中的语速对下一条 TTS 生效；中文/English 设置继续控制界面和对话语言指令，不翻译旧消息。

token 首次未填写或配置记录不完整时自动进入热点模式。已配置启动不开放后台；在设备设置中点击“手机配置”，会保存一次配置模式标记并重启进入热点，再完整填写全部服务字段。提交成功后退出配置模式；中途断电则下次继续配置。此操作保留旧凭据、Muse 配对与设备身份，未提交新值不会覆盖旧凭据。本次不提供恢复出厂或清空身份功能。

## TTS 兼容范围

| 类型 | 请求与响应 | 音频处理 |
| --- | --- | --- |
| 阿里千问实时 | 百炼 Key；WSS `session.update`、文本提交；默认 `qwen3-tts-flash-realtime` / `Cherry`，音量 50、不追加数字增益 | Base64 事件解码为原始 PCM16，16kHz；确认服务返回的采样率 |
| 硅基流动兼容 | Bearer token；JSON `model/voice/input/response_format=pcm/speed`，另加 `sample_rate=16000/stream=true/gain=6` | 原始单声道有符号 PCM16，小端，16kHz |
| OpenAI 兼容 | Bearer token；JSON `model/voice/input/response_format=pcm/speed` | 原始单声道有符号 PCM16，小端，24kHz，流式转为 16kHz，饱和增益 |

API 地址填写完整端点，例如 `https://api.siliconflow.cn/v1/audio/speech`，不是只填域名。模型与音色必须由对应服务提供。HTTP 200 的 JSON 错误、MP3/WAV、带音频头或 URL/base64 响应不属于当前支持范围；非兼容服务需单独适配。

硅基流动预设模型 `fnlp/MOSS-TTSD-v0.5`、音色 `fnlp/MOSS-TTSD-v0.5:claire`；请求自动添加单说话人标记 `[S1]`。旧版默认 CosyVoice2 配置在首次升级时迁移，保留 token；自定义配置保持原值，之后手机保存的选择不会被覆盖。当前账号没有 Qwen TTS。OpenAI 预设 `tts-1/alloy` 仅用于填写便利，当前未调用 OpenAI 云端验收。接口参考：[硅基流动](https://docs.siliconflow.cn/docs/api/audio-speech-post)、[OpenAI PCM 说明](https://developers.openai.com/api/docs/guides/text-to-speech)。

页面只验证必填、长度、字符与 HTTPS 地址格式，不能在无互联网的热点中确认云端凭据有效。保存失败保留填写内容并允许重试，保存成功清空页面 token。

## 保存与重配

默认预设是阿里百炼 `qwen3-tts-flash-realtime` / `Cherry`，北京地域 WSS 地址为 `wss://dashscope.aliyuncs.com/api-ws/v1/realtime`。填入自己的百炼 API Key，设备请求并确认 PCM16/16kHz 音频；不追加数字放大增益。硅基流动、OpenAI 和自定义兼容服务可在提供方下拉框选择。

配置保存在本机 NVS，不嵌入固件；保存后重启，配置热点关闭。Muse 配对信息和设置分别保存，更新固件不要求清除配对。在设备设置中点击“手机配置”，可以再次开启热点并重新填写服务字段；页面不回显已保存的 Key。

完整安装见仓库根目录 README.md，源码构建见 BUILDING.md。当前千问版已真机验证多轮对话和朗读，用户确认电流音消失、声音顺滑。其他自定义服务以及断电后的不同重配路径需在实际使用环境中单独验证。
