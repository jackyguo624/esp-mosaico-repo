// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_gsp.h"
#include "companion_settings.h"
#define COMPANION_MAX_TASKS 16
#define COMPANION_TITLE_BYTES 128
#define COMPANION_NOTE_BYTES 512

typedef enum { COMPANION_IDLE, COMPANION_RECORDING, COMPANION_THINKING,
               COMPANION_SPEAKING, COMPANION_ERROR } companion_stage_t;
typedef enum { COMPANION_TALK, COMPANION_COMPLETE, COMPANION_DELETE,
               COMPANION_SNOOZE, COMPANION_PROVISION, COMPANION_VOLUME_UP,
               COMPANION_VOLUME_DOWN, COMPANION_SAVE_SETTINGS,
               COMPANION_SCAN_START, COMPANION_SCAN_STOP } companion_command_t;
typedef struct {
    int id;
    int64_t due;
    int64_t alerted_due;
    char title[COMPANION_TITLE_BYTES];
    char repeat[12];
} companion_task_t;
typedef struct {
    companion_stage_t stage;
    bool online;
    bool task_pending;
    bool voice_ready;
    char voice_note[128];
    bool setup_pending,scan_active;
    int setup_result;
    unsigned scan_serial;
    char setup_note[128];
    int volume;
    int64_t now;
    int reminder_id;
    int count;
    companion_task_t tasks[COMPANION_MAX_TASKS];
    char note[COMPANION_NOTE_BYTES];
    char settings_note[COMPANION_NOTE_BYTES];
} companion_model_t;
typedef struct companion_ui_t *companion_ui_handle_t;
typedef struct {
    // Called from the GSP UI task. Platform must copy a consistent snapshot.
    void (*read)(void *ctx, companion_model_t *out);
    // Non-blocking: platform queues work; UI never calls audio or HTTP directly.
    void (*command)(void *ctx, companion_command_t command, int task_id);
    bool (*settings_read)(void *ctx,bool scanned,companion_settings_t *out);
    bool (*settings_save)(void *ctx,const companion_settings_t *settings);
    void *ctx;
} companion_ui_config_t;
esp_gsp_err_t companion_ui_create(esp_gsp_handle_t ui,
    const companion_ui_config_t *config, companion_ui_handle_t *out);
void companion_ui_delete(companion_ui_handle_t handle);
