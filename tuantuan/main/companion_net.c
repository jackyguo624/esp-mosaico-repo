// SPDX-License-Identifier: Apache-2.0
#include "companion_net.h"
#include "companion_provision_peer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define URL_BYTES 192
#define TOKEN_BYTES 128
#define PROVISION_SECONDS 600
struct companion_net_t {
    SemaphoreHandle_t lock;
    esp_netif_t *station;
    httpd_handle_t http;
    bool connected, configured, provisioning;
    int64_t close_at, reboot_at, retry_at;
    char ssid[33], password[65], base_url[URL_BYTES], token[TOKEN_BYTES];
    char device_id[32], ap_name[32], ap_password[16];
};

static void lock(companion_net_handle_t h) { xSemaphoreTake(h->lock,portMAX_DELAY); }
static void unlock(companion_net_handle_t h) { xSemaphoreGive(h->lock); }
static void wifi_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)data;
    companion_net_handle_t h=ctx;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        lock(h); bool configured=h->configured; unlock(h);
        if (configured) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        lock(h); h->connected=false; h->retry_at=esp_timer_get_time()+5000000; unlock(h);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        lock(h); h->connected=true; h->retry_at=0; unlock(h);
        esp_netif_sntp_start();
    }
}

static esp_err_t provision_check(httpd_req_t *req, companion_net_handle_t h)
{
    struct sockaddr_storage peer = {0};
    socklen_t length=sizeof(peer);
    lock(h); bool enabled=h->provisioning; unlock(h);
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    if (!enabled) {
        httpd_resp_set_status(req,"403 Forbidden");
        httpd_resp_send(req,"配网窗口已关闭，请在团团设置页点击「重新配网 / 设置服务器」，再刷新此页。",HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    // Provisioning is reachable only by a client on the device's AP subnet.
    if (getpeername(httpd_req_to_sockfd(req),(struct sockaddr*)&peer,&length) ||
        !companion_provision_peer_allowed((struct sockaddr*)&peer,length)) {
        httpd_resp_set_status(req,"403 Forbidden");
        httpd_resp_send(req,"请先连接团团屏幕显示的 Tuantuan 热点，然后打开 http://192.168.4.1。",HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t page(httpd_req_t *req)
{
    companion_net_handle_t h=req->user_ctx;
    if (provision_check(req,h)!=ESP_OK) return ESP_OK;
    static const char html[] =
        "<!doctype html><html lang='zh'><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
        "<title>团团连接设置</title><style>body{font:18px sans-serif;background:#f8f3ea;color:#443850;max-width:460px;margin:40px auto;padding:20px}"
        "input,button{box-sizing:border-box;display:block;width:100%;padding:14px;margin:8px 0 22px;border-radius:12px;border:1px solid #ccc}button{background:#76688a;color:white}</style>"
        "<h1>让团团连上你</h1><p>填写家中 Wi-Fi 和电脑端语音服务地址。</p>"
        "<form id='f'>Wi-Fi 名称<input id='ssid' maxlength='32' required>Wi-Fi 密码<input id='password' type='password' maxlength='63'>"
        "语音服务地址<input id='url' placeholder='http://192.168.1.10:8765' required>连接口令<input id='token' type='password' minlength='16' maxlength='127' required>"
        "<button>保存并重启团团</button></form><p id='msg'></p><script>f.onsubmit=async e=>{e.preventDefault();msg.textContent='正在保存…';"
        "try{let r=await fetch('/configure',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid.value,password:password.value,url:url.value,token:token.value})});"
        "msg.textContent=await r.text();}catch(e){msg.textContent='连接中断，请检查团团屏幕';}}</script></html>";
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,html,HTTPD_RESP_USE_STRLEN);
}

static const char *field(cJSON *obj,const char *key,size_t min,size_t max)
{
    cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if (!cJSON_IsString(v) || !v->valuestring) return NULL;
    size_t n=strlen(v->valuestring);
    if (n<min || n>max || strpbrk(v->valuestring,"\r\n")) return NULL;
    return v->valuestring;
}

static esp_err_t configure(httpd_req_t *req)
{
    companion_net_handle_t h=req->user_ctx;
    if (provision_check(req,h)!=ESP_OK) return ESP_OK;
    if (req->content_len<=0 || req->content_len>1024) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Request too large");
    char *body=calloc(1,(size_t)req->content_len+1);
    if (!body) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    int got=0;
    while (got<req->content_len) {
        int n=httpd_req_recv(req,body+got,req->content_len-got);
        if (n<=0) { free(body); return ESP_FAIL; }
        got+=n;
    }
    cJSON *json=cJSON_Parse(body); free(body);
    const char *ssid=field(json,"ssid",1,32), *password=field(json,"password",0,63),
               *url=field(json,"url",8,URL_BYTES-1), *token=field(json,"token",16,TOKEN_BYTES-1);
    if (!ssid || !password || !url || !token || (password[0] && strlen(password)<8) ||
        (strncmp(url,"http://",7) && strncmp(url,"https://",8)) || strchr(url,'@') || strchr(url,'?') || strchr(url,'#') || strchr(url,' ')) {
        cJSON_Delete(json); return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"请检查名称、密码、服务地址与连接口令");
    }
    companion_settings_t *settings=calloc(1,sizeof(*settings));
    if (!settings) {cJSON_Delete(json);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");}
    settings->kind=COMPANION_SETTINGS_CONNECTION;
    snprintf(settings->ssid,sizeof(settings->ssid),"%s",ssid);
    snprintf(settings->password,sizeof(settings->password),"%s",password);
    snprintf(settings->url,sizeof(settings->url),"%s",url);
    snprintf(settings->token,sizeof(settings->token),"%s",token);
    cJSON_Delete(json);
    esp_err_t err=companion_net_save_settings(h,settings);
    memset(settings,0,sizeof(*settings));free(settings);
    if (err!=ESP_OK) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"保存失败，请重试");
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    return httpd_resp_send(req,"配置已保存，团团正在重启。请将电脑切回家中 Wi-Fi。",HTTPD_RESP_USE_STRLEN);
}

esp_err_t companion_net_provision(companion_net_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    wifi_config_t *ap=calloc(1,sizeof(*ap));
    if (!ap) return ESP_ERR_NO_MEM;
    snprintf((char*)ap->ap.ssid,sizeof(ap->ap.ssid),"%s",h->ap_name);
    snprintf((char*)ap->ap.password,sizeof(ap->ap.password),"%s",h->ap_password);
    ap->ap.ssid_len=strlen(h->ap_name);ap->ap.channel=1;ap->ap.max_connection=2;ap->ap.authmode=WIFI_AUTH_WPA2_PSK;
    esp_err_t err=esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err==ESP_OK) err=esp_wifi_set_config(WIFI_IF_AP,ap);
    free(ap);
    if (err==ESP_OK) {lock(h);h->provisioning=true;h->close_at=esp_timer_get_time()+(int64_t)PROVISION_SECONDS*1000000;unlock(h);}
    return err;
}

static void read_nvs_str(nvs_handle_t nvs,const char *key,char *out,size_t size)
{ if (nvs_get_str(nvs,key,out,&size)!=ESP_OK) out[0]='\0'; }

void companion_net_get_settings(companion_net_handle_t h,companion_settings_t *out)
{
    if (!h || !out) return;
    memset(out,0,sizeof(*out));out->kind=COMPANION_SETTINGS_CONNECTION;
    lock(h);
    snprintf(out->ssid,sizeof(out->ssid),"%s",h->ssid);
    snprintf(out->password,sizeof(out->password),"%.*s",(int)sizeof(out->password)-1,h->password);
    snprintf(out->url,sizeof(out->url),"%s",h->base_url);
    snprintf(out->token,sizeof(out->token),"%s",h->token);
    unlock(h);
}

esp_err_t companion_net_save_settings(companion_net_handle_t h,const companion_settings_t *s)
{
    if (!h || !s || s->kind!=COMPANION_SETTINGS_CONNECTION || !companion_settings_validate(s,NULL,0)) return ESP_ERR_INVALID_ARG;
    lock(h);
    nvs_handle_t nvs;
    esp_err_t err=nvs_open("tuantuan",NVS_READWRITE,&nvs);
    if (err==ESP_OK) {
        err=nvs_set_str(nvs,"ssid",s->ssid);
        if (err==ESP_OK) err=nvs_set_str(nvs,"password",s->password);
        if (err==ESP_OK) err=nvs_set_str(nvs,"url",s->url);
        if (err==ESP_OK) err=nvs_set_str(nvs,"token",s->token);
        if (err==ESP_OK) err=nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err==ESP_OK) h->reboot_at=esp_timer_get_time()+2000000;
    unlock(h);return err;
}

esp_err_t companion_net_create(companion_net_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=NULL;
    companion_net_handle_t h=calloc(1,sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;
    h->lock=xSemaphoreCreateMutex();
    if (!h->lock) {free(h);return ESP_ERR_NO_MEM;}
    nvs_handle_t nvs;
    if (nvs_open("tuantuan",NVS_READONLY,&nvs)==ESP_OK) {
        read_nvs_str(nvs,"ssid",h->ssid,sizeof(h->ssid));
        read_nvs_str(nvs,"password",h->password,sizeof(h->password));
        read_nvs_str(nvs,"url",h->base_url,sizeof(h->base_url));
        read_nvs_str(nvs,"token",h->token,sizeof(h->token)); nvs_close(nvs);
    }
    h->configured=h->ssid[0] && h->base_url[0] && h->token[0];
    uint8_t mac[6];
    esp_err_t err=esp_read_mac(mac,ESP_MAC_WIFI_STA);
    if (err!=ESP_OK) {vSemaphoreDelete(h->lock);free(h);return err;}
    snprintf(h->device_id,sizeof(h->device_id),"mosaico-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    snprintf(h->ap_name,sizeof(h->ap_name),"Tuantuan-%02X%02X",mac[4],mac[5]);
    snprintf(h->ap_password,sizeof(h->ap_password),"tt%08lx",(unsigned long)esp_random());
    // Boot-lifetime services. Failures after registration are fatal to initialization;
    // callbacks retain this object, so it is not freed until reboot.
    *out=h;
    if ((err=esp_netif_init())!=ESP_OK) return err;
    if ((err=esp_event_loop_create_default())!=ESP_OK) return err;
    h->station=esp_netif_create_default_wifi_sta();
    if (!h->station || !esp_netif_create_default_wifi_ap()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    if ((err=esp_wifi_init(&init))!=ESP_OK) return err;
    if ((err=esp_wifi_set_storage(WIFI_STORAGE_RAM))!=ESP_OK) return err;
    if ((err=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,h))!=ESP_OK) return err;
    if ((err=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,h))!=ESP_OK) return err;
    esp_sntp_config_t sntp=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");sntp.start=false;
    if ((err=esp_netif_sntp_init(&sntp))!=ESP_OK) return err;
    wifi_config_t *station=calloc(1,sizeof(*station));
    if (!station) return ESP_ERR_NO_MEM;
    memcpy(station->sta.ssid,h->ssid,strlen(h->ssid));memcpy(station->sta.password,h->password,strlen(h->password));
    err=esp_wifi_set_mode(WIFI_MODE_STA);
    if (err==ESP_OK) err=esp_wifi_set_config(WIFI_IF_STA,station);
    free(station);
    if (err==ESP_OK) err=esp_wifi_start();
    if (err!=ESP_OK) return err;
    httpd_config_t http=HTTPD_DEFAULT_CONFIG();http.stack_size=6144;http.max_uri_handlers=2;
    if ((err=httpd_start(&h->http,&http))!=ESP_OK) return err;
    httpd_uri_t get={.uri="/",.method=HTTP_GET,.handler=page,.user_ctx=h};
    httpd_uri_t post={.uri="/configure",.method=HTTP_POST,.handler=configure,.user_ctx=h};
    if ((err=httpd_register_uri_handler(h->http,&get))!=ESP_OK) return err;
    if ((err=httpd_register_uri_handler(h->http,&post))!=ESP_OK) return err;
    return h->configured ? ESP_OK : companion_net_provision(h);
}

void companion_net_get_info(companion_net_handle_t h,char *text,size_t size,bool *connected)
{
    if (!h || !text || !connected) return;
    lock(h); *connected=h->connected && h->configured;
    if (h->provisioning) snprintf(text,size,"连接 Wi-Fi：%s\n密码：%s\n浏览器打开 192.168.4.1\n配网窗口开放10分钟",h->ap_name,h->ap_password);
    else snprintf(text,size,"语音服务：%s\nWi-Fi：%s\n%s",h->base_url,h->ssid,h->connected ? "已连接" : "连接中，请检查 Wi-Fi");
    unlock(h);
}

esp_http_client_handle_t companion_net_client_create(companion_net_handle_t h,const char *path,int timeout_ms)
{
    if (!h || !path) return NULL;
    // Credentials are immutable after boot; provisioning saves them and schedules a reboot.
    size_t total=strlen(h->base_url)+strlen(path)+1;
    char *url=malloc(total),*auth=malloc(strlen(h->token)+8);
    if (!url || !auth) {free(url);free(auth);return NULL;}
    snprintf(url,total,"%s%s",h->base_url,path);
    snprintf(auth,strlen(h->token)+8,"Bearer %s",h->token);
    esp_http_client_config_t cfg={.url=url,.timeout_ms=timeout_ms,.crt_bundle_attach=esp_crt_bundle_attach,
                                  .disable_auto_redirect=true,.buffer_size=2048};
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    free(url);
    if (client) {
        if (esp_http_client_set_header(client,"Authorization",auth)!=ESP_OK ||
            esp_http_client_set_header(client,"X-Device-Id",h->device_id)!=ESP_OK) {
            esp_http_client_cleanup(client);client=NULL;
        }
    }
    free(auth);return client;
}

void companion_net_tick(companion_net_handle_t h)
{
    if (!h) return;
    const int64_t now=esp_timer_get_time();
    lock(h);
    bool reboot=h->reboot_at && now>=h->reboot_at;
    bool retry=h->configured && h->retry_at && now>=h->retry_at;
    bool close=h->provisioning && now>=h->close_at;
    if (retry) h->retry_at=now+10000000;
    if (close) h->provisioning=false;
    unlock(h);
    if (reboot) esp_restart();
    if (retry) esp_wifi_connect();
    if (close) esp_wifi_set_mode(WIFI_MODE_STA);
}
