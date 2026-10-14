// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "gsp_sim_bridge.h"
#include "companion_ui.h"
// Hardware adapter is replaced by deterministic device data. Same native UI and scene.
static companion_ui_handle_t s_ui;
static companion_model_t s_model;
static int s_ticks;
static void read_model(void *ctx,companion_model_t *out)
{
    (void)ctx;
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
    } else if (command==COMPANION_COMPLETE || command==COMPANION_DELETE) {
        for(int i=0;i<s_model.count;i++) if(s_model.tasks[i].id==id) {
            memmove(&s_model.tasks[i],&s_model.tasks[i+1],(s_model.count-i-1)*sizeof(s_model.tasks[0]));s_model.count--;break;
        }
    } else if (command==COMPANION_SNOOZE) {
        for(int i=0;i<s_model.count;i++) if(s_model.tasks[i].id==id) s_model.tasks[i].due=time(NULL)+600;
        s_model.reminder_id=0;
    } else if(command==COMPANION_VOLUME_UP && s_model.volume<90) s_model.volume+=10;
    else if(command==COMPANION_VOLUME_DOWN && s_model.volume>10) s_model.volume-=10;
    else if(command==COMPANION_PROVISION) snprintf(s_model.settings_note,sizeof(s_model.settings_note),"连接 Wi-Fi：Tuantuan-DEMO\n密码：tt12345678\n浏览器打开 192.168.4.1\n模拟器中演示配网入口");
}
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    s_model.online=true;s_model.volume=50;s_model.count=5;
    const char *titles[]={"提交周报","喝水和休息一下","买咖啡豆","阅读20分钟","整理桌面"};
    for(int i=0;i<5;i++) {
        s_model.tasks[i].id=i+1;s_model.tasks[i].due=time(NULL)+(i+1)*3600;
        snprintf(s_model.tasks[i].title,sizeof(s_model.tasks[i].title),"%s",titles[i]);
        snprintf(s_model.tasks[i].repeat,sizeof(s_model.tasks[i].repeat),"none");
    }
    snprintf(s_model.settings_note,sizeof(s_model.settings_note),"语音服务：http://192.168.1.10:8765\nWi-Fi：Home\n模拟数据，音量：50%%");
    companion_ui_config_t config={.read=read_model,.command=command};
    return companion_ui_create(ui,&config,&s_ui);
}
void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    (void)ui;companion_ui_delete(s_ui);s_ui=NULL;
    fprintf(stderr,"companion_backend: stage=%d tasks=%d\n",s_model.stage,s_model.count);
}
