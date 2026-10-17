#!/usr/bin/env python3
"""Generate a private local SVG QR; no network service receives credentials."""
import argparse
import getpass
import os
from pathlib import Path
import qrcode
import qrcode.image.svg
from configuration import qr_payload
from server import load_config
from setup import lan_address

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--kind',choices=('connection','api-key'),default='connection')
    parser.add_argument('--config',type=Path,default=Path(__file__).with_name('config.env'))
    parser.add_argument('--from-config',action='store_true',help='Read WIFI_SSID/WIFI_PASSWORD from the private config file')
    args=parser.parse_args()
    config=load_config(args.config)
    ssid=password=url=''
    if args.kind=='connection':
        if args.from_config:
            ssid=config.get('WIFI_SSID','');password=config.get('WIFI_PASSWORD','')
            if not ssid or 'WIFI_PASSWORD' not in config:parser.error('请在本机配置文件填写 WIFI_SSID 与 WIFI_PASSWORD')
        else:
            ssid=input('家庭 Wi-Fi 名称：').strip()
            password=getpass.getpass('Wi-Fi 密码（开放网络可留空）：')
        address=lan_address()
        default=f'http://{address}:{config.get("PORT","8765")}' if address else ''
        url=default if args.from_config else input(f'电脑服务地址 [{default}]：').strip() or default
    payload=qr_payload(config,args.kind,ssid,password,url)
    root=Path(__file__).with_name('data')/'qr'
    root.mkdir(mode=0o700,parents=True,exist_ok=True)
    path=root/(args.kind+'.svg')
    code=qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M,box_size=8,border=4)
    code.add_data(payload);code.make(fit=True)
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)
    with os.fdopen(fd,'wb') as stream:
        code.make_image(image_factory=qrcode.image.svg.SvgPathFillImage).save(stream)
    os.chmod(path,0o600)
    print(f'本机二维码已生成：{path.resolve()}')
    print('二维码含私密配置。打开此文件，让团团摄像头扫码，再在设备上确认保存。')

if __name__=='__main__': main()
