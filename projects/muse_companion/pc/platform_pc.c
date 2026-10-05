#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gsp_sim_bridge.h"
#include "companion_ui.h"
#include "companion_platform.h"
static companion_ui_handle_t app;
static companion_message_t message;
static bool pending, muted, reply_error;
bool companion_platform_setup_request_failed(void){return false;}
bool companion_platform_setup_request(void) {
    fprintf(stderr,"MOCK phone setup requested; hardware would restart into hotspot\n");
    return true;
}
bool companion_platform_setup(const char **ssid,const char **password) {
    const char *mode=getenv("MUSE_COMPANION_SIM_SETUP");
    if(!mode||strcmp(mode,"1"))return false;
    if(ssid)*ssid="Mochi-Setup-1234";
    if(password)*password="";
    return true;
}
static uint32_t clock_ms, reply_at, done_at, generation;
void companion_platform_tick(uint32_t ms) {
    clock_ms += ms;
    if(reply_at && clock_ms >= reply_at) {
        fprintf(stderr,"MOCK reply generation=%u\n",generation);
        reply_at=0; pending=true; message.kind=reply_error?COMP_MSG_ERROR:COMP_MSG_REPLY;
        message.generation=generation; message.speaking=!muted;
        snprintf(message.text,sizeof(message.text),"%s",reply_error?"Muse 对话失败，请检查连接后重试":"你好呀，我一直在这里陪你。按住机器上方的 AI 键，就可以和我说话啦。今天想聊些什么？");
        if(!muted&&!reply_error) done_at=clock_ms+3000;
    } else if(done_at && clock_ms>=done_at && !pending) {
        done_at=0;pending=true;message.kind=COMP_MSG_DONE;message.generation=generation;
    }
}
void companion_platform_status(bool *online,bool *paired,int *battery,bool *key,bool *motion) {
    *online=true;*paired=true;*battery=82;*key=false;*motion=false;
}
bool companion_platform_message(companion_message_t *out) {
    if(!pending) return false;
    *out=message;pending=false;return true;
}
bool companion_platform_effect(unsigned effects,const companion_snapshot_t *s) {
    generation=s->generation;muted=s->muted;
    if(effects&COMP_EFFECT_CANCEL) { pending=false;reply_at=done_at=0; }
    if(effects&COMP_EFFECT_END) reply_at=clock_ms+1500;
    if(muted) done_at=0;
    if(effects&COMP_EFFECT_RETRY) done_at=clock_ms+2000;
    fprintf(stderr,"companion effects=%u generation=%u muted=%d volume=%u brightness=%u\n",effects,generation,muted,s->volume,s->brightness);
    fprintf(stderr,"preferences language=%u sleep=%u rate=%u page=%u\n",s->language,s->sleep_seconds,s->speech_rate,s->page_seconds);
    return true;
}
void companion_platform_brightness(unsigned value) { fprintf(stderr,"display brightness=%u\n",value); }
void companion_platform_load_prefs(companion_snapshot_t *prefs) {
    const char *language=getenv("MUSE_COMPANION_SIM_LANGUAGE");
    if(language&&!strcmp(language,"en"))prefs->language=1;
}
bool companion_platform_confirm_pairing(void) { return false; }
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui) {
    const char *fault=getenv("MUSE_COMPANION_SIM_REPLY_ERROR");
    reply_error=fault&&!strcmp(fault,"1");
    fprintf(stderr,"MOCK Muse/TTS service; shared native UI\n");
    return companion_ui_create(ui,&app);
}
void gsp_bridge_app_deinit(esp_gsp_handle_t ui) { (void)ui;companion_ui_delete(app);app=NULL; }
