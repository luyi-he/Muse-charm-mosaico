#include "companion_credentials.h"
#include "companion_pcm.h"
#include "companion_tts_preset.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char stored[2048],pending[2048];
static size_t stored_size,pending_size;
static bool fail_write,fail_commit;
static unsigned char revision,pending_revision;
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,unsigned char *value)
{(void)h;assert(!strcmp(key,"tts_preset"));*value=revision;return revision?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,unsigned char value)
{(void)h;assert(!strcmp(key,"tts_preset"));if(fail_write)return 8;pending_revision=value;return ESP_OK;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h)
{assert(!strcmp(name,"companion_keys"));*h=1;return !stored_size&&mode==NVS_READONLY?ESP_ERR_NVS_NOT_FOUND:ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size)
{(void)h;assert(!strcmp(key,"tokens"));if(*size<stored_size)return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,stored,stored_size);*size=stored_size;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t size)
{(void)h;assert(!strcmp(key,"tokens")&&size<sizeof(pending));if(fail_write)return 8;memcpy(pending,in,size);pending_size=size;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h)
{(void)h;if(fail_commit)return 9;memcpy(stored,pending,pending_size);stored_size=pending_size;revision=pending_revision;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
static void credentials_test(void)
{
    companion_credentials_config_t c={"dummy-muse","dummy-tts","https://example.test/v1/audio/speech","tts-1","alloy",24000};
    assert(companion_credentials_init()==ESP_OK&&!companion_credentials_ready());
    assert(companion_credentials_valid(&c));assert(companion_credentials_save(&c)==ESP_OK);
    assert(!companion_credentials_ready()); /* No concurrent snapshot mutation. */
    assert(companion_credentials_init()==ESP_OK&&companion_credentials_ready());
    assert(!strcmp(companion_credentials_muse(),c.muse_token));
    assert(!strcmp(companion_credentials_tts(),c.tts_token));
    assert(!strcmp(companion_credentials_tts_url(),c.tts_url));
    assert(!strcmp(companion_credentials_tts_model(),c.tts_model));
    assert(!strcmp(companion_credentials_tts_voice(),c.tts_voice));
    assert(companion_credentials_tts_sample_rate()==24000);
    c.tts_url="wss://dashscope.aliyuncs.com/api-ws/v1/realtime";c.tts_sample_rate=16000;c.tts_model="qwen3-tts-flash-realtime";
    assert(companion_credentials_valid(&c));c.tts_model="qwen&token=bad";assert(!companion_credentials_valid(&c));
    c.tts_model="tts-1";c.tts_sample_rate=24000;assert(!companion_credentials_valid(&c));
    c.tts_url="http://example.test/speech";assert(!companion_credentials_valid(&c));
    c.tts_url="https://example.test/speech";c.tts_token="bad token";assert(!companion_credentials_valid(&c));
    c.tts_token="";assert(companion_credentials_save(&c)==ESP_ERR_INVALID_ARG);
    char huge[COMPANION_TTS_TOKEN_CAP+1];memset(huge,'a',sizeof(huge)-1);huge[sizeof(huge)-1]=0;
    c.tts_token=huge;assert(!companion_credentials_valid(&c));
    c.tts_token="new-token";c.tts_sample_rate=44100;assert(!companion_credentials_valid(&c));
    c.tts_sample_rate=16000;fail_write=true;assert(companion_credentials_save(&c)!=ESP_OK);fail_write=false;
    fail_commit=true;assert(companion_credentials_save(&c)!=ESP_OK);fail_commit=false;
    assert(!strcmp(companion_credentials_tts(),"dummy-tts"));
    assert(companion_credentials_init()==ESP_OK&&!strcmp(companion_credentials_tts(),"dummy-tts"));
    assert(companion_credentials_save(&c)==ESP_OK);assert(!strcmp(companion_credentials_tts(),"dummy-tts"));
    assert(companion_credentials_init()==ESP_OK&&!strcmp(companion_credentials_tts(),"new-token"));
    c.tts_url=COMPANION_SILICONFLOW_URL;c.tts_model="FunAudioLLM/CosyVoice2-0.5B";
    c.tts_voice="FunAudioLLM/CosyVoice2-0.5B:claire";
    assert(companion_credentials_save(&c)==ESP_OK);revision=pending_revision=0; /* Old firmware record. */
    assert(companion_credentials_init()==ESP_OK);
    assert(!strcmp(companion_credentials_tts_model(),COMPANION_SILICONFLOW_MODEL));
    assert(!strcmp(companion_credentials_tts_voice(),COMPANION_SILICONFLOW_VOICE));
    assert(!strcmp(companion_credentials_muse(),"dummy-muse")&&!strcmp(companion_credentials_tts(),"new-token"));
    assert(companion_credentials_save(&c)==ESP_OK);assert(companion_credentials_init()==ESP_OK);
    assert(!strcmp(companion_credentials_tts_model(),"FunAudioLLM/CosyVoice2-0.5B")); /* Later user edit retained. */
    stored[0]=99;assert(companion_credentials_init()==ESP_OK&&!companion_credentials_ready());
    memset(stored,'x',sizeof(stored));stored_size=sizeof(stored);
    assert(companion_credentials_init()==ESP_OK&&!companion_credentials_ready());
}
static size_t stream(const unsigned char *input,size_t bytes,size_t chunk,unsigned char *out)
{
    unsigned char buffer[64];size_t carry=0,at=0,written=0;
    while(at<bytes){size_t n=bytes-at;if(n>chunk)n=chunk;memcpy(buffer+carry,input+at,n);at+=n;
        size_t consumed,converted=companion_pcm_convert(buffer,n+carry,24000,&consumed);
        memcpy(out+written,buffer,converted);written+=converted;carry=n+carry-consumed;
        memmove(buffer,buffer+consumed,carry);}
    assert(carry==0);return written;
}
static void pcm_test(void)
{
    const unsigned char input[]={0xe8,3,0xd0,7,0xb8,11,0,0x80,0xff,0x7f,0xff,0x7f};
    const unsigned char expected[]={0xd0,7,0x88,0x13,0,0x80,0xff,0x7f};
    unsigned char out[32];
    for(size_t chunk=1;chunk<=12;++chunk){assert(stream(input,sizeof(input),chunk,out)==sizeof(expected));assert(!memcmp(out,expected,sizeof(expected)));}
    size_t consumed;memcpy(out,input,sizeof(input));assert(companion_pcm_convert(out,11,16000,&consumed)==10&&consumed==10);
    assert(!memcmp(out,input,11));assert(companion_pcm_convert(out,11,44100,&consumed)==0&&consumed==0);
}
int main(void){credentials_test();pcm_test();puts("PASS: setup validation, persistent reload, failures, immutable snapshot, fragmented PCM conversion");return 0;}
