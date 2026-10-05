// SPDX-License-Identifier: Apache-2.0
#include "companion_credentials.h"
#include "companion_tts_preset.h"
#include "nvs.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
enum { CREDENTIALS_VERSION=1 };
typedef struct {
    uint32_t version, sample_rate;
    char muse[COMPANION_MUSE_TOKEN_CAP], tts[COMPANION_TTS_TOKEN_CAP];
    char url[COMPANION_TTS_URL_CAP], model[COMPANION_TTS_NAME_CAP], voice[COMPANION_TTS_NAME_CAP];
} credentials_t;
static credentials_t credentials;
/* Boot task alone performs a one-time upgrade of the old SiliconFlow
 * default. Subsequent user edits and custom endpoints remain authoritative. */
static esp_err_t migrate_preset(void)
{
    nvs_handle_t h;esp_err_t err=nvs_open("companion_keys",NVS_READWRITE,&h);
    if(err!=ESP_OK)return err;
    uint8_t revision=0;
    err=nvs_get_u8(h,"tts_preset",&revision);
    if(err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND){nvs_close(h);return err;}
    if(revision>=COMPANION_TTS_PRESET_REVISION){nvs_close(h);return ESP_OK;}
    credentials_t *next=malloc(sizeof(*next));if(!next){nvs_close(h);return ESP_ERR_NO_MEM;}
    *next=credentials;
    bool change=!strcmp(next->url,COMPANION_SILICONFLOW_URL)&&
        !strcmp(next->model,"FunAudioLLM/CosyVoice2-0.5B");
    if(change){
        strcpy(next->model,COMPANION_SILICONFLOW_MODEL);strcpy(next->voice,COMPANION_SILICONFLOW_VOICE);
        next->sample_rate=16000;err=nvs_set_blob(h,"tokens",next,sizeof(*next));
    }else err=ESP_OK;
    if(err==ESP_OK)err=nvs_set_u8(h,"tts_preset",COMPANION_TTS_PRESET_REVISION);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);
    if(err==ESP_OK&&change)credentials=*next;
    memset(next,0,sizeof(*next));free(next);return err;
}
static bool token_valid(const char *token,size_t cap)
{
    if(!token)return false;
    size_t n=strnlen(token,cap);
    if(!n||n>=cap)return false;
    for(size_t i=0;i<n;++i)if((unsigned char)token[i]<33||(unsigned char)token[i]>126)return false;
    return true;
}
bool companion_credentials_valid(const companion_credentials_config_t *c)
{
    if(!c||!token_valid(c->muse_token,COMPANION_MUSE_TOKEN_CAP)||
        !token_valid(c->tts_token,COMPANION_TTS_TOKEN_CAP)||
        !token_valid(c->tts_url,COMPANION_TTS_URL_CAP)||
        !token_valid(c->tts_model,COMPANION_TTS_NAME_CAP)||
        !token_valid(c->tts_voice,COMPANION_TTS_NAME_CAP))return false;
    bool websocket=!strncmp(c->tts_url,"wss://",6);
    size_t prefix=websocket?6:8;
    if((!websocket&&strncmp(c->tts_url,"https://",8))||!c->tts_url[prefix]||
        strchr(c->tts_url,'@')||strchr(c->tts_url,'#'))return false;
    if(websocket){
        if(c->tts_sample_rate!=16000)return false;
        for(const char *p=c->tts_model;*p;++p)
            if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='.'||*p=='_'))return false;
    }
    return c->tts_sample_rate==16000||c->tts_sample_rate==24000;
}
bool companion_credentials_ready(void)
{
    const companion_credentials_config_t c={credentials.muse,credentials.tts,credentials.url,
        credentials.model,credentials.voice,credentials.sample_rate};
    return credentials.version==CREDENTIALS_VERSION&&companion_credentials_valid(&c);
}
esp_err_t companion_credentials_init(void)
{
    memset(&credentials,0,sizeof(credentials));nvs_handle_t h;
    esp_err_t err=nvs_open("companion_keys",NVS_READONLY,&h);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    size_t size=sizeof(credentials);err=nvs_get_blob(h,"tokens",&credentials,&size);nvs_close(h);
    if(err==ESP_ERR_NVS_NOT_FOUND||err==ESP_ERR_NVS_INVALID_LENGTH){memset(&credentials,0,sizeof(credentials));return ESP_OK;}
    if(err!=ESP_OK)return err;
    if(size!=sizeof(credentials)||!companion_credentials_ready())memset(&credentials,0,sizeof(credentials));
    return companion_credentials_ready()?migrate_preset():ESP_OK;
}
esp_err_t companion_credentials_save(const companion_credentials_config_t *c)
{
    if(!companion_credentials_valid(c))return ESP_ERR_INVALID_ARG;
    credentials_t *next=calloc(1,sizeof(*next));if(!next)return ESP_ERR_NO_MEM;
    next->version=CREDENTIALS_VERSION;next->sample_rate=c->tts_sample_rate;
    strcpy(next->muse,c->muse_token);strcpy(next->tts,c->tts_token);
    strcpy(next->url,c->tts_url);strcpy(next->model,c->tts_model);strcpy(next->voice,c->tts_voice);
    nvs_handle_t h;esp_err_t err=nvs_open("companion_keys",NVS_READWRITE,&h);
    if(err==ESP_OK){
        err=nvs_set_blob(h,"tokens",next,sizeof(*next));
        if(err==ESP_OK)err=nvs_set_u8(h,"tts_preset",COMPANION_TTS_PRESET_REVISION);
        if(err==ESP_OK)err=nvs_commit(h);
        nvs_close(h);
    }
    /* Live readers keep the old snapshot. New values are loaded after reboot. */
    memset(next,0,sizeof(*next));free(next);return err;
}
const char *companion_credentials_muse(void){return companion_credentials_ready()?credentials.muse:NULL;}
const char *companion_credentials_tts(void){return companion_credentials_ready()?credentials.tts:NULL;}
const char *companion_credentials_tts_url(void){return companion_credentials_ready()?credentials.url:NULL;}
const char *companion_credentials_tts_model(void){return companion_credentials_ready()?credentials.model:NULL;}
const char *companion_credentials_tts_voice(void){return companion_credentials_ready()?credentials.voice:NULL;}
unsigned companion_credentials_tts_sample_rate(void){return credentials.sample_rate;}
