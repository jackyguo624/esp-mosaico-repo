# ESP-Mosaico 固件 App 工作区

在同一个 Repo 中开发多个 ESP-Mosaico App。每个 App 放在独立子文件夹，共用固定版本的官方工具和板级依赖。

```text
esp-mosaico-repo/
├── mosaico.py                 # 共用命令入口
├── .mosaico.json              # 工作区配置；新 App 创建在 apps/
├── firmware-workspace/        # 官方工具、依赖、开发文档与技能
│   └── submodule/
├── tuantuan/                  # 团团完整项目
│   ├── main/、ui/、pc/         # 设备固件与原生模拟器
│   ├── server/                # 本机语音、对话与提醒服务
│   ├── firmware/              # 固件清单与校验值；编译包仅在本机保存
│   └── docs/
└── apps/                      # 后续固件 App 的独立项目文件夹
```

从仓库根目录运行命令，使用 Python 3.10+：

```sh
git submodule update --init --recursive firmware-workspace/submodule/esp-mosaico-utils firmware-workspace/submodule/esp-mosaico-bsp
python3 mosaico.py iris status --all --json
python3 mosaico.py iris list --details --json
```

团团是奶油色界面、薄荷色小猫的桌面陪伴助手，支持中文对话、待办和语音提醒。

![团团界面](tuantuan/docs/images/native-home.jpg)

- [团团安装与配置](tuantuan/README.md)
- [第一次连接设备](tuantuan/docs/getting-started.md)
- [共用工作区与新增 App](firmware-workspace/docs/multi-app_CN.md)
- [官方设备操作指南](firmware-workspace/docs/mosaico-cli_CN.md)

电脑端配置填写到 `tuantuan/server/config.env`。真实配置、含密钥的二维码、任务数据库、聊天记录、模型缓存、固件二进制、原始验证记录和设备状态均由 Git 忽略。

固件固定使用 ESP-IDF 提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，目标 **esp32s31**。完整来源见 [SOURCE_REVISIONS.json](firmware-workspace/SOURCE_REVISIONS.json)。新 App 或 UI 资源变化使用官方 `system-update` 安装；保留设备的 Vibe Mode 和恢复分区。

CI 运行服务端、配网地址及工作区检查。提交前可运行 `python3 scripts/check_private_files.py` 检查 Git 中是否误包含真实配置或运行数据。
