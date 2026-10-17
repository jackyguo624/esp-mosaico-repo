// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gsp_sim_bridge.h"
#include "companion_ui.h"
// Hardware adapter is replaced by deterministic device data. Same native UI and scene.
static companion_ui_handle_t s_ui;
static companion_model_t s_model;
static int s_ticks;
static int s_action_ticks, s_action_id;
static companion_command_t s_action;
static bool s_fail_actions;
static companion_settings_t s_settings;
static int s_scan_ticks;

static void apply_action(void)
{
    s_model.task_pending=false;
    if (s_fail_actions) {
        s_model.stage=COMPANION_ERROR;
        snprintf(s_model.note,sizeof(s_model.note),"任务操作失败，请稍后重试");
        return;
    }
    for (int i=0;i<s_model.count;i++) if (s_model.tasks[i].id==s_action_id) {
        if (s_action==COMPANION_SNOOZE) s_model.tasks[i].due=time(NULL)+600;
        else {
            memmove(&s_model.tasks[i],&s_model.tasks[i+1],(s_model.count-i-1)*sizeof(s_model.tasks[0]));
            s_model.count--;
        }
        break;
    }
    s_model.reminder_id=0;s_model.stage=COMPANION_IDLE;
    snprintf(s_model.note,sizeof(s_model.note),"%s",s_action==COMPANION_COMPLETE ? "任务已完成" :
        s_action==COMPANION_DELETE ? "任务已删除" : "已设为10分钟后提醒");
}
static void read_model(void *ctx,companion_model_t *out)
{
    (void)ctx;
    if (s_model.task_pending && ++s_action_ticks>=6) apply_action();
    if (s_model.scan_active && ++s_scan_ticks>15) {
        s_model.scan_active=false;s_model.scan_serial++;
        snprintf(s_model.setup_note,sizeof(s_model.setup_note),"已识别，请确认后保存");
    }
    if (s_model.stage==COMPANION_THINKING && ++s_ticks>15) {
        s_model.stage=COMPANION_SPEAKING;s_ticks=0;
        snprintf(s_model.note,sizeof(s_model.note),"我在这里呀，今天过得怎么样？");
    } else if (s_model.stage==COMPANION_SPEAKING && ++s_ticks>25) {s_model.stage=COMPANION_IDLE;s_ticks=0;}
    *out=s_model;out->now=time(NULL);
}
static void command(void *ctx,companion_command_t command,int id)
{
    (void)ctx;
    fprintf(stderr,"companion_command=%d task=%d\n",command,id);
    if (command==COMPANION_TALK) {
        if (s_model.stage==COMPANION_IDLE || s_model.stage==COMPANION_ERROR) s_model.stage=COMPANION_RECORDING;
        else if (s_model.stage==COMPANION_RECORDING) {s_model.stage=COMPANION_THINKING;s_ticks=0;}
        else if (s_model.stage==COMPANION_SPEAKING) s_model.stage=COMPANION_IDLE;
    } else if (command==COMPANION_COMPLETE || command==COMPANION_DELETE || command==COMPANION_SNOOZE) {
        if (!s_model.task_pending) {
            s_action=command;s_action_id=id;s_action_ticks=0;s_model.task_pending=true;
        }
    } else if(command==COMPANION_VOLUME_UP && s_model.volume<90) s_model.volume+=10;
    else if(command==COMPANION_VOLUME_DOWN && s_model.volume>10) s_model.volume-=10;
    else if(command==COMPANION_PROVISION) snprintf(s_model.settings_note,sizeof(s_model.settings_note),"连接 Wi-Fi：Tuantuan-DEMO\n密码：tt12345678\n浏览器打开 192.168.4.1\n模拟器中演示配网入口");
    else if(command==COMPANION_SCAN_START) {s_model.scan_active=true;s_scan_ticks=0;snprintf(s_model.setup_note,sizeof(s_model.setup_note),"模拟扫码中，等待确认页面…");}
    else if(command==COMPANION_SCAN_STOP) s_model.scan_active=false;
}
static bool settings_read(void *ctx,bool scanned,companion_settings_t *out)
{
    (void)ctx;*out=s_settings;
    if (scanned) {
        out->kind=COMPANION_SETTINGS_API_KEY;
        snprintf(out->api_key,sizeof(out->api_key),"demo-only-example-key-not-a-real-credential");
    }
    return true;
}
static bool settings_save(void *ctx,const companion_settings_t *settings)
{
    (void)ctx;s_settings=*settings;
    s_model.setup_result=s_fail_actions ? -1 : 1;
    snprintf(s_model.setup_note,sizeof(s_model.setup_note),s_fail_actions ? "保存失败，请稍后重试" : "配置已保存（模拟器）");
    fprintf(stderr,"settings_save kind=%d (values omitted)\n",settings->kind);
    return true;
}
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    s_model.online=true;s_model.voice_ready=true;s_model.volume=50;s_model.count=5;
    s_settings.kind=COMPANION_SETTINGS_CONNECTION;
    snprintf(s_settings.ssid,sizeof(s_settings.ssid),"Home");
    snprintf(s_settings.url,sizeof(s_settings.url),"http://192.168.1.10:8765");
    snprintf(s_settings.token,sizeof(s_settings.token),"demo-device-token-for-simulator");
    const char *scenario=getenv("TUANTUAN_SIM_SCENARIO");
    if (scenario && !strcmp(scenario,"offline")) s_model.online=false;
    if (scenario && !strcmp(scenario,"empty")) s_model.count=0;
    s_fail_actions=scenario && !strcmp(scenario,"failure");
    const char *titles[]={"提交周报","喝水和休息一下","买咖啡豆","阅读20分钟","整理桌面"};
    for(int i=0;i<5;i++) {
        s_model.tasks[i].id=i+1;s_model.tasks[i].due=time(NULL)+(i+1)*3600;
        snprintf(s_model.tasks[i].title,sizeof(s_model.tasks[i].title),"%s",titles[i]);
        snprintf(s_model.tasks[i].repeat,sizeof(s_model.tasks[i].repeat),"none");
    }
    snprintf(s_model.settings_note,sizeof(s_model.settings_note),"语音服务：http://192.168.1.10:8765\nWi-Fi：Home\n模拟数据，音量：50%%");
    companion_ui_config_t config={.read=read_model,.command=command,.settings_read=settings_read,.settings_save=settings_save};
    return companion_ui_create(ui,&config,&s_ui);
}
void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    (void)ui;companion_ui_delete(s_ui);s_ui=NULL;
    fprintf(stderr,"companion_backend: stage=%d tasks=%d\n",s_model.stage,s_model.count);
}
