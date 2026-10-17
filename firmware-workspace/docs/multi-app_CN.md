# 在同一个 Repo 中开发多个 App

Repo 根目录是共用工作区。团团的全部应用文件在 `tuantuan/`；官方开发工具、技能与固定版本依赖在 `firmware-workspace/`。下文命令均从 Repo 根目录运行。

## 新增固件 App

实际创建前，检查官方工作区最新发布标签，并与 `firmware-workspace/SOURCE_REVISIONS.json` 对照。升级共用依赖会影响所有 App，应先确认所选版本；目录整理本身不升级依赖。

```sh
git ls-remote --tags --refs https://github.com/esp-mosaico/esp-mosaico-vibe.git
git submodule update --init firmware-workspace/submodule/esp-mosaico-utils
python3 mosaico.py project init my_app --dry-run
python3 mosaico.py project init my_app
```

官方 Hello World 模板创建 `apps/my_app/`。已交付的团团保持在 `tuantuan/`；新 App 使用独立的 `apps/<name>/` 文件夹。固定版官方创建工具要求专用的项目目录，不能把 `projects_dir` 设置为 `.`。它包含固件、UI 和原生模拟器，并通过相对路径引用共用依赖。每个 App 独立构建，服务端和应用专属文档也应放在自己的目录中。

当存在多个 App 时，明确选择 `--project apps/my_app`。新建项目不会自动改变默认项目。

## 构建与预览

使用 Python 3.10+，先核对 ESP-IDF 的完整提交 SHA：

```sh
git -C "$IDF_PATH" rev-parse HEAD
# 必须为 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe
python3 firmware-workspace/submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project tuantuan doctor
python3 firmware-workspace/submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project tuantuan build
python3 mosaico.py project sim --project tuantuan --interactive
```

目标始终为 `esp32s31`。修改 UI 后先用共用的原生 C 界面验证点击、页面切换和状态变化，再安装到设备。

## 设备安装

```sh
python3 mosaico.py iris status --all --json
python3 mosaico.py iris list --details --json
python3 mosaico.py iris device-status --project tuantuan --json
python3 mosaico.py iris system-update --project tuantuan
```

已发布的完整团团包可作为 `system-update` 的输入：

```sh
python3 mosaico.py iris system-update --project tuantuan --bundle tuantuan/firmware/tuantuan-1.0.1.irisfw
```

新 App、分区或外部资源变化使用 `system-update`。只有代码变化且完整分区表相同才使用 `app-update`。切换 App 会替换当前运行的应用；使用官方命令保留 Vibe Mode。只有空白设备或排查后 normal/Vibe Mode 均不可达时才进入 `recover` 流程。

## 团团的电脑端服务

```sh
cd tuantuan/server
python3 setup.py
# 在本机 config.env 中填写 DEEPSEEK_API_KEY
./start.command
```

配置和运行数据属于本机，不分享或提交。其他 App 可使用自己的 `server/`，并选择不同服务端口，避免相互占用。
