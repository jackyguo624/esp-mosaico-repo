本目录基于官方 ESP-Mosaico Vibe v26.0.2 的工作区文件快照。精确来源见 SOURCE_REVISIONS.json。

BSP、utils、raylib-lite-engine 由父仓库 Git 子模块固定到对应提交。在仓库根目录执行 `git submodule update --init --recursive` 后，再进入此目录使用 mosaico.py。

不包含 ESP-IDF、工具链、构建目录、第三方下载缓存或设备配置。若需创建其他新项目，可先克隆官方仓库相应标签，再导入 projects/tuantuan。
