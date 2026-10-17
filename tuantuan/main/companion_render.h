// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_gsp.h"
typedef struct companion_render_t *companion_render_handle_t;
typedef esp_gsp_err_t (*companion_text_setter_t)(esp_gsp_handle_t,const char *);
typedef esp_gsp_err_t (*companion_visible_setter_t)(esp_gsp_handle_t,bool);
esp_gsp_err_t companion_render_create(esp_gsp_handle_t ui,companion_render_handle_t *out);
void companion_render_delete(companion_render_handle_t handle);
esp_gsp_err_t companion_render_text(companion_render_handle_t handle,companion_text_setter_t setter,const char *text);
esp_gsp_err_t companion_render_visible(companion_render_handle_t handle,companion_visible_setter_t setter,bool visible);
