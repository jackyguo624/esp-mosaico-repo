// SPDX-License-Identifier: Apache-2.0
#include "companion_input.h"
#include "bsp/esp_mosaico.h"
#include "esp_iris.h"
#include "esp_iris_service_profiles.h"
#include "esp_gsp_debug.h"
#include <string.h>
static esp_err_t pointer(const esp_iris_rpc_request_t *request,uint8_t *response,
    size_t capacity,size_t *size,void *ctx)
{
    // Bounded status snapshot on the 4 KiB Iris service task, matching the
    // board's standard pointer adapter. No UI/credential text is logged.
    esp_iris_status_t status={0};
    esp_err_t err=esp_iris_get_status(&status);
    if (err!=ESP_OK) return err;
    if (!status.session_ready || status.transport!=ESP_IRIS_TRANSPORT_KIND_USB) return ESP_ERR_NOT_ALLOWED;
    if (!request || !request->payload || request->payload_size!=ESP_IRIS_POINTER_MESSAGE_SIZE ||
        !response || !size || capacity<ESP_IRIS_POINTER_MESSAGE_SIZE) return ESP_ERR_INVALID_SIZE;
    const uint8_t *p=request->payload;
    int16_t x=(int16_t)(p[2]|((uint16_t)p[3]<<8)),y=(int16_t)(p[4]|((uint16_t)p[5]<<8));
    if (p[0]>2 || p[1] || x<0 || x>=BSP_LCD_H_RES || y<0 || y>=BSP_LCD_V_RES) return ESP_ERR_INVALID_ARG;
    err=esp_gsp_inject_touch((esp_gsp_handle_t)ctx,x,y,p[0]!=2);
    if (err==ESP_OK) {memcpy(response,p,ESP_IRIS_POINTER_MESSAGE_SIZE);*size=ESP_IRIS_POINTER_MESSAGE_SIZE;}
    return err;
}
esp_err_t companion_input_register(esp_gsp_handle_t ui)
{
    if (!ui) return ESP_ERR_INVALID_ARG;
    return esp_iris_rpc_register(ESP_IRIS_POINTER_SERVICE_ID,ESP_IRIS_POINTER_METHOD_ID,pointer,ui);
}
