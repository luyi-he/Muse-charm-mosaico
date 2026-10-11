// SPDX-License-Identifier: Apache-2.0
#include "companion_platform.h"
#include "companion_runtime.h"
#include "companion_board.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include "nvs.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { RATE=16000, BLOCK=256, MAX_SAMPLES=RATE*20, LINE_CAP=8192 };
typedef struct { unsigned effects,gen,volume,brightness,language,sleep,rate,page; bool muted; } command_t;
static struct {
    QueueHandle_t commands,messages;
    SemaphoreHandle_t tx;
    esp_codec_dev_handle_t mic,spk;
    i2c_master_dev_handle_t power;
    atomic_uint generation,volume;
    atomic_bool muted,key,recording,text_mode;
    atomic_llong heartbeat;
    atomic_int battery;
    int16_t *samples;
} r;

static void send_json(cJSON *json)
{
    if(esp_timer_get_time()-atomic_load(&r.heartbeat)>12000000){cJSON_Delete(json);return;}
    char *body=cJSON_PrintUnformatted(json);
    if(body){
        xSemaphoreTake(r.tx,portMAX_DELAY);
        /* Driver-backed stdio handles partial USB writes; lock the complete
         * line so microphone chunks and acknowledgements cannot interleave. */
        char *line=NULL;
        if(asprintf(&line,"\n@cores3 %s\n",body)>0){
            flockfile(stdout);
            usb_serial_jtag_write_bytes(line,strlen(line),pdMS_TO_TICKS(500));
            funlockfile(stdout);free(line);
        }
        xSemaphoreGive(r.tx); free(body);
    }
    cJSON_Delete(json);
}
static cJSON *event(const char *type,unsigned gen)
{
    cJSON *j=cJSON_CreateObject(); cJSON_AddStringToObject(j,"type",type);
    cJSON_AddNumberToObject(j,"generation",gen); return j;
}
static void publish(companion_message_kind_t kind,unsigned gen,const char *text,bool speaking)
{
    companion_message_t *m=calloc(1,sizeof(*m)); if(!m)return;
    m->kind=kind;m->generation=gen;m->speaking=speaking;
    snprintf(m->text,sizeof(m->text),"%s",text?text:"");
    xQueueSend(r.messages,m,pdMS_TO_TICKS(100));free(m);
}
static void save(command_t *c)
{
    nvs_handle_t h;if(nvs_open("companion",NVS_READWRITE,&h)!=ESP_OK)return;
    nvs_set_u8(h,"volume",c->volume);nvs_set_u8(h,"brightness",c->brightness);
    nvs_set_u8(h,"muted",c->muted);nvs_set_u8(h,"language",c->language);
    nvs_set_u8(h,"sleep_sec",c->sleep);nvs_set_u8(h,"speech_rate",c->rate);
    nvs_set_u8(h,"page_sec",c->page);nvs_commit(h);nvs_close(h);
}
static void battery_read(void)
{
    uint8_t status=0,soc=255,reg=0;
    bool ok=r.power&&i2c_master_transmit_receive(r.power,&reg,1,&status,1,100)==ESP_OK;
    reg=0xa4;
    ok=ok&&i2c_master_transmit_receive(r.power,&reg,1,&soc,1,100)==ESP_OK;
    atomic_store(&r.battery,ok&&(status&8)&&soc<=100?soc:-1);
}
static void export_audio(unsigned gen,size_t count,bool valid)
{
    if(!valid||count<RATE/5){publish(COMP_MSG_ERROR,gen,"录音太短或超过二十秒，请重试",false);return;}
    cJSON *j=event("recording",gen);cJSON_AddNumberToObject(j,"samples",count);
    cJSON_AddNumberToObject(j,"sample_rate",RATE);send_json(j);
    char encoded[1400];
    for(size_t offset=0;offset<count&&gen==atomic_load(&r.generation);){
        size_t n=count-offset;if(n>512)n=512;size_t length=0;
        if(mbedtls_base64_encode((unsigned char*)encoded,sizeof(encoded)-1,&length,
                (unsigned char*)(r.samples+offset),n*2)!=0)break;
        encoded[length]=0;j=event("pcm",gen);cJSON_AddStringToObject(j,"data",encoded);
        send_json(j);offset+=n;
    }
    if(gen==atomic_load(&r.generation))send_json(event("recording_end",gen));
}
static void capture_task(void *unused)
{
    (void)unused;int16_t block[BLOCK];size_t count=0;unsigned gen=0;bool valid=true;
    command_t c;int64_t next_battery=0;
    for(;;){
        while(xQueueReceive(r.commands,&c,0)==pdTRUE){
            atomic_store(&r.volume,c.volume);atomic_store(&r.muted,c.muted);
            if(c.effects&COMP_EFFECT_PREFS)save(&c);
            if(c.effects&COMP_EFFECT_CANCEL){
                atomic_store(&r.recording,false);count=0;
                send_json(event("cancel",c.gen));
            }
            if(c.effects&COMP_EFFECT_BEGIN){gen=c.gen;count=0;valid=true;atomic_store(&r.recording,true);}
            if(c.effects&COMP_EFFECT_END){
                atomic_store(&r.recording,false);
                if(atomic_exchange(&r.text_mode,false))send_json(event("text_ready",gen));
                else export_audio(gen,count,valid);
                count=0;
            }
            if(c.effects&COMP_EFFECT_RETRY)send_json(event("retry",c.gen));
        }
        if(esp_timer_get_time()>next_battery){battery_read();next_battery=esp_timer_get_time()+10000000;}
        int ret=esp_codec_dev_read(r.mic,block,sizeof(block));
        if(atomic_load(&r.recording)){
            if(ret!=ESP_CODEC_DEV_OK||count+BLOCK>MAX_SAMPLES)valid=false;
            else if(valid){memcpy(r.samples+count,block,sizeof(block));count+=BLOCK;}
        }
        if(ret!=ESP_CODEC_DEV_OK)vTaskDelay(pdMS_TO_TICKS(20));
    }
}
static unsigned number(cJSON *j,const char *name)
{cJSON *v=cJSON_GetObjectItemCaseSensitive(j,name);return cJSON_IsNumber(v)&&v->valuedouble>=0?(unsigned)v->valuedouble:0;}
static void receive(cJSON *j)
{
    const char *type=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j,"type"));
    if(!type)return;
    unsigned gen=number(j,"generation");
    if(!strcmp(type,"hello")){
        atomic_store(&r.heartbeat,esp_timer_get_time());cJSON *out=event("hello",atomic_load(&r.generation));
        cJSON_AddNumberToObject(out,"protocol",1);cJSON_AddNumberToObject(out,"battery",atomic_load(&r.battery));
        cJSON_AddBoolToObject(out,"muted",atomic_load(&r.muted));send_json(out);return;
    }
    if(!strcmp(type,"key")){
        bool pressed=cJSON_IsTrue(cJSON_GetObjectItem(j,"pressed"));
        if(pressed)atomic_store(&r.text_mode,cJSON_IsTrue(cJSON_GetObjectItem(j,"text_mode")));
        atomic_store(&r.key,pressed);return;
    }
    if(gen!=atomic_load(&r.generation))return;
    if(!strcmp(type,"reply")||!strcmp(type,"error")){
        const char *text=cJSON_GetStringValue(cJSON_GetObjectItem(j,"text"));
        publish(!strcmp(type,"reply")?COMP_MSG_REPLY:COMP_MSG_ERROR,gen,text,
            cJSON_IsTrue(cJSON_GetObjectItem(j,"speaking")));
    }else if(!strcmp(type,"audio")){
        const char *data=cJSON_GetStringValue(cJSON_GetObjectItem(j,"data"));
        unsigned char pcm[2048];size_t bytes=0;int result=-1;
        if(data&&mbedtls_base64_decode(pcm,sizeof(pcm),&bytes,(const unsigned char*)data,strlen(data))==0&&bytes%2==0){
            esp_codec_dev_set_out_vol(r.spk,atomic_load(&r.muted)?0:atomic_load(&r.volume));
            result=esp_codec_dev_write(r.spk,pcm,bytes);
        }
        cJSON *ack=event("audio_ack",gen);cJSON_AddNumberToObject(ack,"seq",number(j,"seq"));
        cJSON_AddNumberToObject(ack,"result",result);send_json(ack);
    }else if(!strcmp(type,"done")){
        vTaskDelay(pdMS_TO_TICKS(120));publish(COMP_MSG_DONE,gen,"",false);
    }
}
static void usb_task(void *unused)
{
    (void)unused;char *line=malloc(LINE_CAP);size_t used=0;bool discard=false;uint8_t bytes[256];
    if(!line){vTaskDelete(NULL);return;}
    for(;;){
        int n=usb_serial_jtag_read_bytes(bytes,sizeof(bytes),pdMS_TO_TICKS(100));
        for(int i=0;i<n;++i){
            if(bytes[i]=='\n'){
                line[used]=0;
                if(!discard){cJSON *j=cJSON_Parse(line);if(j){receive(j);cJSON_Delete(j);}}
                used=0;discard=false;
            }else if(bytes[i]!='\r'){
                if(used<LINE_CAP-1&&!discard)line[used++]=bytes[i];else discard=true;
            }
        }
    }
}
esp_err_t companion_runtime_start(void)
{
    r.commands=xQueueCreate(12,sizeof(command_t));r.messages=xQueueCreate(6,sizeof(companion_message_t));
    r.tx=xSemaphoreCreateMutex();r.samples=heap_caps_malloc(MAX_SAMPLES*2,MALLOC_CAP_SPIRAM);
    if(!r.commands||!r.messages||!r.tx||!r.samples)return ESP_ERR_NO_MEM;
    atomic_store(&r.battery,-1);atomic_store(&r.volume,55);
    usb_serial_jtag_driver_config_t usb={.rx_buffer_size=8192,.tx_buffer_size=8192};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));usb_serial_jtag_vfs_use_driver();
    r.mic=bsp_audio_codec_microphone_init();r.spk=bsp_audio_codec_speaker_init();
    if(!r.mic||!r.spk)return ESP_FAIL;
    esp_codec_dev_sample_info_t format={.sample_rate=RATE,.bits_per_sample=16,.channel=1};
    if(esp_codec_dev_open(r.mic,&format)||esp_codec_dev_open(r.spk,&format))return ESP_FAIL;
    esp_codec_dev_set_in_gain(r.mic,30);esp_codec_dev_set_out_vol(r.spk,55);
    i2c_master_bus_handle_t bus;
    if(i2c_master_get_bus_handle(BSP_I2C_NUM,&bus)==ESP_OK){
        const i2c_device_config_t power={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x34,.scl_speed_hz=100000};
        i2c_master_bus_add_device(bus,&power,&r.power);
    }
    battery_read();
    if(xTaskCreate(capture_task,"local_capture",8192,NULL,4,NULL)!=pdPASS||
        xTaskCreate(usb_task,"local_usb",12288,NULL,3,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
}
void companion_platform_tick(uint32_t ms){(void)ms;}
void companion_platform_status(bool *online,bool *paired,int *battery,bool *key,bool *motion)
{
    int64_t beat=atomic_load(&r.heartbeat);*online=beat>0&&esp_timer_get_time()-beat<12000000;
    *paired=*online;*battery=atomic_load(&r.battery);*key=atomic_load(&r.key);*motion=false;
}
bool companion_platform_message(companion_message_t *out){return xQueueReceive(r.messages,out,0)==pdTRUE;}
bool companion_platform_effect(unsigned effects,const companion_snapshot_t *s)
{
    command_t c={effects,s->generation,s->volume,s->brightness,s->language,s->sleep_seconds,s->speech_rate,s->page_seconds,s->muted};
    atomic_store(&r.generation,s->generation);
    return xQueueSend(r.commands,&c,0)==pdTRUE;
}
void companion_platform_brightness(unsigned value){bsp_display_brightness_set(value);}
void companion_platform_load_prefs(companion_snapshot_t *s)
{
    nvs_handle_t h;if(nvs_open("companion",NVS_READONLY,&h)!=ESP_OK)return;
    uint8_t v;
#define LOAD(key,field) if(nvs_get_u8(h,key,&v)==ESP_OK)s->field=v
    LOAD("volume",volume);LOAD("brightness",brightness);LOAD("muted",muted);LOAD("language",language);
    LOAD("sleep_sec",sleep_seconds);LOAD("speech_rate",speech_rate);LOAD("page_sec",page_seconds);
#undef LOAD
    nvs_close(h);atomic_store(&r.volume,s->volume);atomic_store(&r.muted,s->muted);
}
bool companion_platform_confirm_pairing(void){return false;}
bool companion_platform_setup(const char **ssid,const char **password){(void)ssid;(void)password;return false;}
bool companion_platform_setup_request(void){publish(COMP_MSG_ERROR,atomic_load(&r.generation),"电脑连接 USB 后启动桥接程序",false);return true;}
bool companion_platform_setup_request_failed(void){return false;}
