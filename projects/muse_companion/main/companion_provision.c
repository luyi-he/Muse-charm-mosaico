// SPDX-License-Identifier: Apache-2.0
#include "companion_provision.h"
#include "companion_credentials.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PROVISION_SERVICE=0x7e11, REQUEST_LIMIT=2048, SAVE_STACK=4096 };
/* RPC queues bounded data; an internal-stack task exclusively writes NVS.
 * Existing readers retain the boot snapshot until the subsequent restart. */
static atomic_bool saving;
static void save(void *ctx)
{
    cJSON *root=ctx;
    const companion_credentials_config_t c={companion_credentials_muse(),
        cJSON_GetStringValue(cJSON_GetObjectItem(root,"tts_token")),
        cJSON_GetStringValue(cJSON_GetObjectItem(root,"tts_url")),
        cJSON_GetStringValue(cJSON_GetObjectItem(root,"tts_model")),
        cJSON_GetStringValue(cJSON_GetObjectItem(root,"tts_voice")),16000};
    /* Allow the queued response to reach the host before restart. */
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_err_t err=companion_credentials_save(&c);
    cJSON_Delete(root);
    if(err==ESP_OK){ESP_LOGI("companion_provision","TTS configuration saved; restarting");esp_restart();}
    ESP_LOGE("companion_provision","TTS configuration save failed: 0x%x",err);
    atomic_store(&saving,false);vTaskDelete(NULL);
}
static esp_err_t rpc(const esp_iris_rpc_request_t *q,uint8_t *out,size_t cap,size_t *size,void *ctx)
{
    (void)ctx;*size=0;
    if(q->method_id==2){
        if(q->payload_size)return ESP_ERR_INVALID_SIZE;
        cJSON *root=cJSON_CreateObject();if(!root)return ESP_ERR_NO_MEM;
        cJSON_AddStringToObject(root,"model",companion_credentials_tts_model());
        cJSON_AddStringToObject(root,"voice",companion_credentials_tts_voice());
        char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!json)return ESP_ERR_NO_MEM;
        size_t n=strlen(json);if(n>cap){free(json);return ESP_ERR_INVALID_SIZE;}
        memcpy(out,json,n);*size=n;free(json);return ESP_OK;
    }
    if(q->method_id!=1)return ESP_ERR_NOT_SUPPORTED;
    if(!companion_credentials_ready()||atomic_load(&saving))return ESP_ERR_INVALID_STATE;
    if(!q->payload_size||q->payload_size>REQUEST_LIMIT)return ESP_ERR_INVALID_SIZE;
    cJSON *root=cJSON_ParseWithLength((const char*)q->payload,q->payload_size);
    companion_credentials_config_t c={companion_credentials_muse(),
        cJSON_GetStringValue(cJSON_GetObjectItem(root,"tts_token")),
        cJSON_GetStringValue(cJSON_GetObjectItem(root,"tts_url")),
        cJSON_GetStringValue(cJSON_GetObjectItem(root,"tts_model")),
        cJSON_GetStringValue(cJSON_GetObjectItem(root,"tts_voice")),16000};
    if(!root||!companion_credentials_valid(&c)){cJSON_Delete(root);return ESP_ERR_INVALID_ARG;}
    bool expected=false;
    if(!atomic_compare_exchange_strong(&saving,&expected,true)){cJSON_Delete(root);return ESP_ERR_INVALID_STATE;}
    if(xTaskCreate(save,"tts_provision",SAVE_STACK,root,4,NULL)!=pdPASS){atomic_store(&saving,false);cJSON_Delete(root);return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
esp_err_t companion_provision_start(void)
{
    esp_err_t err=esp_iris_rpc_register(PROVISION_SERVICE,1,rpc,NULL);
    if(err!=ESP_OK)return err;
    err=esp_iris_rpc_register(PROVISION_SERVICE,2,rpc,NULL);
    if(err!=ESP_OK)esp_iris_rpc_unregister(PROVISION_SERVICE,1);
    return err;
}
