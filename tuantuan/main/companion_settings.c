// SPDX-License-Identifier: Apache-2.0
#include "companion_settings.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static bool reject(char *message,size_t capacity,const char *reason)
{ if (message && capacity) snprintf(message,capacity,"%s",reason); return false; }
static bool clean(const char *value,size_t capacity)
{
    if (!memchr(value,0,capacity)) return false;
    for (const unsigned char *p=(const unsigned char *)value;*p;p++) if (*p<32 || *p==127) return false;
    return true;
}
static bool valid_url(const char *url)
{
    const char *host=!strncmp(url,"http://",7) ? url+7 : !strncmp(url,"https://",8) ? url+8 : NULL;
    return host && *host && *host!='/' && *host!=':' && !strpbrk(url," @?#\\");
}
bool companion_settings_validate(const companion_settings_t *s,char *message,size_t capacity)
{
    if (!s) return reject(message,capacity,"配置无效");
    if (s->kind==COMPANION_SETTINGS_API_KEY) {
        if (!clean(s->api_key,sizeof(s->api_key)) || strlen(s->api_key)<16)
            return reject(message,capacity,"请填写完整的 API Key");
        for (const unsigned char *p=(const unsigned char*)s->api_key;*p;p++)
            if (*p<33 || *p>126 || *p=='"' || *p=='\'') return reject(message,capacity,"API Key 不能包含空格或换行");
        return true;
    }
    if (s->kind!=COMPANION_SETTINGS_CONNECTION || !clean(s->ssid,sizeof(s->ssid)) || !s->ssid[0])
        return reject(message,capacity,"请填写 Wi-Fi 名称");
    if (!clean(s->password,sizeof(s->password)) || (s->password[0] && strlen(s->password)<8))
        return reject(message,capacity,"Wi-Fi 密码至少8位，开放网络可留空");
    if (!clean(s->url,sizeof(s->url)) ||
        !valid_url(s->url))
        return reject(message,capacity,"请填写完整的电脑服务地址");
    if (!clean(s->token,sizeof(s->token)) || strlen(s->token)<16 || strchr(s->token,' '))
        return reject(message,capacity,"设备连接口令至少16位");
    return true;
}
static bool copy_field(cJSON *root,const char *name,char *out,size_t capacity,bool optional)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,name);
    if (!v && optional) {out[0]=0;return true;}
    if (!cJSON_IsString(v) || strlen(v->valuestring)>=capacity) return false;
    memcpy(out,v->valuestring,strlen(v->valuestring)+1);return true;
}
bool companion_settings_parse_qr(const char *payload,size_t length,companion_settings_t *out)
{
    if (out) memset(out,0,sizeof(*out));
    if (!payload || !out || !length || length>=COMPANION_QR_BYTES || memchr(payload,0,length)) return false;
    memset(out,0,sizeof(*out));
    // Also accept a plain DeepSeek key QR, but never arbitrary text or URLs.
    if (length>=16 && length<sizeof(out->api_key) && !memcmp(payload,"sk-",3)) {
        out->kind=COMPANION_SETTINGS_API_KEY;memcpy(out->api_key,payload,length);
        if (companion_settings_validate(out,NULL,0)) return true;
        memset(out,0,sizeof(*out));return false;
    }
    for (size_t i=0;i+5<length;i++) if (!memcmp(payload+i,"\\u0000",6)) return false;
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithLengthOpts(payload,length,&end,false);
    if (!root) return false;
    while (end<payload+length && (*end==' ' || *end=='\r' || *end=='\n' || *end=='\t')) end++;
    const cJSON *version=cJSON_GetObjectItemCaseSensitive(root,"v");
    const cJSON *kind=cJSON_GetObjectItemCaseSensitive(root,"kind");
    bool ok=end==payload+length && cJSON_IsNumber(version) && version->valuedouble==1 && cJSON_IsString(kind);
    if (ok && !strcmp(kind->valuestring,"tuantuan.connection")) {
        out->kind=COMPANION_SETTINGS_CONNECTION;
        ok=copy_field(root,"ssid",out->ssid,sizeof(out->ssid),false) &&
           copy_field(root,"password",out->password,sizeof(out->password),true) &&
           copy_field(root,"url",out->url,sizeof(out->url),false) &&
           copy_field(root,"token",out->token,sizeof(out->token),false);
    } else if (ok && !strcmp(kind->valuestring,"tuantuan.api-key")) {
        out->kind=COMPANION_SETTINGS_API_KEY;
        ok=copy_field(root,"key",out->api_key,sizeof(out->api_key),false);
    } else ok=false;
    cJSON_Delete(root);
    if (!ok || !companion_settings_validate(out,NULL,0)) {memset(out,0,sizeof(*out));return false;}
    return true;
}
