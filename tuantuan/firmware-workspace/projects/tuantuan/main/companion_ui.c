// SPDX-License-Identifier: Apache-2.0
#include "companion_ui.h"
#include "bundle_gsp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

struct companion_ui_t {
    esp_gsp_handle_t ui;
    companion_ui_config_t config;
    companion_model_t model;
    void *timer;
    unsigned tick, pet_ticks;
    int page, offset, selected_id, shown_reminder;
    char text[COMPANION_NOTE_BYTES];
    esp_gsp_err_t last_error;
};

static void check(companion_ui_handle_t h, esp_gsp_err_t err)
{ if (err != ESP_GSP_OK) h->last_error = err; }

static void page_set(companion_ui_handle_t h, int page)
{
    h->page = page;
    check(h, gsp_companion_home_set_visible(h->ui, page == 0));
    check(h, gsp_companion_task_page_set_visible(h->ui, page == 1));
    check(h, gsp_companion_settings_page_set_visible(h->ui, page == 2));
    check(h, gsp_companion_detail_page_set_visible(h->ui, page == 3));
}

static companion_task_t *selected(companion_ui_handle_t h)
{
    for (int i = 0; i < h->model.count; ++i)
        if (h->model.tasks[i].id == h->selected_id) return &h->model.tasks[i];
    return NULL;
}

static void format_due(companion_ui_handle_t h, companion_task_t *task)
{
    if (!task || !task->due) {
        snprintf(h->text, sizeof(h->text), "待办事项 · 未设定时");
        return;
    }
    // Server provides UNIX time. v1 uses Asia/Shanghai on the device; no host timezone leakage.
    time_t seconds = (time_t)(task->due + 8 * 3600);
    struct tm wall;
    gmtime_r(&seconds, &wall);
    snprintf(h->text, sizeof(h->text), "%02d-%02d %02d:%02d%s",
             wall.tm_mon + 1, wall.tm_mday, wall.tm_hour, wall.tm_min,
             strcmp(task->repeat,"daily") == 0 ? " · 每天" :
             strcmp(task->repeat,"weekdays") == 0 ? " · 工作日" :
             strcmp(task->repeat,"weekly") == 0 ? " · 每周" : "");
}

static void update(companion_ui_handle_t h)
{
    h->config.read(h->config.ctx, &h->model);
    if (h->model.count < 0) h->model.count = 0;
    if (h->model.count > COMPANION_MAX_TASKS) h->model.count = COMPANION_MAX_TASKS;
    time_t seconds = (time_t)(h->model.now + 8 * 3600);
    struct tm wall;
    gmtime_r(&seconds, &wall);
    if (h->model.now > 1700000000) snprintf(h->text,sizeof(h->text),"%02d:%02d",wall.tm_hour,wall.tm_min);
    else snprintf(h->text,sizeof(h->text),"--:--");
    check(h, gsp_companion_clock_set_text(h->ui, h->text));
    check(h, gsp_companion_connection_set_text(h->ui, h->model.online ? "在线" : "离线"));
    const char *status = h->model.stage == COMPANION_RECORDING ? "我在听，再点一次结束" :
                         h->model.stage == COMPANION_THINKING ? "让我想一想" :
                         h->model.stage == COMPANION_SPEAKING ? "想和你说几句话" :
                         h->model.stage == COMPANION_ERROR ? "遇到一点问题" :
                         h->pet_ticks ? "嘿嘿，喜欢你摸摸我" : "我在这里，慢慢说";
    check(h, gsp_companion_status_set_text(h->ui, status));
    check(h, gsp_companion_talk_text_set_text(h->ui, h->model.stage == COMPANION_RECORDING ? "结束录音" :
        h->model.stage == COMPANION_THINKING ? "正在思考…" :
        h->model.stage == COMPANION_SPEAKING ? "停止播放" : "点击开始说话"));
    check(h, gsp_companion_note_set_text(h->ui, h->model.note[0] ? h->model.note : "摸摸我，或者聊一会儿"));
    check(h, gsp_companion_settings_note_set_text(h->ui, h->model.settings_note));
    bool blink = h->pet_ticks || (h->tick % 45 >= 43);
    check(h, gsp_companion_eye_50_set_visible(h->ui, !blink));
    check(h, gsp_companion_eye_138_set_visible(h->ui, !blink));
    check(h, gsp_companion_blink_50_set_visible(h->ui, blink));
    check(h, gsp_companion_blink_138_set_visible(h->ui, blink));
    check(h, gsp_companion_mouth_set_text(h->ui,
          h->model.stage == COMPANION_SPEAKING && h->tick % 4 < 2 ? "○" : "ω"));
    if (h->model.count) {
        companion_task_t *t = &h->model.tasks[0];
        format_due(h, t);
        size_t len = strlen(h->text);
        snprintf(h->text+len, sizeof(h->text)-len, " · %s", t->title);
    } else snprintf(h->text, sizeof(h->text), "目前没有任务");
    check(h, gsp_companion_next_task_text_set_text(h->ui, h->text));
    const int pages = (h->model.count + 3) / 4;
    if (h->offset >= h->model.count) h->offset = 0;
    snprintf(h->text, sizeof(h->text), "%d / %d", h->offset / 4 + 1, pages ? pages : 1);
    check(h, gsp_companion_page_number_set_text(h->ui, h->text));
    esp_gsp_err_t (*const setters[])(esp_gsp_handle_t,const char*) = {
        gsp_companion_task_0_text_set_text, gsp_companion_task_1_text_set_text,
        gsp_companion_task_2_text_set_text, gsp_companion_task_3_text_set_text };
    for (int i=0; i<4; ++i) {
        const int index = h->offset+i;
        check(h, setters[i](h->ui, index < h->model.count ? h->model.tasks[index].title : ""));
    }
    if (h->model.reminder_id && h->shown_reminder != h->model.reminder_id) {
        h->shown_reminder = h->model.reminder_id;
        h->selected_id = h->model.reminder_id;
        page_set(h,3);
    }
    if (!h->model.reminder_id) h->shown_reminder = 0;
    if (h->page == 3) {
        companion_task_t *task = selected(h);
        if (!task) { page_set(h,1); return; }
        check(h, gsp_companion_detail_title_set_text(h->ui, task->title));
        format_due(h, task);
        check(h, gsp_companion_detail_due_set_text(h->ui, h->text));
    }
}

static void timer_tick(esp_gsp_handle_t ui, void *ctx)
{
    (void)ui;
    companion_ui_handle_t h = ctx;
    h->tick++;
    if (h->pet_ticks) h->pet_ticks--;
    update(h);
}

static void command(companion_ui_handle_t h, companion_command_t cmd, int id)
{ h->config.command(h->config.ctx,cmd,id); }

static void event(esp_gsp_handle_t ui, const esp_gsp_event_t *event, void *ctx)
{
    (void)ui;
    companion_ui_handle_t h = ctx;
    if (gsp_companion_event_is_pet(event)) h->pet_ticks = 20;
    else if (gsp_companion_event_is_talk(event)) command(h,COMPANION_TALK,0);
    else if (gsp_companion_event_is_tasks(event)) { h->offset=0; page_set(h,1); }
    else if (gsp_companion_event_is_settings(event)) page_set(h,2);
    else if (gsp_companion_event_is_next_task(event)) {
        if (h->model.count) { h->selected_id=h->model.tasks[0].id; page_set(h,3); }
        else page_set(h,1);
    } else if (gsp_companion_event_is_back_tasks(event) || gsp_companion_event_is_back_settings(event)) page_set(h,0);
    else if (gsp_companion_event_is_back_detail(event)) page_set(h,1);
    else if (gsp_companion_event_is_previous(event)) { if (h->offset>=4) h->offset-=4; }
    else if (gsp_companion_event_is_next(event)) { if (h->offset+4<h->model.count) h->offset+=4; }
    else if (gsp_companion_event_is_provision(event)) command(h,COMPANION_PROVISION,0);
    else if (gsp_companion_event_is_volume_up(event)) command(h,COMPANION_VOLUME_UP,0);
    else if (gsp_companion_event_is_volume_down(event)) command(h,COMPANION_VOLUME_DOWN,0);
    else if (gsp_companion_event_is_complete(event)) { command(h,COMPANION_COMPLETE,h->selected_id); page_set(h,1); }
    else if (gsp_companion_event_is_delete(event)) { command(h,COMPANION_DELETE,h->selected_id); page_set(h,1); }
    else if (gsp_companion_event_is_snooze(event)) { command(h,COMPANION_SNOOZE,h->selected_id); page_set(h,0); }
    else {
        int row = gsp_companion_event_is_task_0(event) ? 0 : gsp_companion_event_is_task_1(event) ? 1 :
                  gsp_companion_event_is_task_2(event) ? 2 : gsp_companion_event_is_task_3(event) ? 3 : -1;
        if (row>=0 && h->offset+row<h->model.count) {
            h->selected_id=h->model.tasks[h->offset+row].id; page_set(h,3);
        }
    }
    update(h);
}

esp_gsp_err_t companion_ui_create(esp_gsp_handle_t ui, const companion_ui_config_t *config, companion_ui_handle_t *out)
{
    if (!ui || !config || !config->read || !config->command || !out) return ESP_GSP_ERR_INVALID_ARG;
    *out = NULL;
    companion_ui_handle_t h = calloc(1,sizeof(*h));
    if (!h) return ESP_GSP_ERR_NO_MEM;
    h->ui=ui; h->config=*config;
    page_set(h,0);
    check(h,esp_gsp_on_event(ui,event,h));
    h->timer=esp_gsp_timer_create(ui,100,timer_tick,h);
    if (!h->timer) check(h,ESP_GSP_ERR_NO_MEM);
    update(h);
    if (h->last_error != ESP_GSP_OK) {
        esp_gsp_err_t error=h->last_error;
        companion_ui_delete(h);
        return error;
    }
    *out=h; return ESP_GSP_OK;
}

void companion_ui_delete(companion_ui_handle_t h)
{
    if (!h) return;
    esp_gsp_on_event(h->ui,NULL,NULL);
    if (h->timer) esp_gsp_timer_delete(h->ui,h->timer);
    free(h);
}
