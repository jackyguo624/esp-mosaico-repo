// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "companion_ui.h"
#include "esp_err.h"
typedef struct companion_app_t *companion_app_handle_t;
// A boot-lifetime singleton owns network/audio/tasks; UI receives snapshots only.
esp_err_t companion_app_create(companion_app_handle_t *out);
void companion_app_ui_config(companion_app_handle_t handle, companion_ui_config_t *out);
