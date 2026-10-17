// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef struct companion_qr_t *companion_qr_handle_t;
// Single worker owns camera and decoder. Delete returns errors before freeing
// a still-owned camera; callers retain the handle and may retry. Create may
// return a partial handle on failure so the caller can release it with delete.
esp_err_t companion_qr_create(companion_qr_handle_t *out);
esp_err_t companion_qr_read(companion_qr_handle_t handle,char *out,size_t capacity);
esp_err_t companion_qr_delete(companion_qr_handle_t handle);

// Error text belongs to the handle, valid until delete; caller may copy it.
const char *companion_qr_get_error(companion_qr_handle_t handle);
