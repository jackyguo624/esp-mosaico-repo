// SPDX-License-Identifier: Apache-2.0
#include "companion_qr.h"
#include "mosaico_module_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include "quirc.h"
#include <linux/videodev2.h>
#include <stdlib.h>
#include <string.h>

struct companion_qr_t {
    mosaico_camera_handle_t camera;
    struct quirc *decoder;
    struct quirc_code *code;
    struct quirc_data *data;
    bool opened,streaming;
    unsigned width,height,scale,frames;
    char error[128];
    mosaico_module_mgr_info_t module_info;
};
static const char *TAG="tuantuan_qr";
static esp_err_t failed(companion_qr_handle_t h,const char *stage,esp_err_t err)
{
    if (h) snprintf(h->error,sizeof(h->error),"%s (0x%x)",stage,(unsigned)err);
    ESP_LOGE(TAG,"%s: %s",stage,esp_err_to_name(err));
    return err;
}
const char *companion_qr_get_error(companion_qr_handle_t h)
{ return h && h->error[0] ? h->error : "扫码内存不足，请重启后重试"; }
esp_err_t companion_qr_delete(companion_qr_handle_t h)
{
    if (!h) return ESP_OK;
    esp_err_t err;
    if (h->streaming) {
        err=mosaico_camera_stop_stream(h->camera);
        if (err!=ESP_OK) return err;
        h->streaming=false;
    }
    if (h->opened) {
        err=mosaico_camera_close(h->camera);
        if (err!=ESP_OK) return err;
        h->opened=false;
    }
    if (h->camera) {
        err=mosaico_camera_del(h->camera);
        if (err!=ESP_OK) return err;
    }
    if (h->decoder) quirc_destroy(h->decoder);
    free(h->code);free(h->data);free(h);return ESP_OK;
}
esp_err_t companion_qr_create(companion_qr_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=heap_caps_calloc(1,sizeof(**out),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!*out) return ESP_ERR_NO_MEM;
    companion_qr_handle_t h=*out;
    h->code=heap_caps_malloc(sizeof(*h->code),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    h->data=heap_caps_malloc(sizeof(*h->data),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    h->decoder=quirc_new();
    if (!h->code || !h->data || !h->decoder) return failed(h,"扫码内存不足",ESP_ERR_NO_MEM);
    mosaico_camera_config_t config=MOSAICO_CAMERA_DEFAULT_CONFIG();
    // Both BSP sensor profiles provide UYVY 1280x720. The OV3640 Kconfig
    // default is RGB565 640x480, which cannot be reused as a UYVY mode.
    config.width=1280;config.height=720;
    config.frame_timeout_ms=200;
    esp_err_t err=mosaico_camera_new(&config,&h->camera);
    if (err!=ESP_OK) {
        if (mosaico_module_mgr_get_info(MOSAICO_MODULE_MGR_SLOT_LEFT,&h->module_info)==ESP_OK) {
            const mosaico_module_mgr_info_t *m=&h->module_info;
            ESP_LOGE(TAG,"left module: presence=%d descriptor=%d owner=%d type=%u error=%s",
                m->presence,m->descriptor_state,m->owner_state,m->eeprom.board_type,esp_err_to_name(m->last_error));
            if (m->presence==MOSAICO_MODULE_PRESENCE_ABSENT) return failed(h,"左侧未识别到摄像头",err);
            if (m->descriptor_state==MOSAICO_MODULE_DESCRIPTOR_INVALID) return failed(h,"摄像头模块描述无效",err);
        }
        return failed(h,"摄像头模块识别失败",err);
    }
    if ((err=mosaico_camera_open(h->camera))!=ESP_OK) return failed(h,"摄像头采集模式打开失败",err);
    h->opened=true;
    mosaico_camera_info_t info;
    if ((err=mosaico_camera_get_info(h->camera,&info))!=ESP_OK) return failed(h,"摄像头格式读取失败",err);
    if (info.pixel_format!=V4L2_PIX_FMT_UYVY || !info.width || !info.height) return failed(h,"摄像头格式不支持",ESP_ERR_NOT_SUPPORTED);
    h->scale=(info.width+479)/480;
    if ((info.height+479)/480>h->scale) h->scale=(info.height+479)/480;
    h->width=info.width/h->scale;h->height=info.height/h->scale;
    if (quirc_resize(h->decoder,h->width,h->height)<0) return failed(h,"扫码图像内存不足",ESP_ERR_NO_MEM);
    if ((err=mosaico_camera_start_stream(h->camera))!=ESP_OK) return failed(h,"摄像头采集启动失败",err);
    h->streaming=true;ESP_LOGI(TAG,"QR capture ready: %ux%u -> %ux%u",info.width,info.height,h->width,h->height);return ESP_OK;
}
esp_err_t companion_qr_read(companion_qr_handle_t h,char *out,size_t capacity)
{
    if (!h || !out || !capacity || !h->streaming) return ESP_ERR_INVALID_ARG;
    out[0]=0;
    mosaico_camera_frame_t frame;
    esp_err_t err=mosaico_camera_get_frame(h->camera,&frame);
    if (err!=ESP_OK) return err;
    uint8_t *image=quirc_begin(h->decoder,NULL,NULL);
    size_t stride=frame.bytes_per_line ? frame.bytes_per_line : frame.width*2;
    bool valid=frame.data && frame.width>=h->width*h->scale && frame.height>=h->height*h->scale &&
        frame.pixel_format==V4L2_PIX_FMT_UYVY && stride>=frame.width*2 && frame.size>=stride*frame.height;
    if (valid) {
        const uint8_t *source=frame.data;
        for (unsigned y=0;y<h->height;y++) for (unsigned x=0;x<h->width;x++)
            image[y*h->width+x]=source[y*h->scale*stride+x*h->scale*2+1];
    }
    err=mosaico_camera_return_frame(h->camera,&frame);
    if (err!=ESP_OK) return err;
    if (!valid) return ESP_ERR_INVALID_SIZE;
    quirc_end(h->decoder);
    if (++h->frames==1) ESP_LOGI(TAG,"First QR frame received: %ux%u stride=%u",frame.width,frame.height,(unsigned)stride);
    for (int i=0;i<quirc_count(h->decoder);i++) {
        quirc_extract(h->decoder,i,h->code);
        quirc_decode_error_t result=quirc_decode(h->code,h->data);
        if (result==QUIRC_ERROR_DATA_ECC) {quirc_flip(h->code);result=quirc_decode(h->code,h->data);}
        if (result==QUIRC_SUCCESS && h->data->payload_len>0 && (size_t)h->data->payload_len<capacity &&
            !memchr(h->data->payload,0,h->data->payload_len)) {
            memcpy(out,h->data->payload,h->data->payload_len);out[h->data->payload_len]=0;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
