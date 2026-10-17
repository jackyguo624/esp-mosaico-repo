# 团团 · ESP-Mosaico 桌面陪伴助手

面向用户的 ESP-Mosaico V1.2.1（由官方 BSP 读取 eFuse 验证硬件映射），采用 ESP32-S31 和官方 ESP-Mosaico Vibe v26.0.2 框架。设备负责触屏、录音与播放；本机提供 Faster-Whisper 中文 ASR、DeepSeek 文字对话和 macOS 中文 TTS。

## 第一版功能

1.0.3 已修复并验证摄像头采集初始化，增加具体错误提示；实机已识别 SC101IOT 并反复正常取帧。见 [摄像头验证记录](docs/camera-fix-1.0.3.md)。

1.0.2 已通过编译、模拟器及电脑端回归，并在本轮设备成功安装和健康启动；基础固件已更新为仓库评审的 Vibe Mode 0.1.4。配网、摄像头与实际语音交互的验收进度见本轮验证记录。1.0.2 改进离线连接入口、任务空状态、详情返回位置、任务操作的保存/失败反馈，以及眨眼的眼睛高光。新 UI 资源需要完整 `system-update`；构建和真机验证完成前不要把源码版本当作已安装版本。触屏填写、扫码确认、语音就绪提示和减少重复刷新也包含在本次改动中。

- 点击“开始说话”录音，再点一次结束；每次最长20秒。识别后的文字送给 DeepSeek，回答通过设备扬声器播放；可点击停止播放。
- 团团会眨眼，被摸时露出开心表情，倾听、思考和说话有不同状态。
- 语音记录待办，创建一次性、每天、工作日或每周重复的提醒；触屏查看、完成、删除任务，或稍后10分钟提醒。
- 服务端 SQLite 保存任务、最近12条对话消息和请求编号；网络重试同一请求不会重复添加任务。第一版最多16条未完成任务。
- 设备将任务镜像存入自己的 NVS 命名空间。在线可语音提醒；服务端断连时，已缓存且有有效时钟的提醒可显示通知并播放提示音。完成、删除和稍后提醒需要服务端在线。
- DeepSeek Key 只放在电脑端，不编入设备固件。设备通过独立连接口令访问语音服务。

## 电脑端运行

电脑端建议使用 Python 3.12；仓库工具要求 Python 3.10+。在 `server/` 目录中：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-asr.txt
python3 setup.py
# 编辑 config.env：填写 DEEPSEEK_API_KEY；setup.py 已生成随机 DEVICE_TOKEN
.venv/bin/python doctor.py
.venv/bin/python server.py --config config.env
```

仓库提供 `config.example.env` 模板。运行 `setup.py` 生成本机 `config.env` 与随机连接口令，再填写自己的参数；真实配置由 `.gitignore` 忽略，不应提交到 Git。

macOS 默认使用 `say` 的 `Tingting` 中文音色，并转换为16kHz单声道PCM。`say -v '?'` 可以查看音色。Linux/Windows 可配置 `TTS_ENGINE=piper`、`PIPER_BIN`、`PIPER_MODEL`，使用支持中文的 Piper 模型；这条路径需要另行安装并验证。

ASR 默认 `small`，第一次运行会从模型托管服务下载本地模型，后续直接加载本地缓存。可以把 `ASR_MODEL` 改为已有 CTranslate2 模型目录。对话需要 DeepSeek 网络连接，ASR/TTS 处理在本机进行。电脑/服务器应保持运行，设备与它接入同一个家庭 Wi-Fi。

`start.command` 是 macOS 启动脚本，会在自身目录创建虚拟环境并安装 ASR 依赖。正式部署可以将服务放到常开的电脑或家用服务器。第一版仅为本地运行，未添加开机自启动。

## 设备配网

当前源码版本为1.0.3。请按下文从源码构建并安装；本机已有的 `firmware/tuantuan-1.0.3.irisfw` 等编译包可能包含编译机路径，不随源码上传。安装后需要配网，服务地址应填写运行电脑端服务的局域网 IP，例如 `http://192.168.1.10:8765`。

1.0.2 提供三种输入方式（1.0.1 仅支持手机辅助配网）：

- **触屏填写**：设置 → 触屏填写，输入 Wi-Fi 名称、密码、电脑服务地址和设备连接口令，再点“保存并重启”。键盘有大小写和符号页，秘密字段默认遮盖，可临时显示。中文 Wi-Fi 名称可用二维码导入。
- **扫码导入**：电脑本地生成二维码，设备设置 → 扫码导入，对准左侧摄像头。识别后先显示遮盖的确认页，点击保存才生效。连接二维码保存 Wi-Fi、电脑地址和连接口令；API Key 二维码把 Key 发送给已经连接的电脑服务，设备不将模型 Key 写入 NVS。
- **手机辅助配网**：点击该按钮后设备开放10分钟热点。手机连接屏幕显示的 `Tuantuan-xxxx` 和密码，访问 `http://192.168.4.1`，填写连接配置并保存。设备会重启，电脑应回到家中 Wi-Fi。

在 `tuantuan/server/` 目录生成二维码：

```sh
.venv/bin/python -m pip install -r requirements-qr.txt
# 交互输入 Wi-Fi 名称、密码；自动读取本机连接口令
.venv/bin/python make_qr.py --kind connection
# 也可先在私有 config.env 填写 WIFI_SSID/WIFI_PASSWORD，再读取生成
.venv/bin/python make_qr.py --kind connection --from-config
# 从本机 config.env 读取已经填写的模型 Key
.venv/bin/python make_qr.py --kind api-key
```

生成文件在本机 `server/data/qr/`，不会使用在线二维码服务或输出密钥。二维码等同于其中的配置，使用后可删除文件，不要上传或公开分享。没有摄像头模块时，可继续使用键盘或手机配网。导入 API Key 前先完成设备连接；在设置表单右上角切换“模型Key”，也可直接输入。

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

应用固件、UI、模拟器、服务端和发行包均在 `tuantuan/` 中，共用工具在 Repo 的 `firmware-workspace/` 中。先在仓库根目录执行 `git submodule update --init --recursive firmware-workspace/submodule/esp-mosaico-utils firmware-workspace/submodule/esp-mosaico-bsp`，取得已固定的官方依赖。固件使用该工作区指定的 ESP-IDF 提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，目标 `esp32s31`。请勿将目标替换为 ESP32-S3。

```sh
# 在 Repo 根目录中，先激活指定 ESP-IDF 环境
python3 firmware-workspace/submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project tuantuan doctor
python3 firmware-workspace/submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project tuantuan build
python3 mosaico.py project sim --project tuantuan --interactive
# 安装新应用和中文UI资源，使用官方 System Update
python3 mosaico.py iris system-update --project tuantuan
```

保留官方前2MB Recovery分区布局。中文字体资源分区调整为5MB，应用分区为约9MB，因此新安装和UI资源变更需要 System Update。单独的应用 `.bin` 不包含中文界面资源；不要把它当成在线刷机页面要求的完整镜像，不要猜测偏移地址进行烧录。

## 数据、接口与测试

- `server/data/tuantuan.sqlite3`：任务与短期对话历史，可在停止服务后备份。录音不持久保存。ASR模型缓存位于 `server/data/models/`。
- `/health`：运行状态；其余 `/v1/*` 接口需要 Bearer连接口令和 `X-Device-Id`。
- `POST /v1/audio`：16kHz/16bit/mono WAV，`X-Turn-Id` 用于防重。
- `GET /v1/state`：任务、已到时间的提醒、时间同步和语音就绪状态。
- `POST /v1/config/key`：经设备口令鉴权，将新模型 Key 原子保存到电脑配置。
- `POST /v1/task`：完成、删除、稍后提醒及已播报确认。
- `GET /v1/speech/<id>`：16kHz/16bit/mono little-endian PCM。

```sh
cd server
python3 -m unittest discover -s tests -v
```

当前电脑端38项回归测试覆盖配置更新鉴权、原子保存、二维码字段校验、ASR预加载与错误处理、标准WAV直接识别、TTS缓存，以及请求防重、事务回滚、重启保存、不同设备数据隔离、过去/无时区时间拒绝、任务数量限制、重复提醒与稍后提醒，以及TTS失败后已记录任务不丢失。

## 已知范围

第一版采用点击开始说话。唤醒词、声学回声消除、边播放边说话、任意外部应用操作和复杂定时工作流未启用。提醒时区固定为 Asia/Shanghai，轮询提醒最多约10秒延迟。设备关机期间无法发声；重新连上服务后会处理尚未播报的一次性提醒。中文显示覆盖GB2312字符及基本标点，生僻字或emoji可能没有字形。

模拟器与硬件共用 C UI 和场景文件；模拟器中的语音、联网和任务数据由可控适配器提供。旧版已有实机启动/配网/首次语音和真实模型/TTS测试；1.0.2的验证状态见 [本轮验证记录](docs/setup-and-validation-1.0.2.md)。每台设备仍应验证麦克风、扬声器、触控、Wi-Fi和定时提醒。`docs/` 仅保留脱敏后的验证摘要；原始日志和设备截图保留在本机。

## 官方资料与许可

- [ESP-Mosaico 硬件](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp-mosaico/index.html)
- [官方开发工作区](https://github.com/esp-mosaico/esp-mosaico-vibe)
- [板级驱动](https://github.com/esp-mosaico/esp-mosaico-bsp)
- [DeepSeek API](https://api-docs.deepseek.com/)
- [Faster-Whisper](https://github.com/SYSTRAN/faster-whisper)

应用 C 代码采用 Apache-2.0 标识，官方组件遵循各自许可；中文字体采用 Noto Sans CJK SC，随源码包含其 OFL 许可文本。密钥、用户任务数据库和第三方模型缓存不包含在可分享源码包中。
