// SPDX-License-Identifier: Apache-2.0
#include "companion_app.h"
#include "companion_net.h"
#include "bsp/esp_mosaico.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "cJSON.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#define SAMPLE_RATE 16000
#define IO_BYTES 640
#define MAX_RECORD_SECONDS 20
#define WAV_BYTES (44 + SAMPLE_RATE * 2 * MAX_RECORD_SECONDS)
#define RESPONSE_BYTES 32768
#define POLL_SECONDS 10
#define MAX_SPEECH_BYTES (SAMPLE_RATE * 2 * 90)
static const char *TAG="tuantuan";
typedef struct { companion_command_t command; int task_id; } queued_command_t;
typedef struct { uint32_t version; int count; companion_task_t tasks[COMPANION_MAX_TASKS]; } cache_t;
struct companion_app_t {
    SemaphoreHandle_t lock;
    QueueHandle_t commands;
    TaskHandle_t worker;
    companion_net_handle_t net;
    esp_codec_dev_handle_t microphone,speaker;
    companion_model_t model;
    cache_t cache,previous_cache;
    atomic_bool stop_audio;
    uint8_t *wav,*io;
    char *response;
    int64_t next_poll;
    int last_alert_id;
    int64_t last_alert_due;
};

static void lock(companion_app_handle_t h) { xSemaphoreTake(h->lock,portMAX_DELAY); }
static void unlock(companion_app_handle_t h) { xSemaphoreGive(h->lock); }
static void text_copy(char *out,size_t size,const char *text)
{
    if (!size) return;
    size_t length=strlen(text);
    if (length>=size) {length=size-1;while (length && ((unsigned char)text[length]&0xc0)==0x80) length--;}
    memcpy(out,text,length);out[length]=0;
}

static void stage_set(companion_app_handle_t h,companion_stage_t stage,const char *note)
{
    lock(h);h->model.stage=stage;
    if (note) text_copy(h->model.note,sizeof(h->model.note),note);
    unlock(h);
}

static void cache_save(companion_app_handle_t h)
{
    if (!memcmp(&h->cache,&h->previous_cache,sizeof(h->cache))) return;
    nvs_handle_t nvs;
    if (nvs_open("tuantuan",NVS_READWRITE,&nvs)!=ESP_OK) return;
    esp_err_t err=nvs_set_blob(nvs,"tasks",&h->cache,sizeof(h->cache));
    if (err==ESP_OK) err=nvs_commit(nvs);
    nvs_close(nvs);
    if (err==ESP_OK) h->previous_cache=h->cache;
    else ESP_LOGW(TAG,"Could not persist task cache: %s",esp_err_to_name(err));
}

static esp_err_t request(companion_app_handle_t h,const char *path,const void *body,int length,const char *turn_id)
{
    esp_http_client_handle_t client=companion_net_client_create(h->net,path,turn_id ? 120000 : !strcmp(path,"/v1/reminder") ? 90000 : 5000);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err=ESP_OK;
    if (body) {
        err=esp_http_client_set_method(client,HTTP_METHOD_POST);
        if (err==ESP_OK) err=esp_http_client_set_header(client,"Content-Type",turn_id ? "audio/wav" : "application/json");
        if (err==ESP_OK && turn_id) err=esp_http_client_set_header(client,"X-Turn-Id",turn_id);
    }
    if (err==ESP_OK) err=esp_http_client_open(client,body ? length : 0);
    if (err==ESP_OK && body) {
        int offset=0;
        while (offset<length) {
            int n=esp_http_client_write(client,(const char*)body+offset,length-offset);
            if (n<=0) {err=ESP_FAIL;break;} offset+=n;
        }
    }
    int total=0,status=0;
    if (err==ESP_OK) {
        int64_t announced=esp_http_client_fetch_headers(client);
        status=esp_http_client_get_status_code(client);
        if (announced>RESPONSE_BYTES-1) err=ESP_ERR_INVALID_SIZE;
        else {
            while (total<RESPONSE_BYTES-1) {
                int n=esp_http_client_read(client,h->response+total,RESPONSE_BYTES-1-total);
                if (n<0) {err=ESP_FAIL;break;}
                if (!n) break;
                total+=n;
            }
            if (!esp_http_client_is_complete_data_received(client)) err=ESP_ERR_INVALID_SIZE;
        }
    }
    h->response[total]='\0';
    if (err==ESP_OK && status!=200) err=ESP_FAIL;
    esp_http_client_close(client);esp_http_client_cleanup(client);
    return err;
}

static const char *string_field(cJSON *obj,const char *key)
{
    cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
static int64_t number_field(cJSON *obj,const char *key)
{
    cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    return cJSON_IsNumber(v) ? (int64_t)v->valuedouble : 0;
}

static void tasks_parse(companion_app_handle_t h,cJSON *root)
{
    cJSON *tasks=cJSON_GetObjectItemCaseSensitive(root,"tasks");
    if (!cJSON_IsArray(tasks)) return;
    memset(&h->cache,0,sizeof(h->cache));h->cache.version=1;
    cJSON *item;
    cJSON_ArrayForEach(item,tasks) {
        if (h->cache.count==COMPANION_MAX_TASKS) break;
        int id=(int)number_field(item,"id");
        const char *title=string_field(item,"title");
        if (id<=0 || !title[0] || strlen(title)>=COMPANION_TITLE_BYTES) continue;
        companion_task_t *t=&h->cache.tasks[h->cache.count++];
        t->id=id;t->due=number_field(item,"due");
        t->alerted_due=number_field(item,"notified_due");
        for (int old=0;old<h->previous_cache.count;old++) {
            companion_task_t *before=&h->previous_cache.tasks[old];
            if (before->id==id && before->due==t->due && before->alerted_due==t->due) t->alerted_due=t->due;
        }
        snprintf(t->title,sizeof(t->title),"%s",title);
        snprintf(t->repeat,sizeof(t->repeat),"%s",string_field(item,"repeat"));
    }
    lock(h);h->model.count=h->cache.count;
    memcpy(h->model.tasks,h->cache.tasks,sizeof(h->cache.tasks));unlock(h);
    cache_save(h);
}

static void response_error(companion_app_handle_t h,const char *fallback)
{
    cJSON *root=cJSON_Parse(h->response);
    const char *error=string_field(root,"error");
    stage_set(h,COMPANION_ERROR,error[0] ? error : fallback);
    cJSON_Delete(root);
}

static esp_err_t speech_play(companion_app_handle_t h,const char *sid)
{
    if (!sid || strlen(sid)!=32 || strspn(sid,"0123456789abcdef")!=32) return ESP_ERR_INVALID_ARG;
    char path[48];snprintf(path,sizeof(path),"/v1/speech/%s",sid);
    esp_http_client_handle_t client=companion_net_client_create(h->net,path,15000);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err=esp_http_client_open(client,0);
    int64_t length=0;
    if (err==ESP_OK) {
        length=esp_http_client_fetch_headers(client);
        if (esp_http_client_get_status_code(client)!=200 || length<=0 || length>MAX_SPEECH_BYTES || length%2) err=ESP_FAIL;
    }
    atomic_store(&h->stop_audio,false);
    int carry=0,total=0;
    if (err==ESP_OK) stage_set(h,COMPANION_SPEAKING,NULL);
    while (err==ESP_OK && total<length && !atomic_load(&h->stop_audio)) {
        int n=esp_http_client_read(client,(char*)h->io+carry,IO_BYTES-carry);
        if (n<=0) {err=ESP_FAIL;break;}total+=n;
        int count=n+carry,even=count&~1;
        if (even && esp_codec_dev_write(h->speaker,h->io,even)!=ESP_OK) err=ESP_FAIL;
        carry=count-even;
        if (carry) h->io[0]=h->io[even];
    }
    esp_http_client_close(client);esp_http_client_cleanup(client);
    stage_set(h,err==ESP_OK ? COMPANION_IDLE : COMPANION_ERROR,err==ESP_OK ? NULL : "语音播放失败，请检查连接");
    return err;
}

static void put16(uint8_t *p,uint16_t n) {p[0]=n;p[1]=n>>8;}
static void put32(uint8_t *p,uint32_t n) {for (int i=0;i<4;i++) p[i]=n>>(8*i);}
static void wav_header(uint8_t *p,int size)
{
    memset(p,0,44);memcpy(p,"RIFF",4);put32(p+4,size+36);memcpy(p+8,"WAVEfmt ",8);
    put32(p+16,16);put16(p+20,1);put16(p+22,1);put32(p+24,SAMPLE_RATE);
    put32(p+28,SAMPLE_RATE*2);put16(p+32,2);put16(p+34,16);memcpy(p+36,"data",4);put32(p+40,size);
}

static void audio_turn(companion_app_handle_t h)
{
    atomic_store(&h->stop_audio,false);
    stage_set(h,COMPANION_RECORDING,"最多20秒，再点一次结束");
    int recorded=0;
    esp_err_t err=ESP_OK;
    while (recorded<WAV_BYTES-44 && !atomic_load(&h->stop_audio)) {
        int n=WAV_BYTES-44-recorded; if (n>IO_BYTES) n=IO_BYTES;
        if (esp_codec_dev_read(h->microphone,h->wav+44+recorded,n)!=ESP_OK) {err=ESP_FAIL;break;}
        recorded+=n;
    }
    if (err!=ESP_OK || recorded<SAMPLE_RATE*2/5) {
        stage_set(h,COMPANION_ERROR,err==ESP_OK ? "这句话太短，请再说一次" : "录音失败，请重试");return;
    }
    wav_header(h->wav,recorded);
    stage_set(h,COMPANION_THINKING,"正在识别你的声音");
    char turn_id[40];
    snprintf(turn_id,sizeof(turn_id),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),
             (unsigned long)esp_random(),(unsigned long)esp_random());
    err=request(h,"/v1/audio",h->wav,recorded+44,turn_id);
    if (err!=ESP_OK) {response_error(h,"语音服务未连接，请检查电脑端");return;}
    cJSON *root=cJSON_Parse(h->response);
    if (!root || !string_field(root,"reply")[0]) {cJSON_Delete(root);stage_set(h,COMPANION_ERROR,"服务响应格式无效");return;}
    tasks_parse(h,root);
    stage_set(h,COMPANION_IDLE,string_field(root,"reply"));
    const char *sid=string_field(root,"speech_id");
    if (sid[0]) speech_play(h,sid);
    else if (string_field(root,"audio_error")[0]) {
        lock(h);snprintf(h->model.settings_note,sizeof(h->model.settings_note),"语音合成提示：%s",string_field(root,"audio_error"));unlock(h);
    }
    cJSON_Delete(root);
    h->next_poll=0;
}

static esp_err_t task_action(companion_app_handle_t h,int id,const char *action)
{
    char body[96];snprintf(body,sizeof(body),"{\"id\":%d,\"action\":\"%s\",\"seconds\":600}",id,action);
    esp_err_t err=request(h,"/v1/task",body,strlen(body),NULL);
    if (err==ESP_OK) {
        cJSON *root=cJSON_Parse(h->response);
        if (!root) return ESP_FAIL;
        tasks_parse(h,root);cJSON_Delete(root);
        if (strcmp(action,"ack")) {lock(h);h->model.reminder_id=0;unlock(h);}
        h->next_poll=0;
    } else response_error(h,"任务操作失败，请连接电脑端服务");
    return err;
}

static void reminder(companion_app_handle_t h,int id,int64_t due,const char *title,bool online)
{
    h->last_alert_id=id;h->last_alert_due=due;
    for (int i=0;i<h->cache.count;i++) if (h->cache.tasks[i].id==id) h->cache.tasks[i].alerted_due=due;
    cache_save(h);
    lock(h);h->model.reminder_id=id;snprintf(h->model.note,sizeof(h->model.note),"时间到了：%s",title);unlock(h);
    if (!online) {
        // A local audible chime works even without the server. Constant waveform avoids floating point DSP.
        for (int i=0;i<IO_BYTES/2;++i) put16(h->io+i*2,(uint16_t)((i%32<16) ? 1500 : -1500));
        for (int i=0;i<10;++i) if (esp_codec_dev_write(h->speaker,h->io,IO_BYTES)!=ESP_OK) break;
        return;
    }
    char body[48];snprintf(body,sizeof(body),"{\"id\":%d}",id);
    if (request(h,"/v1/reminder",body,strlen(body),NULL)==ESP_OK) {
        cJSON *root=cJSON_Parse(h->response);
        const char *sid=string_field(root,"speech_id");
        if (sid[0] && speech_play(h,sid)==ESP_OK) task_action(h,id,"ack");
        cJSON_Delete(root);
    }
}

static void companion_poll(companion_app_handle_t h,bool connected)
{
    bool online=false;
    if (connected && request(h,"/v1/state",NULL,0,NULL)==ESP_OK) {
        cJSON *root=cJSON_Parse(h->response);
        if (root && cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"tasks"))) {
            online=true;tasks_parse(h,root);
            int64_t now=number_field(root,"now");
            if (now>1700000000) {struct timeval tv={.tv_sec=now,.tv_usec=0};settimeofday(&tv,NULL);}
            cJSON *reminders=cJSON_GetObjectItemCaseSensitive(root,"reminders");
            cJSON *first=cJSON_GetArrayItem(reminders,0);
            if (first) {
                int id=number_field(first,"id");int64_t due=number_field(first,"due");
                bool alerted=false;
                for (int i=0;i<h->cache.count;i++) if (h->cache.tasks[i].id==id && h->cache.tasks[i].alerted_due==due) alerted=true;
                if (id && !alerted) reminder(h,id,due,string_field(first,"title"),true);
                else if (id && alerted) task_action(h,id,"ack");
            }
        }
        cJSON_Delete(root);
    }
    lock(h);h->model.online=online;unlock(h);
    if (!online) {
        int64_t now=time(NULL);
        for (int i=0;i<h->cache.count;i++) {
            companion_task_t *t=&h->cache.tasks[i];
            if (now>1700000000 && t->due && t->due<=now && t->alerted_due!=t->due) {
                reminder(h,t->id,t->due,t->title,false);break;
            }
        }
    }
}

static void worker(void *ctx)
{
    companion_app_handle_t h=ctx;
    char *info=calloc(1,COMPANION_NOTE_BYTES);
    if (!info) {stage_set(h,COMPANION_ERROR,"内存不足");vTaskDelete(NULL);return;}
    for (;;) {
        companion_net_tick(h->net);
        bool connected=false;
        companion_net_get_info(h->net,info,COMPANION_NOTE_BYTES,&connected);
        lock(h);h->model.now=time(NULL);snprintf(h->model.settings_note,sizeof(h->model.settings_note),"%s\n音量：%d%%",info,h->model.volume);unlock(h);
        queued_command_t message;
        if (xQueueReceive(h->commands,&message,pdMS_TO_TICKS(100))) {
            if (message.command==COMPANION_TALK) {
                lock(h);bool online=h->model.online;unlock(h);
                if (online) audio_turn(h);
                else stage_set(h,COMPANION_ERROR,"先到设置连接电脑端语音服务");
            } else if (message.command==COMPANION_PROVISION) {
                if (companion_net_provision(h->net)!=ESP_OK) stage_set(h,COMPANION_ERROR,"配网启动失败");
            } else if (message.command==COMPANION_VOLUME_UP || message.command==COMPANION_VOLUME_DOWN) {
                lock(h);int volume=h->model.volume+(message.command==COMPANION_VOLUME_UP ? 10 : -10);
                if (volume<10) volume=10;
                if (volume>90) volume=90;
                h->model.volume=volume;unlock(h);
                if (esp_codec_dev_set_out_vol(h->speaker,volume)!=ESP_OK) stage_set(h,COMPANION_ERROR,"音量设置失败");
            } else task_action(h,message.task_id,message.command==COMPANION_COMPLETE ? "complete" :
                               message.command==COMPANION_DELETE ? "delete" : "snooze");
        }
        int64_t now=esp_timer_get_time();
        if (now>=h->next_poll) {h->next_poll=now+POLL_SECONDS*1000000LL;companion_poll(h,connected);}
    }
}

static void ui_read(void *ctx,companion_model_t *out)
{
    companion_app_handle_t h=ctx;lock(h);*out=h->model;unlock(h);
    out->now=time(NULL);
}

static void ui_command(void *ctx,companion_command_t command,int id)
{
    companion_app_handle_t h=ctx;
    if (command==COMPANION_TALK) {
        lock(h);companion_stage_t stage=h->model.stage;unlock(h);
        if (stage==COMPANION_RECORDING || stage==COMPANION_SPEAKING) {atomic_store(&h->stop_audio,true);return;}
        if (stage==COMPANION_THINKING) return;
        lock(h);bool online=h->model.online;unlock(h);
        if (!online) {stage_set(h,COMPANION_ERROR,"先到设置连接电脑端语音服务");return;}
        stage_set(h,COMPANION_THINKING,"准备录音");
    }
    queued_command_t msg={.command=command,.task_id=id};
    if (xQueueSend(h->commands,&msg,0)!=pdTRUE) stage_set(h,COMPANION_ERROR,"正在忙，请稍后再试");
}

esp_err_t companion_app_create(companion_app_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=NULL;
    companion_app_handle_t h=heap_caps_calloc(1,sizeof(*h),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!h) return ESP_ERR_NO_MEM;
    h->lock=xSemaphoreCreateMutex();h->commands=xQueueCreate(4,sizeof(queued_command_t));
    h->wav=heap_caps_malloc(WAV_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    h->response=heap_caps_calloc(1,RESPONSE_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    h->io=heap_caps_malloc(IO_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if (!h->lock || !h->commands || !h->wav || !h->response || !h->io) {
        if (h->lock) vSemaphoreDelete(h->lock);
        if (h->commands) vQueueDelete(h->commands);
        free(h->wav);free(h->response);free(h->io);free(h);return ESP_ERR_NO_MEM;
    }
    atomic_init(&h->stop_audio,false);h->model.volume=50;
    h->cache.version=1;
    nvs_handle_t nvs;
    if (nvs_open("tuantuan",NVS_READONLY,&nvs)==ESP_OK) {
        size_t size=sizeof(h->cache);
        if (nvs_get_blob(nvs,"tasks",&h->cache,&size)!=ESP_OK || size!=sizeof(h->cache) || h->cache.version!=1 ||
            h->cache.count<0 || h->cache.count>COMPANION_MAX_TASKS) memset(&h->cache,0,sizeof(h->cache));
        nvs_close(nvs);
    }
    h->previous_cache=h->cache;h->model.count=h->cache.count;memcpy(h->model.tasks,h->cache.tasks,sizeof(h->cache.tasks));
    // BSP and net retain singleton resources after successful creation. On subsequent failure
    // keep the handle for diagnostics until reboot; never free registered callback contexts.
    *out=h;
    esp_err_t err=companion_net_create(&h->net);
    if (err!=ESP_OK) {stage_set(h,COMPANION_ERROR,"网络初始化失败");return err;}
    h->microphone=bsp_audio_codec_microphone_init();h->speaker=bsp_audio_codec_speaker_init();
    if (!h->microphone || !h->speaker) return ESP_FAIL;
    esp_codec_dev_sample_info_t samples={.sample_rate=SAMPLE_RATE,.bits_per_sample=16,.channel=1,.channel_mask=0};
    if (esp_codec_dev_open(h->microphone,&samples)!=ESP_OK || esp_codec_dev_open(h->speaker,&samples)!=ESP_OK ||
        esp_codec_dev_set_in_gain(h->microphone,30)!=ESP_OK || esp_codec_dev_set_out_vol(h->speaker,h->model.volume)!=ESP_OK) return ESP_FAIL;
    if (xTaskCreate(worker,"companion",8192,h,5,&h->worker)!=pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void companion_app_ui_config(companion_app_handle_t h,companion_ui_config_t *out)
{ if (h && out) *out=(companion_ui_config_t){.read=ui_read,.command=ui_command,.ctx=h}; }
