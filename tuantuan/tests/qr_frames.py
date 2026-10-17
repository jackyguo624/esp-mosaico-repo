#!/usr/bin/env python3
"""Generate dummy QR frames and run the host copy of the firmware decoder."""
import argparse
from pathlib import Path
import subprocess
import sys
import qrcode
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'server'))
from configuration import qr_payload
p=argparse.ArgumentParser();p.add_argument('--decoder',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
c={'DEEPSEEK_API_KEY':'sk-dummy-test-credential-only-123456','DEVICE_TOKEN':'dummy-device-token'}
for kind in ['api-key','connection']:
    qr=qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M,box_size=5,border=4)
    qr.add_data(qr_payload(c,kind,'家中网络','12345678','http://192.168.1.10:8765'));qr.make();matrix=qr.get_matrix()
    for rotation in [0,90,180,270]:
        n=len(matrix);pixels=b''.join(bytes(0 if black else 255 for black in row for _ in range(5))*5 for row in matrix)
        path=a.output/f'dummy-{kind}-{rotation}.pgm';path.write_bytes(f'P5\n{n*5} {n*5}\n255\n'.encode()+pixels)
        subprocess.run([str(a.decoder.resolve()),str(path),'1'],check=True);matrix=list(zip(*matrix[::-1]))
path=a.output/'blank.pgm';path.write_bytes(b'P5\n320 240\n255\n'+bytes([255])*(320*240))
subprocess.run([str(a.decoder.resolve()),str(path),'0'],check=True)
