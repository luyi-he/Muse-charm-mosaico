// SPDX-License-Identifier: Apache-2.0
#include "companion_qwen.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>

enum { MESSAGE_CAP=65536, DECODE_CAP=49152, PCM_RING_CAP=65536,
       NETWORK_TIMEOUT_MS=3000, START_TIMEOUT_MS=15000, TOTAL_TIMEOUT_MS=120000 };
typedef struct {
    const companion_qwen_config_t *config;
    esp_websocket_client_handle_t ws;
    StreamBufferHandle_t ring;
    StaticStreamBuffer_t ring_control;
    char *message;
    unsigned char *decoded,*storage;
    size_t message_used;
    atomic_bool connected,updated,done,error;
} session_t;
/* Base64 strings contain no JSON escapes. Read only this named string;
 * metadata uses the general parser below. Avoid duplicating large base64
 * strings on the scarce internal heap. */
static char *delta_string(char *json)
{
    char *p=strstr(json,"\"delta\"");if(!p)return NULL;p+=7;
    while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;
    if(*p++!=':')return NULL;
    while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;
    if(*p++!='"')return NULL;
    char *end=strchr(p,'"');if(!end)return NULL;*end=0;return p;
}
static void message(session_t *s)
{
    if(strstr(s->message,"\"response.audio.delta\"")){
        char *delta=delta_string(s->message);size_t bytes=0;
        if(!delta||mbedtls_base64_decode(s->decoded,DECODE_CAP,&bytes,
            (unsigned char*)delta,strlen(delta))||bytes%2){atomic_store(&s->error,true);return;}
        size_t sent=0;
        while(sent<bytes&&s->config->current(s->config->ctx)&&!atomic_load(&s->error)){
            sent+=xStreamBufferSend(s->ring,s->decoded+sent,bytes-sent,pdMS_TO_TICKS(100));
        }
        return;
    }
    if(s->message_used>4096){atomic_store(&s->error,true);return;}
    cJSON *root=cJSON_Parse(s->message);const char *type=cJSON_GetStringValue(cJSON_GetObjectItem(root,"type"));
    if(!type)atomic_store(&s->error,true);
    else if(!strcmp(type,"session.updated")){
        cJSON *session=cJSON_GetObjectItem(root,"session");
        const char *format=cJSON_GetStringValue(cJSON_GetObjectItem(session,"response_format"));
        cJSON *rate=cJSON_GetObjectItem(session,"sample_rate");
        if(!format||strcmp(format,"pcm")||!cJSON_IsNumber(rate)||rate->valueint!=16000)atomic_store(&s->error,true);
        else atomic_store(&s->updated,true);
    }else if(!strcmp(type,"error")){
        /* Do not echo server messages: they may contain request credentials. */
        ESP_LOGW("companion_qwen","service returned an error");atomic_store(&s->error,true);
    }else if(!strcmp(type,"response.done")){
        const char *status=cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(root,"response"),"status"));
        if(!status||strcmp(status,"completed"))atomic_store(&s->error,true);
        atomic_store(&s->done,true);
    }
    cJSON_Delete(root);
}
static void event(void *ctx,esp_event_base_t base,int32_t id,void *data)
{
    (void)base;session_t *s=ctx;esp_websocket_event_data_t *e=data;
    if(id==WEBSOCKET_EVENT_CONNECTED){atomic_store(&s->connected,true);return;}
    if(id==WEBSOCKET_EVENT_ERROR||id==WEBSOCKET_EVENT_DISCONNECTED||id==WEBSOCKET_EVENT_CLOSED){
        if(!atomic_load(&s->done))atomic_store(&s->error,true);
        return;
    }
    if(id!=WEBSOCKET_EVENT_DATA||(e->op_code!=1&&e->op_code!=0))return;
    if(e->payload_offset==0&&e->op_code==1)s->message_used=0;
    if(e->data_len<0||(size_t)e->data_len>=MESSAGE_CAP-s->message_used){atomic_store(&s->error,true);return;}
    memcpy(s->message+s->message_used,e->data_ptr,e->data_len);s->message_used+=e->data_len;
    if(e->fin&&e->payload_offset+e->data_len==e->payload_len){s->message[s->message_used]=0;message(s);}
}
static bool send_event(session_t *s,cJSON *root)
{
    if(!root)return false;
    char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(!json)return false;
    size_t n=strlen(json);
    bool ok=esp_websocket_client_send_text(s->ws,json,n,pdMS_TO_TICKS(NETWORK_TIMEOUT_MS))==(int)n;
    free(json);return ok;
}
static cJSON *request(const char *type,unsigned id)
{
    cJSON *r=cJSON_CreateObject();char event_id[32];snprintf(event_id,sizeof(event_id),"mochi-%u",id);
    if(!r||!cJSON_AddStringToObject(r,"type",type)||!cJSON_AddStringToObject(r,"event_id",event_id)){cJSON_Delete(r);return NULL;}
    return r;
}
static bool wait_for(session_t *s,atomic_bool *flag)
{
    int64_t deadline=esp_timer_get_time()+START_TIMEOUT_MS*1000LL;
    while(!atomic_load(flag)&&!atomic_load(&s->error)&&s->config->current(s->config->ctx)&&esp_timer_get_time()<deadline)vTaskDelay(pdMS_TO_TICKS(20));
    return atomic_load(flag)&&!atomic_load(&s->error)&&s->config->current(s->config->ctx);
}
bool companion_qwen_speak(const companion_qwen_config_t *c)
{
    session_t *s=calloc(1,sizeof(*s));if(!s)return false;s->config=c;bool ok=false;
    atomic_init(&s->connected,false);atomic_init(&s->updated,false);atomic_init(&s->done,false);atomic_init(&s->error,false);
    s->message=heap_caps_malloc(MESSAGE_CAP,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s->decoded=heap_caps_malloc(DECODE_CAP,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s->storage=heap_caps_malloc(PCM_RING_CAP+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s->message||!s->decoded||!s->storage)goto finish;
    s->ring=xStreamBufferCreateStatic(PCM_RING_CAP+1,1,s->storage,&s->ring_control);if(!s->ring)goto finish;
    char *uri=malloc(strlen(c->url)+strlen(c->model)+9);if(!uri)goto finish;
    sprintf(uri,"%s%cmodel=%s",c->url,strchr(c->url,'?')?'&':'?',c->model);
    esp_websocket_client_config_t cfg={.uri=uri,.crt_bundle_attach=esp_crt_bundle_attach,
        .disable_auto_reconnect=true,.network_timeout_ms=NETWORK_TIMEOUT_MS,.task_stack=6144,.buffer_size=2048};
    s->ws=esp_websocket_client_init(&cfg);free(uri);if(!s->ws)goto finish;
    char *auth=malloc(strlen(c->token)+8);if(!auth)goto finish;sprintf(auth,"Bearer %s",c->token);
    esp_err_t err=esp_websocket_client_append_header(s->ws,"Authorization",auth);memset(auth,0,strlen(auth));free(auth);
    if(err!=ESP_OK||esp_websocket_register_events(s->ws,WEBSOCKET_EVENT_ANY,event,s)!=ESP_OK||esp_websocket_client_start(s->ws)!=ESP_OK)goto finish;
    if(!wait_for(s,&s->connected))goto finish;
    cJSON *r=request("session.update",1);if(!r)goto finish;
    cJSON *settings=cJSON_AddObjectToObject(r,"session");
    if(!settings||!cJSON_AddStringToObject(settings,"voice",c->voice)||!cJSON_AddStringToObject(settings,"mode","commit")||
        !cJSON_AddStringToObject(settings,"response_format","pcm")||!cJSON_AddStringToObject(settings,"language_type","Auto")||
        !cJSON_AddNumberToObject(settings,"sample_rate",16000)||!cJSON_AddNumberToObject(settings,"speech_rate",c->speech_rate/100.0)||
        !cJSON_AddNumberToObject(settings,"volume",50)){cJSON_Delete(r);goto finish;}
    if(!send_event(s,r)||!wait_for(s,&s->updated))goto finish;
    r=request("input_text_buffer.append",2);if(!r)goto finish;
    if(!cJSON_AddStringToObject(r,"text",c->text)){cJSON_Delete(r);goto finish;}
    if(!send_event(s,r)||!send_event(s,request("input_text_buffer.commit",3)))goto finish;
    ESP_LOGI("companion_qwen","model=%s voice=%s PCM16 16000Hz; no extra gain",c->model,c->voice);
    unsigned volume=c->volume(c->ctx);if(esp_codec_dev_set_out_vol(c->speaker,volume)!=ESP_CODEC_DEV_OK)goto finish;
    /* The callback owns decoded; worker owns this separate playback buffer. */
    unsigned char *pcm=heap_caps_malloc(2048,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!pcm)goto finish;
    size_t played=0;int64_t deadline=esp_timer_get_time()+TOTAL_TIMEOUT_MS*1000LL;
    int64_t first_deadline=esp_timer_get_time()+START_TIMEOUT_MS*1000LL;
    while(c->current(c->ctx)&&!atomic_load(&s->error)&&esp_timer_get_time()<deadline){
        size_t n=xStreamBufferReceive(s->ring,pcm,2048,pdMS_TO_TICKS(50));
        if(n){
            unsigned next=c->volume(c->ctx);
            if(next!=volume){if(esp_codec_dev_set_out_vol(c->speaker,next)!=ESP_CODEC_DEV_OK)break;volume=next;}
            if(n%2||esp_codec_dev_write(c->speaker,pcm,n)!=ESP_CODEC_DEV_OK)break;
            played+=n;
        }else if(atomic_load(&s->done)){ok=played>0;break;}
        else if(!played&&esp_timer_get_time()>first_deadline)break;
    }
    free(pcm);
    if(ok)send_event(s,request("session.finish",4));
finish:
    atomic_store(&s->error,true);
    if(s->ws){esp_websocket_client_stop(s->ws);esp_websocket_client_destroy(s->ws);}
    if(s->ring)vStreamBufferDelete(s->ring);
    free(s->message);free(s->decoded);free(s->storage);free(s);return ok;
}
