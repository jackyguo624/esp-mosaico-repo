本目录基于官方 ESP-Mosaico Vibe v26.0.2 的工作区文件快照。精确来源见 SOURCE_REVISIONS.json。

BSP、utils、raylib-lite-engine 由父仓库 Git 子模块固定到对应提交。Repo 根目录提供 `mosaico.py` 与 `.mosaico.json`；应用在 Repo 的同级子文件夹，例如 `tuantuan/`。从 Repo 根目录初始化 `firmware-workspace/submodule/` 下所需依赖并使用命令入口。

不包含 ESP-IDF、工具链、构建目录、第三方下载缓存或设备配置。新增项目见 [多 App 工作区](docs/multi-app_CN.md)，通过官方模板在 Repo 根目录创建新 App。
