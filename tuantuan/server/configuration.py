"""Local credential updates and QR payloads; never log values."""
import json
import os
from pathlib import Path
import tempfile
from urllib.parse import urlsplit

def validate_key(key):
    if not isinstance(key,str) or not 16 <= len(key) <= 255 or any(not 33 <= ord(c) <= 126 or c in "\"'" for c in key):
        raise ValueError('请填写完整的 API Key，不包含空格或换行')
    return key

def save_model_key(path,key):
    validate_key(key)
    path=Path(path)
    lines=path.read_text().splitlines()
    updated=[];found=False
    for line in lines:
        if line.partition('=')[0].strip()=='DEEPSEEK_API_KEY':
            if not found: updated.append('DEEPSEEK_API_KEY='+key)
            found=True
        else: updated.append(line)
    if not found: updated.append('DEEPSEEK_API_KEY='+key)
    fd,temporary=tempfile.mkstemp(prefix='.config-',suffix='.env',dir=path.parent)
    try:
        with os.fdopen(fd,'w') as stream:
            stream.write('\n'.join(updated)+'\n');stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally: Path(temporary).unlink(missing_ok=True)

def qr_payload(config,kind,ssid='',password='',url=''):
    if kind=='api-key':
        data={'v':1,'kind':'tuantuan.api-key','key':validate_key(config.get('DEEPSEEK_API_KEY'))}
    elif kind=='connection':
        token=config.get('DEVICE_TOKEN','')
        if not 1 <= len(ssid.encode()) <= 32: raise ValueError('Wi-Fi 名称须为1–32字节')
        if password and not 8 <= len(password.encode()) <= 63: raise ValueError('Wi-Fi 密码须为8–63字节')
        try: host=urlsplit(url).hostname
        except ValueError: host=None
        if not url.startswith(('http://','https://')) or not host or any(c in url for c in ' @?#\\') or len(url.encode())>=192: raise ValueError('服务地址无效')
        if not 16 <= len(token) <= 127: raise ValueError('连接口令无效')
        data={'v':1,'kind':'tuantuan.connection','ssid':ssid,'password':password,'url':url,'token':token}
    else: raise ValueError('不支持的二维码类型')
    if any(ord(c)<32 or ord(c)==127 for c in ssid+password+url+config.get('DEVICE_TOKEN','')):
        raise ValueError('配置不能包含控制字符')
    return json.dumps(data,ensure_ascii=False,separators=(',',':'))
