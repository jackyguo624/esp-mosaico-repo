// SPDX-License-Identifier: Apache-2.0
#include "companion_render.h"
#include <stdlib.h>
#include <string.h>
// UI-thread owned. Retain the last successful writes to avoid repeatedly
// rendering identical values, on both the device and the native RPC bridge.
#define TEXT_SLOTS 48
#define VISIBLE_SLOTS 32
struct companion_render_t {
    esp_gsp_handle_t ui;
    struct {companion_text_setter_t setter;char *text;size_t capacity;bool valid;} texts[TEXT_SLOTS];
    struct {companion_visible_setter_t setter;bool value,valid;} visibility[VISIBLE_SLOTS];
};
esp_gsp_err_t companion_render_create(esp_gsp_handle_t ui,companion_render_handle_t *out)
{
    if (!ui || !out) return ESP_GSP_ERR_INVALID_ARG;
    *out=calloc(1,sizeof(**out));
    if (!*out) return ESP_GSP_ERR_NO_MEM;
    (*out)->ui=ui;return ESP_GSP_OK;
}
void companion_render_delete(companion_render_handle_t h)
{
    if (!h) return;
    for (size_t i=0;i<TEXT_SLOTS;i++) {
        if (h->texts[i].text) memset(h->texts[i].text,0,h->texts[i].capacity);
        free(h->texts[i].text);
    }
    free(h);
}
esp_gsp_err_t companion_render_text(companion_render_handle_t h,companion_text_setter_t setter,const char *text)
{
    if (!h || !setter || !text) return ESP_GSP_ERR_INVALID_ARG;
    for (size_t i=0;i<TEXT_SLOTS;i++) {
        if (h->texts[i].setter && h->texts[i].setter!=setter) continue;
        if (h->texts[i].valid && h->texts[i].text && !strcmp(h->texts[i].text,text)) return ESP_GSP_OK;
        size_t size=strlen(text)+1;
        if (size>h->texts[i].capacity) {
            char *copy=calloc(size,1);
            if (!copy) return ESP_GSP_ERR_NO_MEM;
            if (h->texts[i].text) memset(h->texts[i].text,0,h->texts[i].capacity);
            free(h->texts[i].text);h->texts[i].text=copy;h->texts[i].capacity=size;
        }
        h->texts[i].valid=false;
        esp_gsp_err_t err=setter(h->ui,text);
        if (err!=ESP_GSP_OK) return err;
        memset(h->texts[i].text,0,h->texts[i].capacity);
        memcpy(h->texts[i].text,text,size);h->texts[i].setter=setter;h->texts[i].valid=true;return ESP_GSP_OK;
    }
    return ESP_GSP_ERR_NO_MEM;
}
esp_gsp_err_t companion_render_visible(companion_render_handle_t h,companion_visible_setter_t setter,bool value)
{
    if (!h || !setter) return ESP_GSP_ERR_INVALID_ARG;
    for (size_t i=0;i<VISIBLE_SLOTS;i++) {
        if (h->visibility[i].setter && h->visibility[i].setter!=setter) continue;
        if (h->visibility[i].valid && h->visibility[i].value==value) return ESP_GSP_OK;
        esp_gsp_err_t err=setter(h->ui,value);
        if (err!=ESP_GSP_OK) return err;
        h->visibility[i].setter=setter;h->visibility[i].value=value;h->visibility[i].valid=true;return ESP_GSP_OK;
    }
    return ESP_GSP_ERR_NO_MEM;
}
