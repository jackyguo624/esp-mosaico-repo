// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "companion_ui.h"
typedef struct companion_setup_ui_t *companion_setup_ui_handle_t;
esp_gsp_err_t companion_setup_ui_create(esp_gsp_handle_t ui,const companion_ui_config_t *config,companion_setup_ui_handle_t *out);
void companion_setup_ui_delete(companion_setup_ui_handle_t handle);
void companion_setup_ui_open(companion_setup_ui_handle_t handle,bool scan);
void companion_setup_ui_close(companion_setup_ui_handle_t handle);
bool companion_setup_ui_event(companion_setup_ui_handle_t handle,const esp_gsp_event_t *event);
void companion_setup_ui_update(companion_setup_ui_handle_t handle,const companion_model_t *model);
bool companion_setup_ui_active(companion_setup_ui_handle_t handle);
