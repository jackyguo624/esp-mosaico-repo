// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_client.h"
typedef struct companion_net_t *companion_net_handle_t;
// The net singleton lives for the boot lifetime; Wi-Fi/event loop owned by it.
esp_err_t companion_net_create(companion_net_handle_t *out);
esp_err_t companion_net_provision(companion_net_handle_t handle);
void companion_net_get_info(companion_net_handle_t handle, char *text, size_t size, bool *connected);
esp_http_client_handle_t companion_net_client_create(companion_net_handle_t handle, const char *path, int timeout_ms);
void companion_net_tick(companion_net_handle_t handle);
