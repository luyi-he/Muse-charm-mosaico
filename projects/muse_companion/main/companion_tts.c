// SPDX-License-Identifier: Apache-2.0
#include "companion_tts.h"
#include "companion_model.h"
#include "companion_credentials.h"
#include "companion_pcm.h"
#include "companion_qwen.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
enum { PCM_BUFFER_BYTES=2048, TTS_TASK_STACK=8192, TTS_IO_TIMEOUT_MS=3000,
       MOSS_START_TIMEOUT_MS=15000,
       TTS_GAIN_DB=6 };
typedef struct {uint32_t generation, epoch;unsigned volume,speech_rate;char text[COMP_REPLY_CAP];} request_t;
/* App-lifetime service. Worker owns HTTP and codec writes; generation/mute
 * are atomic so the render task can suppress old speech without blocking. */
struct companion_tts_t {
    esp_codec_dev_handle_t speaker;
    QueueHandle_t queue;
    TaskHandle_t task;
    atomic_uint generation, epoch, volume;
    atomic_bool muted, busy;
    companion_tts_done_cb done;
    void *ctx;
    request_t request;
    uint8_t pcm[PCM_BUFFER_BYTES+6];
    char auth[COMPANION_TTS_TOKEN_CAP+8];
    bool response_audio;
};
static esp_err_t http_event(esp_http_client_event_t *event)
{
    companion_tts_handle_t t=event->user_data;
    if(event->event_id==HTTP_EVENT_ON_HEADER&&!strcasecmp(event->header_key,"Content-Type")){
        const char *v=event->header_value;
        t->response_audio=!strncasecmp(v,"application/octet-stream",24)||
            !strncasecmp(v,"audio/pcm",9)||!strncasecmp(v,"audio/x-pcm",11);
    }
    return ESP_OK;
}
static bool current(companion_tts_handle_t t,uint32_t gen)
{ return atomic_load(&t->generation)==gen&&!atomic_load(&t->muted)
    && atomic_load(&t->epoch)==t->request.epoch; }
static bool qwen_current(void *ctx)
{companion_tts_handle_t t=ctx;return current(t,t->request.generation);}
static unsigned qwen_volume(void *ctx)
{companion_tts_handle_t t=ctx;return atomic_load(&t->volume);}
static bool speak(companion_tts_handle_t t)
{
    request_t *r=&t->request;
    const char *token=companion_credentials_tts();
    unsigned rate=companion_credentials_tts_sample_rate();
    if(!token||!current(t,r->generation))return false;
    if(!strncmp(companion_credentials_tts_url(),"wss://",6)){
        const companion_qwen_config_t cfg={.url=companion_credentials_tts_url(),.token=token,
            .model=companion_credentials_tts_model(),.voice=companion_credentials_tts_voice(),
            .text=r->text,.speech_rate=r->speech_rate,.speaker=t->speaker,
            .current=qwen_current,.volume=qwen_volume,.ctx=t};
        return companion_qwen_speak(&cfg);
    }
    cJSON *body=cJSON_CreateObject();if(!body)return false;
    const char *model=companion_credentials_tts_model();
    bool moss=strstr(model,"MOSS-TTSD")!=NULL;
    cJSON_AddStringToObject(body,"model",model);
    cJSON_AddStringToObject(body,"voice",companion_credentials_tts_voice());
    if(moss){
        size_t cap=strlen(r->text)+5;char *input=malloc(cap);
        if(!input){cJSON_Delete(body);return false;}
        snprintf(input,cap,"[S1]%s",r->text);
        cJSON *field=cJSON_AddStringToObject(body,"input",input);free(input);
        if(!field){cJSON_Delete(body);return false;}
    }else cJSON_AddStringToObject(body,"input",r->text);
    cJSON_AddStringToObject(body,"response_format","pcm");
    if(rate==16000){
        cJSON_AddNumberToObject(body,"sample_rate",16000);
        cJSON_AddNumberToObject(body,"gain",TTS_GAIN_DB);
        cJSON_AddBoolToObject(body,"stream",true);
    }
    cJSON_AddNumberToObject(body,"speed",r->speech_rate/100.0);
    char *json=cJSON_PrintUnformatted(body);cJSON_Delete(body);
    if(!json)return false;
    t->response_audio=true;
    esp_http_client_config_t cfg={.url=companion_credentials_tts_url(),
        .method=HTTP_METHOD_POST,.crt_bundle_attach=esp_crt_bundle_attach,
        .timeout_ms=moss?MOSS_START_TIMEOUT_MS:TTS_IO_TIMEOUT_MS,.buffer_size=2048,.event_handler=http_event,.user_data=t};
    esp_http_client_handle_t http=esp_http_client_init(&cfg);
    if(!http){free(json);return false;}
    bool ok=false;
    snprintf(t->auth,sizeof(t->auth),"Bearer %s",token);
    esp_http_client_set_header(http,"Authorization",t->auth);
    esp_http_client_set_header(http,"Content-Type","application/json");
    size_t len=strlen(json);
    if(esp_http_client_open(http,len)!=ESP_OK)goto finish;
    size_t sent=0;
    while(sent<len&&current(t,r->generation)){
        int n=esp_http_client_write(http,json+sent,len-sent);
        if(n<=0)goto finish;
        sent+=n;
    }
    if(sent!=len||esp_http_client_fetch_headers(http)<0)goto finish;
    int status=esp_http_client_get_status_code(http);
    if(status!=200){ESP_LOGW("companion_tts","HTTP %d",status);goto finish;}
    if(!t->response_audio){ESP_LOGW("companion_tts","expected raw PCM response");goto finish;}
    if(esp_http_client_set_timeout_ms(http,TTS_IO_TIMEOUT_MS)!=ESP_OK)goto finish;
    if(!current(t,r->generation))goto finish;
    ESP_LOGI("companion_tts","model=%s voice=%s",model,companion_credentials_tts_voice());
    unsigned applied_volume=atomic_load(&t->volume);
    if(esp_codec_dev_set_out_vol(t->speaker,applied_volume)!=ESP_CODEC_DEV_OK)goto finish;
    ESP_LOGI("companion_tts","output volume=%u TTS gain=%d dB speed=%u%%",applied_volume,TTS_GAIN_DB,r->speech_rate);
    unsigned carry=0;size_t played=0;
    while(current(t,r->generation)){
        int n=esp_http_client_read(http,(char *)t->pcm+carry,PCM_BUFFER_BYTES);
        if(n<0)goto finish;
        if(!n){
            /* At most two final 24kHz samples may remain after complete triples. */
            if(rate==24000&&carry&&(carry%2)==0&&esp_http_client_is_complete_data_received(http)){
                t->pcm[carry]=t->pcm[carry-2];t->pcm[carry+1]=t->pcm[carry-1];
                if(carry==2){t->pcm[4]=t->pcm[0];t->pcm[5]=t->pcm[1];}
                size_t consumed;size_t tail=companion_pcm_convert(t->pcm,6,rate,&consumed);
                tail=carry==2?2:tail;
                if(current(t,r->generation)&&esp_codec_dev_write(t->speaker,t->pcm,tail)==ESP_CODEC_DEV_OK){played+=tail;carry=0;}
            }
            ok=esp_http_client_is_complete_data_received(http)&&carry==0&&played>0;break;
        }
        unsigned bytes=(unsigned)n+carry;size_t consumed;
        size_t whole=companion_pcm_convert(t->pcm,bytes,rate,&consumed);
        if(!current(t,r->generation))goto finish;
        unsigned volume=atomic_load(&t->volume);
        if(volume!=applied_volume){
            if(esp_codec_dev_set_out_vol(t->speaker,volume)!=ESP_CODEC_DEV_OK)goto finish;
            applied_volume=volume;
            ESP_LOGI("companion_tts","output volume changed=%u",volume);
        }
        if(whole&&esp_codec_dev_write(t->speaker,t->pcm,whole)!=ESP_CODEC_DEV_OK)goto finish;
        played+=whole;carry=bytes-consumed;
        if(carry)memmove(t->pcm,t->pcm+consumed,carry);
    }
finish:
    esp_http_client_close(http);esp_http_client_cleanup(http);free(json);
    return ok;
}
static void worker(void *ctx)
{
    companion_tts_handle_t t=ctx;
    for(;;){
        if(xQueueReceive(t->queue,&t->request,portMAX_DELAY)!=pdTRUE)continue;
        if(!current(t,t->request.generation))continue;
        atomic_store(&t->busy,true);
        bool ok=speak(t);
        ESP_LOGI("companion_tts","playback finished: generation=%u success=%u current=%u",(unsigned)t->request.generation,(unsigned)ok,(unsigned)current(t,t->request.generation));
        atomic_store(&t->busy,false);
        if(current(t,t->request.generation)&&t->done)t->done(t->ctx,t->request.generation,ok);
    }
}
esp_err_t companion_tts_create(esp_codec_dev_handle_t spk,
    companion_tts_done_cb done,void *ctx,companion_tts_handle_t *out)
{
    if(!out||!spk)return ESP_ERR_INVALID_ARG;
    *out=NULL;
    companion_tts_handle_t t=calloc(1,sizeof(*t));if(!t)return ESP_ERR_NO_MEM;
    t->speaker=spk;t->done=done;t->ctx=ctx;t->queue=xQueueCreate(1,sizeof(request_t));
    atomic_init(&t->volume,55);
    if(!t->queue){free(t);return ESP_ERR_NO_MEM;}
    /* Worker owns HTTP + codec only, with no NVS/flash operations. */
    if(xTaskCreateWithCaps(worker,"companion_tts",TTS_TASK_STACK,t,4,&t->task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){
        vQueueDelete(t->queue);free(t);return ESP_ERR_NO_MEM;}
    *out=t;return ESP_OK;
}
void companion_tts_cancel(companion_tts_handle_t t,uint32_t gen,bool muted)
{if(t){if(atomic_exchange(&t->generation,gen)!=gen||muted)atomic_fetch_add(&t->epoch,1);atomic_store(&t->muted,muted);}}
esp_err_t companion_tts_submit(companion_tts_handle_t t,uint32_t gen,
    const char *text,unsigned volume,unsigned speech_rate)
{
    if(!t||!text)return ESP_ERR_INVALID_ARG;
    request_t *r=calloc(1,sizeof(*r));if(!r)return ESP_ERR_NO_MEM;
    r->generation=gen;r->epoch=atomic_load(&t->epoch);r->volume=volume;r->speech_rate=speech_rate>=75&&speech_rate<=150?speech_rate:100;
    companion_tts_set_volume(t,volume);
    snprintf(r->text,sizeof(r->text),"%s",text);
    bool ok=xQueueOverwrite(t->queue,r)==pdPASS;free(r);
    return ok?ESP_OK:ESP_FAIL;
}
bool companion_tts_idle(companion_tts_handle_t t)
{return !t||!atomic_load(&t->busy);}
void companion_tts_set_volume(companion_tts_handle_t t,unsigned volume)
{if(t)atomic_store(&t->volume,volume>100?100:volume);}
