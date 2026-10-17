#!/usr/bin/env python3
"""Prepare private local configuration without printing credentials."""
import os
from pathlib import Path
import secrets
import socket


def prepare_config(directory):
    directory = Path(directory)
    target = directory / 'config.env'
    if target.exists():
        return target, False
    content = (directory / 'config.example.env').read_text()
    content = content.replace('replace-with-a-long-random-token', secrets.token_urlsafe(32))
    content = content.replace('TIMEZONE=Asia/Shanghai', 'TIMEZONE=Asia/Singapore')
    # Exclusive creation preserves an existing configuration, including concurrent setup.
    try:
        fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        return target, False
    with os.fdopen(fd, 'w') as stream:
        stream.write(content)
    return target, True


def lan_address():
    # UDP connect determines the local route; it does not send a packet.
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
            connection.connect(('192.0.2.1', 9))
            return connection.getsockname()[0]
    except OSError:
        return None


def main():
    path, created = prepare_config(Path(__file__).resolve().parent)
    print(('已创建' if created else '已保留') + f'本机配置：{path}')
    print('请在该文件填写 DEEPSEEK_API_KEY；连接口令已自动生成，请勿发到聊天或提交到 Git。')
    print('运行 ./start.command 启动服务，再运行 .venv/bin/python doctor.py 检查配置。')
    address = lan_address()
    if address:
        print(f'设备服务地址（默认端口）：http://{address}:8765')
    else:
        print('电脑连上家庭 Wi-Fi 后，使用电脑局域网 IP 填写设备服务地址。')


if __name__ == '__main__':
    main()
