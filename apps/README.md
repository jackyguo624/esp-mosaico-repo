# 其他固件 App

从 Repo 根目录使用官方模板创建独立 App：

```sh
python3 mosaico.py project init my_app --dry-run
python3 mosaico.py project init my_app
```

创建前先核对官方最新标签与共用依赖版本，流程见 [多 App 指南](../firmware-workspace/docs/multi-app_CN.md)。生成目录为 `apps/my_app/`；使用 `--project apps/my_app` 选择它。已有团团保持在 `../tuantuan/`，与新 App 共用开发工具。
