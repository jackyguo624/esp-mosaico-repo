# 团团 · ESP-Mosaico 桌面陪伴助手

面向用户的 ESP-Mosaico V1.2.1（由官方 BSP 读取 eFuse 验证硬件映射），采用 ESP32-S31 和官方 ESP-Mosaico Vibe v26.0.2 框架。设备负责触屏、录音与播放；本机提供 Faster-Whisper 中文 ASR、DeepSeek 文字对话和 macOS 中文 TTS。

## 第一版功能

- 点击“开始说话”录音，再点一次结束；每次最长20秒。识别后的文字送给 DeepSeek，回答通过设备扬声器播放；可点击停止播放。
- 团团会眨眼，被摸时露出开心表情，倾听、思考和说话有不同状态。
- 语音记录待办，创建一次性、每天、工作日或每周重复的提醒；触屏查看、完成、删除任务，或稍后10分钟提醒。
- 服务端 SQLite 保存任务、最近12条对话消息和请求编号；网络重试同一请求不会重复添加任务。第一版最多16条未完成任务。
- 设备将任务镜像存入自己的 NVS 命名空间。在线可语音提醒；服务端断连时，已缓存且有有效时钟的提醒可显示通知并播放提示音。完成、删除和稍后提醒需要服务端在线。
- DeepSeek Key 只放在电脑端，不编入设备固件。设备通过独立连接口令访问语音服务。

## 电脑端运行

首次安装使用 Python 3.10+。在 `server/` 目录中：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-asr.txt
cp config.example.env config.env
# 编辑 config.env：填写 DEEPSEEK_API_KEY，并设置随机 DEVICE_TOKEN（至少16位）
.venv/bin/python doctor.py
.venv/bin/python server.py --config config.env
```

仓库只提供 `config.example.env` 模板。复制为本机 `config.env` 后填写自己的参数；真实配置由 `.gitignore` 忽略，不应提交到 Git。

macOS 默认使用 `say` 的 `Tingting` 中文音色，并转换为16kHz单声道PCM。`say -v '?'` 可以查看音色。Linux/Windows 可配置 `TTS_ENGINE=piper`、`PIPER_BIN`、`PIPER_MODEL`，使用支持中文的 Piper 模型；这条路径需要另行安装并验证。

ASR 默认 `small`，第一次运行会从模型托管服务下载本地模型，后续直接加载本地缓存。可以把 `ASR_MODEL` 改为已有 CTranslate2 模型目录。对话需要 DeepSeek 网络连接，ASR/TTS 处理在本机进行。电脑/服务器应保持运行，设备与它接入同一个家庭 Wi-Fi。

`start.command` 是 macOS 启动脚本，会在自身目录创建虚拟环境并安装 ASR 依赖。正式部署可以将服务放到常开的电脑或家用服务器。第一版仅为本地运行，未添加开机自启动。

## 设备配网

固件包位于 `firmware/tuantuan-1.0.1.irisfw`。安装后需要配网，服务地址应填写运行电脑端服务的局域网 IP，例如 `http://192.168.1.10:8765`。

第一次运行或点击“设置 → 重新配网 / 设置服务器”后，设备开放10分钟配网热点。只打开设置页不会重新开放窗口。热点名称和每次启动生成的密码显示在设置页。

1.0.1 修复了热点客户端地址被 IPv6 双栈网络库映射后误判、导致配网页面拒绝访问的问题。窗口关闭与没有连接设备热点的提示已分别改为中文，并禁止缓存拒绝响应。

1. 电脑/手机连接屏幕上显示的 `Tuantuan-xxxx` 热点。
2. 浏览器访问 `http://192.168.4.1`。
3. 填写家中 Wi-Fi、电脑服务地址（例如 `http://192.168.1.10:8765`）及 `config.env` 中的 `DEVICE_TOKEN`。
4. 保存后设备重启，将电脑切回家中 Wi-Fi。电脑服务地址应采用局域网 IP，不能填 `localhost`。

建议为电脑配置固定的局域网地址或路由器 DHCP 地址保留。服务端默认监听8765端口；如果防火墙询问，允许家庭局域网访问。这个 HTTP 配置用于可信家庭局域网，请勿直接映射到公网；需要跨网络时，可通过带证书的 HTTPS 反向代理，固件会验证证书。

排查离线时，先看设备设置页的 Wi-Fi 状态。服务端只记录 HTTP 状态码、请求方法和路径，正常轮询为 `200 GET /v1/state`。若为 `401 GET /v1/state`，表示设备已能访问电脑，但连接口令不匹配；重新配网并将 `config.env` 中 `DEVICE_TOKEN=` 等号后面的完整值填入连接口令，不包含字段名、引号或额外空格。日志不记录口令、API Key、录音或对话内容。

## 可以这样说

- “帮我记录买咖啡豆。”
- “明天早上九点提醒我提交周报。”
- “每天晚上十点提醒我休息。”
- “每个工作日下午三点提醒我起来走一走。”
- “我现在有哪些任务？”
- “把买咖啡豆这件事标记完成。”
- “今天有点累，陪我聊一会儿。”

时间或任务定位不明确时，助手应先追问。任务修改的成功提示由程序在数据库实际写入后生成。语音合成失败时保留文字和已保存任务，不将成功操作当成失败重试。

DeepSeek 调用采用非思考模式，首个请求输出额度为2048 token。请求中的历史助手消息保持为 JSON 对象形式，数据库仍保存自然对话文字。空内容、截断或结构无效的模型响应最多自动重试一次；重试切换为普通文本请求并继续要求完整 JSON，额度为4096 token，两次请求共享45秒模型等待预算。两种请求均通过同一严格校验：截断内容和普通文字里的 JSON 片段不会作为答案或任务操作执行。任务在有效响应通过校验后才提交，因此格式重试不会重复新增任务。相关说明见 [DeepSeek 思考模式](https://api-docs.deepseek.com/guides/thinking_mode/) 和 [JSON 输出](https://api-docs.deepseek.com/guides/json_mode/)。

## 编译、预览和安装

应用在 `firmware-workspace/projects/tuantuan` 中。先在仓库根目录执行 `git submodule update --init --recursive`，取得已固定的官方依赖。固件使用该工作区指定的 ESP-IDF 提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，目标 `esp32s31`。请勿将目标替换为 ESP32-S3。

```sh
# 在交付的 firmware-workspace/ 中，先激活指定 ESP-IDF 环境
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/tuantuan doctor
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/tuantuan build
python mosaico.py project sim --project projects/tuantuan --interactive
# 安装新应用和中文UI资源，使用官方 System Update
python mosaico.py iris system-update --project projects/tuantuan
```

保留官方前2MB Recovery分区布局。中文字体资源分区调整为5MB，应用分区为约9MB，因此新安装和UI资源变更需要 System Update。单独的应用 `.bin` 不包含中文界面资源；不要把它当成在线刷机页面要求的完整镜像，不要猜测偏移地址进行烧录。

## 数据、接口与测试

- `server/data/tuantuan.sqlite3`：任务与短期对话历史，可在停止服务后备份。录音不持久保存。ASR模型缓存位于 `server/data/models/`。
- `/health`：运行状态；其余 `/v1/*` 接口需要 Bearer连接口令和 `X-Device-Id`。
- `POST /v1/audio`：16kHz/16bit/mono WAV，`X-Turn-Id` 用于防重。
- `GET /v1/state`：任务、已到时间的提醒及时间同步。
- `POST /v1/task`：完成、删除、稍后提醒及已播报确认。
- `GET /v1/speech/<id>`：16kHz/16bit/mono little-endian PCM。

```sh
cd server
python3 -m unittest discover -s tests -v
```

测试覆盖请求防重、事务回滚、重启保存、不同设备数据隔离、过去/无时区时间拒绝、任务数量限制、重复提醒与稍后提醒，以及TTS失败后已记录任务不丢失。

## 已知范围

第一版采用点击开始说话。唤醒词、声学回声消除、边播放边说话、任意外部应用操作和复杂定时工作流未启用。提醒时区固定为 Asia/Shanghai，轮询提醒最多约10秒延迟。设备关机期间无法发声；重新连上服务后会处理尚未播报的一次性提醒。中文显示覆盖GB2312字符及基本标点，生僻字或emoji可能没有字形。

模拟器与硬件共用 C UI 和场景文件；模拟器中的语音、联网和任务数据由可控适配器提供。已有原生交互、实机启动/配网/首次语音、28项服务端回归及六轮真实模型/TTS测试；每台设备仍应验证麦克风、扬声器、触控、Wi-Fi和定时提醒。测试记录位于 `docs/`。

## 官方资料与许可

- [ESP-Mosaico 硬件](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp-mosaico/index.html)
- [官方开发工作区](https://github.com/esp-mosaico/esp-mosaico-vibe)
- [板级驱动](https://github.com/esp-mosaico/esp-mosaico-bsp)
- [DeepSeek API](https://api-docs.deepseek.com/)
- [Faster-Whisper](https://github.com/SYSTRAN/faster-whisper)

应用 C 代码采用 Apache-2.0 标识，官方组件遵循各自许可；中文字体采用 Noto Sans CJK SC，随源码包含其 OFL 许可文本。密钥、用户任务数据库和第三方模型缓存不包含在可分享源码包中。
