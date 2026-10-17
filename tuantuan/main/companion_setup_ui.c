// SPDX-License-Identifier: Apache-2.0
#include "companion_setup_ui.h"
#include "bundle_gsp.h"
#include "companion_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct companion_setup_ui_t {
    esp_gsp_handle_t ui;
    companion_render_handle_t render;
    companion_ui_config_t config;
    companion_settings_t values;
    char edit[COMPANION_KEY_BYTES],text[COMPANION_KEY_BYTES+48],feedback[128];
    int view,field;
    unsigned scan_serial;
    bool reveal,waiting,online;
    esp_gsp_err_t error;
};
static void check(companion_setup_ui_handle_t h,esp_gsp_err_t err)
{ if (err!=ESP_GSP_OK) h->error=err; }
static char *field_value(companion_setup_ui_handle_t h,size_t *capacity)
{
    switch(h->field) {
    case 0:*capacity=sizeof(h->values.ssid);return h->values.ssid;
    case 1:*capacity=sizeof(h->values.password);return h->values.password;
    case 2:*capacity=sizeof(h->values.url);return h->values.url;
    case 3:*capacity=sizeof(h->values.token);return h->values.token;
    default:*capacity=sizeof(h->values.api_key);return h->values.api_key;
    }
}
static void view_set(companion_setup_ui_handle_t h,int view)
{
    h->view=view;
    if (view!=2) check(h,companion_render_text(h->render,gsp_companion_setup_edit_value_set_text,""));
    check(h,companion_render_visible(h->render,gsp_companion_setup_page_set_visible,view==1));
    check(h,companion_render_visible(h->render,gsp_companion_setup_edit_page_set_visible,view==2));
    check(h,companion_render_visible(h->render,gsp_companion_setup_scan_page_set_visible,view==3));
}
static void render(companion_setup_ui_handle_t h)
{
    bool key=h->values.kind==COMPANION_SETTINGS_API_KEY;
    check(h,companion_render_visible(h->render,gsp_companion_setup_connection_fields_set_visible,!key));
    check(h,companion_render_visible(h->render,gsp_companion_setup_key_fields_set_visible,key));
    check(h,companion_render_text(h->render,gsp_companion_setup_title_set_text,key ? "模型 Key" : "连接设置"));
    check(h,companion_render_text(h->render,gsp_companion_setup_mode_text_set_text,key ? "Wi-Fi" : "模型Key"));
    check(h,companion_render_text(h->render,gsp_companion_setup_save_text_set_text,h->waiting ? "正在保存…" : key ? "保存到电脑" : "保存并重启"));
    snprintf(h->text,sizeof(h->text),"Wi-Fi：%s",h->values.ssid[0] ? h->values.ssid : "点击填写名称");
    check(h,companion_render_text(h->render,gsp_companion_edit_ssid_text_set_text,h->text));
    check(h,companion_render_text(h->render,gsp_companion_edit_password_text_set_text,h->values.password[0] ? "Wi-Fi 密码：********" : "Wi-Fi 密码：点击填写（开放网络可空）"));
    snprintf(h->text,sizeof(h->text),"服务：%s",h->values.url[0] ? h->values.url : "http://电脑IP:8765");
    check(h,companion_render_text(h->render,gsp_companion_edit_url_text_set_text,h->text));
    check(h,companion_render_text(h->render,gsp_companion_edit_token_text_set_text,h->values.token[0] ? "设备口令：********" : "设备口令：点击填写"));
    check(h,companion_render_text(h->render,gsp_companion_edit_key_text_set_text,h->values.api_key[0] ? "API Key：********" : "点击输入或扫码导入 API Key"));
    check(h,companion_render_text(h->render,gsp_companion_setup_feedback_set_text,h->feedback[0] ? h->feedback : "点击字段输入；长口令推荐使用扫码导入"));
    if (h->view==2) {
        size_t length=strlen(h->edit);
        if ((h->field==1 || h->field==3 || h->field==4) && !h->reveal) {
            size_t n=length>24 ? 24 : length;
            memset(h->text,'*',n);h->text[n]=0;
        } else snprintf(h->text,sizeof(h->text),"%s",h->edit+(length>40 ? length-40 : 0));
        check(h,companion_render_text(h->render,gsp_companion_setup_edit_value_set_text,h->text));
        check(h,companion_render_text(h->render,gsp_companion_setup_reveal_text_set_text,h->reveal ? "隐藏" : "显示"));
    }
}
esp_gsp_err_t companion_setup_ui_create(esp_gsp_handle_t ui,const companion_ui_config_t *config,companion_setup_ui_handle_t *out)
{
    if (!ui || !config || !out) return ESP_GSP_ERR_INVALID_ARG;
    *out=calloc(1,sizeof(**out));
    if (!*out) return ESP_GSP_ERR_NO_MEM;
    (*out)->ui=ui;(*out)->config=*config;
    esp_gsp_err_t err=companion_render_create(ui,&(*out)->render);
    if (err!=ESP_GSP_OK) {free(*out);*out=NULL;}
    return err;
}
void companion_setup_ui_close(companion_setup_ui_handle_t h)
{
    if (!h) return;
    if (h->view==3) h->config.command(h->config.ctx,COMPANION_SCAN_STOP,0);
    memset(&h->values,0,sizeof(h->values));memset(h->edit,0,sizeof(h->edit));
    h->waiting=false;
    check(h,companion_render_text(h->render,gsp_companion_setup_edit_value_set_text,""));
    memset(h->text,0,sizeof(h->text));view_set(h,0);
}
void companion_setup_ui_delete(companion_setup_ui_handle_t h)
{ if (h) {companion_setup_ui_close(h);companion_render_delete(h->render);memset(h,0,sizeof(*h));free(h);} }
bool companion_setup_ui_active(companion_setup_ui_handle_t h) {return h && h->view!=0;}
void companion_setup_ui_open(companion_setup_ui_handle_t h,bool scan)
{
    if (!h) return;
    memset(&h->values,0,sizeof(h->values));h->values.kind=COMPANION_SETTINGS_CONNECTION;
    if (h->config.settings_read) h->config.settings_read(h->config.ctx,false,&h->values);
    h->feedback[0]=0;h->waiting=false;h->reveal=false;
    if (scan) {view_set(h,3);h->config.command(h->config.ctx,COMPANION_SCAN_START,0);}
    else view_set(h,1);
    render(h);
}
static void edit_start(companion_setup_ui_handle_t h,int field)
{
    h->field=field;h->reveal=false;
    size_t capacity;
    snprintf(h->edit,sizeof(h->edit),"%s",field_value(h,&capacity));
    const char *titles[]={"Wi-Fi 名称","Wi-Fi 密码","电脑服务地址","设备连接口令","DeepSeek API Key"};
    check(h,companion_render_text(h->render,gsp_companion_setup_edit_title_set_text,titles[field]));
    check(h,companion_render_text(h->render,gsp_companion_setup_edit_hint_set_text,"输入完成后点击键盘上的 OK"));
    view_set(h,2);render(h);
}
bool companion_setup_ui_event(companion_setup_ui_handle_t h,const esp_gsp_event_t *event)
{
    if (!h || !h->view) return false;
    if (gsp_companion_event_is_setup_back(event) || gsp_companion_event_is_setup_scan_back(event)) {
        companion_setup_ui_close(h);return true;
    }
    if (h->waiting) return true;
    if (gsp_companion_event_is_setup_cancel_edit(event)) {memset(h->edit,0,sizeof(h->edit));view_set(h,1);}
    else if (gsp_companion_event_is_setup_mode(event)) {
        h->values.kind=h->values.kind==COMPANION_SETTINGS_CONNECTION ? COMPANION_SETTINGS_API_KEY : COMPANION_SETTINGS_CONNECTION;
        h->feedback[0]=0;
    } else if (gsp_companion_event_is_edit_ssid(event)) edit_start(h,0);
    else if (gsp_companion_event_is_edit_password(event)) edit_start(h,1);
    else if (gsp_companion_event_is_edit_url(event)) edit_start(h,2);
    else if (gsp_companion_event_is_edit_token(event)) edit_start(h,3);
    else if (gsp_companion_event_is_edit_key(event)) edit_start(h,4);
    else if (gsp_companion_event_is_setup_reveal(event)) h->reveal=!h->reveal;
    else if (gsp_companion_event_is_setup_keyboard_key(event) && h->view==2) {
        size_t capacity;char *destination=field_value(h,&capacity);
        size_t length=strlen(h->edit);
        if (event->arg==13) {snprintf(destination,capacity,"%s",h->edit);memset(h->edit,0,sizeof(h->edit));view_set(h,1);}
        else if (event->arg==8 && length) {
            do {length--;} while (length && ((unsigned char)h->edit[length]&0xc0)==0x80);
            h->edit[length]=0;
        } else if (event->arg>=32 && event->arg<=126 && length+1<capacity) {
            h->edit[length]=(char)event->arg;h->edit[length+1]=0;
        }
    } else if (gsp_companion_event_is_setup_scan_retry(event)) h->config.command(h->config.ctx,COMPANION_SCAN_START,0);
    else if (gsp_companion_event_is_setup_save(event)) {
        if (!h->config.settings_save) snprintf(h->feedback,sizeof(h->feedback),"当前平台未启用配置保存");
        else if (h->values.kind==COMPANION_SETTINGS_API_KEY && !h->online)
            snprintf(h->feedback,sizeof(h->feedback),"请先完成配网，等语音服务在线后再保存 Key");
        else if (h->config.settings_save(h->config.ctx,&h->values)) {h->waiting=true;snprintf(h->feedback,sizeof(h->feedback),"正在保存，请稍候…");}
        else snprintf(h->feedback,sizeof(h->feedback),"请检查各项配置，或稍后重试");
    }
    render(h);return true;
}
void companion_setup_ui_update(companion_setup_ui_handle_t h,const companion_model_t *model)
{
    if (!h || !model) return;
    h->online=model->online;
    if (!h->view) {h->scan_serial=model->scan_serial;return;}
    if (h->waiting && !model->setup_pending) {
        h->waiting=false;
        snprintf(h->feedback,sizeof(h->feedback),"%s",model->setup_note);
        if (model->setup_result>0 && h->values.kind==COMPANION_SETTINGS_API_KEY) memset(h->values.api_key,0,sizeof(h->values.api_key));
    }
    if (h->view==3) {
        check(h,companion_render_text(h->render,gsp_companion_setup_scan_note_set_text,model->setup_note[0] ? model->setup_note : "正在准备摄像头…"));
        if (model->scan_serial!=h->scan_serial && h->config.settings_read && h->config.settings_read(h->config.ctx,true,&h->values)) {
            h->scan_serial=model->scan_serial;
            snprintf(h->feedback,sizeof(h->feedback),"已识别二维码，请检查配置后点击保存");
            view_set(h,1);
        }
    }
    render(h);
}
