// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#define COMPANION_SSID_BYTES 33
#define COMPANION_PASSWORD_BYTES 64
#define COMPANION_URL_BYTES 192
#define COMPANION_TOKEN_BYTES 128
#define COMPANION_KEY_BYTES 256
#define COMPANION_QR_BYTES 1024
typedef enum { COMPANION_SETTINGS_CONNECTION=1, COMPANION_SETTINGS_API_KEY=2 } companion_settings_kind_t;
typedef struct {
    companion_settings_kind_t kind;
    char ssid[COMPANION_SSID_BYTES];
    char password[COMPANION_PASSWORD_BYTES];
    char url[COMPANION_URL_BYTES];
    char token[COMPANION_TOKEN_BYTES];
    char api_key[COMPANION_KEY_BYTES];
} companion_settings_t;
// Pure validation; messages never contain credentials.
bool companion_settings_validate(const companion_settings_t *settings, char *message, size_t capacity);
bool companion_settings_parse_qr(const char *payload, size_t length, companion_settings_t *out);
