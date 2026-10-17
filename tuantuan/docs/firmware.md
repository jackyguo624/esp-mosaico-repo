# 团团桌面陪伴助手固件

480×480 GSP 触屏，点击录音，16kHz WAV 请求局域网语音服务，流式播放16kHz PCM回答，镜像任务与定时提醒。

- `main/companion_ui.c`：设备/PC共用的界面、表情、任务页与设置页。所有GSP调用由UI任务执行。
- `main/companion_app.c`：拥有音频和网络请求的后台任务；通过队列接收操作，通过互斥锁提供UI快照。
- `main/companion_net.c`：Wi-Fi、10分钟配网入口、NVS连接配置、带证书验证的HTTPS客户端。
- `ui/main.json`：原生界面场景与中文字体字符集。
- `pc/platform_pc.c`：模拟器的可控数据适配器；设备驱动不进入PC构建。

使用工作区指定的ESP-IDF提交、`esp32s31`目标、GSP 1.5.1和GSPC 0.6.1。保留官方Recovery前缀；新应用与资源安装使用`mosaico.py iris system-update`。

1.0.1 修复了配网客户端地址判断：启用 IPv6 的 ESP-IDF HTTP 服务会将 IPv4 热点客户端返回为 IPv4 映射 IPv6 地址。现在同时支持两种地址表示，并继续拒绝热点子网之外的客户端。配网关闭与热点未连接的页面提示分别使用中文，拒绝响应不缓存。

地址检查回归测试可在桌面运行，覆盖正常客户端、映射地址、其他网络、短地址数据，以及真实双栈 socket 的 getpeername 返回值：

```sh
cc -std=c11 -Wall -Wextra -Werror -I main tests/provision_peer_test.c -o /tmp/tuantuan-provision-peer-test
/tmp/tuantuan-provision-peer-test
```

固件源码在 `tuantuan/`。完整部署说明见 [项目 README](../README.md)，电脑端服务在 `../server/`。上面的回归命令从 `tuantuan/` 运行。
