// SPDX-License-Identifier: Apache-2.0
#include "companion_ui.h"
#include "companion_model.h"
#include "companion_platform.h"
#include "character_profile.h"
#include "bundle_gsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef COMPANION_CORES3
#define CHARACTER_X 85
#define CHARACTER_Y 30
#else
#define CHARACTER_X 90
#define CHARACTER_Y 75
#endif
enum { UI_TICK_MS=100, PAGE_BYTES=96, LINE_CHARACTERS=18, PAGE_CHARACTERS=36,
       CHARACTER_POSES=8,
       MAX_PAGE_SECONDS=15 };
struct companion_ui_t {
    esp_gsp_handle_t ui;
    companion_model_handle_t model;
    void *timer;
    companion_snapshot_t snapshot, previous;
    companion_message_t message;
    bool last_key, ready, rendered, current_settings, scene_pending, requested_settings;
    unsigned wave_mask;
    unsigned tick, last_brightness;
    unsigned greeting_ticks, pet_ticks;
    int motion_pose;
    bool auto_pages;
    unsigned reply_pages, page_elapsed_ms;
    char page_hint[96];
    esp_gsp_err_t last_error;
};
static esp_gsp_err_t (*const pose[])(esp_gsp_handle_t,bool)={
    gsp_companion_idle_set_visible,gsp_companion_listening_set_visible,
    gsp_companion_thinking_set_visible,gsp_companion_speaking_set_visible,
    gsp_companion_sleepy_set_visible,gsp_companion_error_set_visible};
static unsigned character_pose(companion_ui_handle_t s,const companion_snapshot_t *m)
{
    if(m->mode==COMP_IDLE&&!m->asleep){
        if(s->pet_ticks)return 6;
        if(s->greeting_ticks)return 7;
    }
    return (unsigned)m->mode;
}
static const unsigned sleep_options[]={5,10,15,20,30,60};
static unsigned sleep_index(unsigned seconds)
{
    for(unsigned i=0;i<sizeof(sleep_options)/sizeof(sleep_options[0]);++i)
        if(sleep_options[i]==seconds)return i;
    return 2;
}
static void checked(companion_ui_handle_t s,esp_gsp_err_t err)
{ if(err!=ESP_GSP_OK){ s->last_error=err;fprintf(stderr,"companion UI error=%d\n",(int)err); } }
/* Split UTF-8 by complete characters, fitting the two visible caption lines.
 * This retains the full reply, and pages never begin inside a UTF-8 codepoint. */
static size_t page_bounds(const char *text,unsigned page,size_t *offset)
{
    size_t start=0,len=strlen(text);
    for(unsigned i=0;;++i) {
        size_t end=start;unsigned characters=0;
        while(end<len && characters<PAGE_CHARACTERS){
            unsigned char c=(unsigned char)text[end];
            size_t n=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;
            if(end+n>len || end+n-start>PAGE_BYTES)break;
            end+=n;++characters;
        }
        if(i==page || end==len) { *offset=start;return end-start; }
        start=end;
    }
}
static unsigned page_count(const char *text)
{
    unsigned page=0;size_t offset,n,len=strlen(text);
    do {
        n=page_bounds(text,page,&offset);
        if(!n)break; /* A truncated UTF-8 tail must not stall counting. */
        ++page;
    } while(offset+n<len);
    return page?page:1;
}
static void wrap_caption(char *text,size_t cap)
{
    char source[128];snprintf(source,sizeof(source),"%s",text);
    size_t out=0;unsigned columns=0;
    for(size_t i=0;source[i]&&out+5<cap;){
        unsigned char c=(unsigned char)source[i];
        size_t n=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;
        if(columns==LINE_CHARACTERS){text[out++]='\n';columns=0;}
        if(strlen(source+i)<n)break;
        if(c=='\n'||c=='\r')text[out]=' ';
        else memcpy(text+out,source+i,n);
        out+=n;i+=n;++columns;
    }
    text[out]='\0';
}
static const char *english_error(const char *text)
{
    static const struct {const char *zh,*en;} errors[]={
        {"请先在 Muse App 配对","Pair in the Muse App first"},
        {"网络未连接，请稍后重试","Network offline. Try again"},
        {"朗读失败，文字已保留，可重试","Speech failed. Text saved"},
        {"Muse 尚未就绪，请检查配对和网络","Muse offline. Check pairing"},
        {"语音播放仍在停止，请重试","Stopping speech. Try again"},
        {"无法开始朗读，请重试","Cannot play speech. Retry"},
        {"录音传输未跟上，请重试","Recording interrupted. Retry"},
        {"Muse 对话失败，请检查连接后重试","Chat failed. Check connection"},
        {"无法开始朗读，文字已保留","Speech failed. Text saved"},
        {"没有收到文字回答，请重试","No reply received. Try again"},
        {"本次等待超时，请重新提问","Timed out. Ask again"},
        {"操作繁忙，请重试","Busy. Please try again"},
        {"配置保存失败，请重试","Setup save failed. Retry"}
    };
    for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);++i)
        if(!strcmp(text,errors[i].zh))return errors[i].en;
    return "Connection failed. Try again";
}
static void render(companion_ui_handle_t s)
{
    companion_model_snapshot(s->model,&s->snapshot);
    companion_snapshot_t *m=&s->snapshot;char text[128];
    if(s->last_brightness!=m->display_brightness){
        companion_platform_brightness(m->display_brightness);s->last_brightness=m->display_brightness;
    }
    s->previous.display_brightness=m->display_brightness;
    if(s->scene_pending)return;
    if(s->rendered&&!memcmp(m,&s->previous,sizeof(*m)))return;
    if(m->settings!=s->current_settings){
        s->requested_settings=m->settings;s->scene_pending=true;
        esp_gsp_err_t err=esp_gsp_goto_scene(s->ui,m->settings?1:0,ESP_GSP_NO_TRANSITION);
        checked(s,err);
        if(err!=ESP_GSP_OK)s->scene_pending=false;
        s->wave_mask=0;
        return;
    }
    bool en=m->language!=0;
    const char *ssid;
    if(companion_platform_setup(&ssid,NULL)){
        for(unsigned i=0;i<6;++i)checked(s,pose[i](s->ui,i==0));
        checked(s,gsp_companion_happy_set_visible(s->ui,false));
        checked(s,gsp_companion_boot_set_visible(s->ui,false));
        checked(s,gsp_companion_connection_set_text(s->ui,en?"First-time setup":"首次配置"));
        checked(s,gsp_companion_caption_set_text(s->ui,""));
        checked(s,gsp_companion_reply_card_set_visible(s->ui,true));
        checked(s,gsp_companion_reply_text_set_text(s->ui,ssid));
        checked(s,gsp_companion_reply_text_2_set_text(s->ui,en?"No password needed":"无需密码，直接连接"));
        checked(s,gsp_companion_hint_set_text(s->ui,en?"Join Wi-Fi / 192.168.4.1":"手机连接热点 / 192.168.4.1"));
        checked(s,gsp_companion_talk_hint_set_text(s->ui,"http://192.168.4.1"));
        checked(s,esp_gsp_component_set_visible(s->ui,GSP_COMPANION_OBJ_KEY_MUTE,false));
        checked(s,esp_gsp_component_set_visible(s->ui,GSP_COMPANION_OBJ_KEY_SETTINGS_OPEN,false));
        checked(s,gsp_companion_icon_speaker_set_visible(s->ui,false));
        checked(s,gsp_companion_icon_muted_set_visible(s->ui,false));
        checked(s,gsp_companion_icon_gear_set_visible(s->ui,false));
        s->previous=*m;s->rendered=true;return;
    }
    if(m->settings){
        checked(s,gsp_companion_settings_volume_set_value(s->ui,m->volume));
        checked(s,gsp_companion_settings_brightness_set_value(s->ui,m->brightness));
        snprintf(text,sizeof(text),en?"Volume %u%%":"音量 %u%%",m->volume);
        checked(s,gsp_companion_settings_volume_text_set_text(s->ui,text));
        snprintf(text,sizeof(text),en?"Brightness %u%%":"亮度 %u%%",m->brightness);
        checked(s,gsp_companion_settings_brightness_text_set_text(s->ui,text));
        checked(s,gsp_companion_settings_title_set_text(s->ui,en?"Settings":"伙伴设置"));
        checked(s,gsp_companion_settings_phone_setup_set_text(s->ui,en?"Phone setup":"手机配置"));
        checked(s,gsp_companion_settings_back_label_set_text(s->ui,en?"Back":"返回"));
        checked(s,gsp_companion_settings_language_text_set_text(s->ui,en?"Language":"语言"));
        checked(s,gsp_companion_settings_language_value_set_text(s->ui,en?"English":"中文"));
        checked(s,gsp_companion_settings_sleep_set_value(s->ui,sleep_index(m->sleep_seconds)));
        checked(s,gsp_companion_settings_rate_set_value(s->ui,m->speech_rate));
        checked(s,gsp_companion_settings_page_set_value(s->ui,m->page_seconds));
        snprintf(text,sizeof(text),en?"Screen off %us":"自动熄屏 %u秒",m->sleep_seconds);
#ifdef COMPANION_LOCAL_CODEX
        snprintf(text,sizeof(text),"%s",en?"Screen always on":"屏幕保持常亮");
#endif
        checked(s,gsp_companion_settings_sleep_text_set_text(s->ui,text));
        snprintf(text,sizeof(text),en?"Rate %u.%02ux":"语速 %u.%02ux",m->speech_rate/100,m->speech_rate%100);
        checked(s,gsp_companion_settings_rate_text_set_text(s->ui,text));
        snprintf(text,sizeof(text),en?"Auto page %us":"翻页 %u秒",m->page_seconds);
        checked(s,gsp_companion_settings_page_text_set_text(s->ui,text));
        snprintf(text,sizeof(text),en?"Network %s / Muse %s":"网络 %s · Muse %s",
            m->online?(en?"online":"已连接"):(en?"offline":"未连接"),m->paired?(en?"paired":"已配对"):(en?"unpaired":"未配对"));
#ifdef COMPANION_LOCAL_CODEX
        snprintf(text,sizeof(text),"USB Codex %s",m->online?(en?"connected":"已连接"):(en?"offline":"未连接"));
        checked(s,gsp_companion_settings_phone_setup_set_text(s->ui,en?"USB connection":"USB 连接说明"));
#endif
        checked(s,gsp_companion_settings_pair_status_set_text(s->ui,text));
        s->previous=*m;s->rendered=true;return;
    }
    unsigned visual=character_pose(s,m);
    bool happy=visual==6, greeting=visual==7;
    for(unsigned i=0;i<6;++i) checked(s,pose[i](s->ui,!happy&&!greeting&&m->mode==(companion_mode_t)i));
    checked(s,gsp_companion_happy_set_visible(s->ui,happy));
    checked(s,gsp_companion_boot_set_visible(s->ui,greeting));
    checked(s,gsp_companion_heart_0_set_visible(s->ui,happy&&!COMPANION_ANIMATED_CHARACTER));
    checked(s,gsp_companion_heart_1_set_visible(s->ui,happy&&!COMPANION_ANIMATED_CHARACTER));
    for(unsigned i=0;i<3;++i){
        static esp_gsp_err_t (*const thought[])(esp_gsp_handle_t,bool)={
            gsp_companion_thought_0_set_visible,gsp_companion_thought_1_set_visible,gsp_companion_thought_2_set_visible};
        checked(s,thought[i](s->ui,!COMPANION_ANIMATED_CHARACTER&&m->mode==COMP_THINKING));
    }
    snprintf(text,sizeof(text),"%s",m->paired?(m->online?(en?"Muse online":"Muse 在线"):(en?"Connecting":"正在连接")):(en?"Pair device":"等待配对"));
#ifdef COMPANION_LOCAL_CODEX
    snprintf(text,sizeof(text),"Codex %s",m->online?(en?"connected":"已连接"):(en?"offline":"未连接"));
#endif
    checked(s,gsp_companion_connection_set_text(s->ui,text));
    if(m->battery>=0) snprintf(text,sizeof(text),"%d%%",m->battery);
    else snprintf(text,sizeof(text),"--");
    checked(s,gsp_companion_battery_set_text(s->ui,text));
    checked(s,gsp_companion_battery_fill_set_value(s->ui,m->battery<0?0:(m->battery>100?100:m->battery)));
    checked(s,gsp_companion_icon_speaker_set_visible(s->ui,!m->muted));
    checked(s,gsp_companion_icon_muted_set_visible(s->ui,m->muted));
    const char *caption=en?"Hi! Want to chat?":"我在，想聊点什么？",*hint=en?"Hold the top AI key to talk":"按住顶部 AI 键说话";
#ifdef COMPANION_CORES3
    hint=en?"Hold to talk":"按住说话";
#endif
    if(m->mode==COMP_LISTENING) {caption=(en?"Listening...":"我在听…");hint=(en?"Release AI to send":"松开 AI 键发送");}
    else if(m->mode==COMP_THINKING) {caption=(en?"Thinking...":"让我想一想…");hint=(en?"Hold AI to ask again":"按住 AI 键可以重新提问");}
    else if(m->mode==COMP_SLEEPY) {caption=(en?"Taking a little nap...":"我先眯一会儿…");hint=(en?"Pick up, touch or press to wake":"拿起、触摸或按键唤醒");}
    else if(m->mode==COMP_ERROR) {caption=en?english_error(m->error):m->error;hint=(en?"Hold AI to try again":"按住 AI 键重试");}
    bool reply=m->reply[0]&&m->mode!=COMP_LISTENING&&m->mode!=COMP_THINKING&&m->mode!=COMP_ERROR;
    bool next_page=false;
    if(reply) {size_t offset;size_t n=page_bounds(m->reply,m->page,&offset);
        next_page=s->reply_pages?m->page+1<s->reply_pages:offset+n<strlen(m->reply);
        memcpy(text,m->reply+offset,n);text[n]='\0';wrap_caption(text,sizeof(text));caption=text;
        hint=m->muted?(en?"Muted / captions on":"已静音 · 字幕保留"):(en?"Hold AI to interrupt":"按住 AI 键可以打断");}
    if(reply&&s->reply_pages>1){
        snprintf(s->page_hint,sizeof(s->page_hint),en?"%s / %u/%u":"%s · %u/%u 页",
            s->auto_pages?(en?"Auto":"自动翻页"):(en?"Manual":"手动翻页"),m->page+1,s->reply_pages);
        hint=m->asleep||m->mode==COMP_SLEEPY?(en?"Touch to keep reading":"轻触屏幕继续阅读"):s->page_hint;
    }
#ifdef COMPANION_CORES3
    if(m->mode==COMP_LISTENING)hint=en?"Release to send":"松开发送";
    else if(m->mode==COMP_THINKING)hint=en?"Hold here to ask again":"按住这里重新提问";
    else if(m->mode==COMP_ERROR)hint=en?"Hold here to retry":"按住这里重试";
    else if(m->mode==COMP_SLEEPY)hint=en?"Touch to wake":"触摸唤醒";
#endif
    checked(s,gsp_companion_reply_card_set_visible(s->ui,reply));
    checked(s,gsp_companion_caption_set_text(s->ui,reply?"":caption));
    char *second=reply?strchr(text,'\n'):NULL;
    if(second){*second='\0';++second;}
    checked(s,gsp_companion_reply_text_set_text(s->ui,reply?caption:""));
    checked(s,gsp_companion_reply_text_2_set_text(s->ui,second?second:""));
    checked(s,gsp_companion_hint_set_text(s->ui,hint));
    checked(s,gsp_companion_talk_hint_set_text(s->ui,hint));
    checked(s,gsp_companion_cancel_set_text(s->ui,en?"Cancel":"取消"));
    checked(s,esp_gsp_component_set_visible(s->ui,GSP_COMPANION_OBJ_KEY_CANCEL,m->mode==COMP_LISTENING||m->mode==COMP_THINKING));
    checked(s,gsp_companion_page_prev_set_visible(s->ui,reply&&m->page>0));
    checked(s,gsp_companion_page_next_set_visible(s->ui,reply&&next_page));
    checked(s,gsp_companion_retry_set_text(s->ui,en?"Retry speech":"重试朗读"));
    checked(s,esp_gsp_component_set_visible(s->ui,GSP_COMPANION_OBJ_KEY_RETRY,m->mode==COMP_ERROR&&m->reply[0]&&!m->muted));
    s->previous=*m;s->rendered=true;
}
static void action_apply(companion_ui_handle_t s,companion_action_t a,unsigned value,bool persist)
{
    if(a==COMP_PAGE_NEXT||a==COMP_PAGE_PREV){
        s->auto_pages=false;s->page_elapsed_ms=0;s->rendered=false;
    }
    unsigned effects=companion_model_action(s->model,a,value);
    /* Drag events preview immediately; only release commits preferences to flash. */
    if(!persist&&(effects&COMP_EFFECT_PREFS))effects=(effects&~COMP_EFFECT_PREFS)|COMP_EFFECT_PREVIEW;
    companion_model_snapshot(s->model,&s->snapshot);
    if(effects&&!companion_platform_effect(effects,&s->snapshot))
        companion_model_error(s->model,s->snapshot.generation,"操作繁忙，请重试");
    render(s);
}
static void action(companion_ui_handle_t s,companion_action_t a,unsigned value)
{action_apply(s,a,value,true);}
static void slider(companion_ui_handle_t s,companion_action_t a,unsigned value,bool persist)
{
    companion_model_snapshot(s->model,&s->snapshot);
    unsigned previous=a==COMP_VOLUME?s->snapshot.volume:a==COMP_BRIGHTNESS?s->snapshot.brightness:
        a==COMP_SLEEP_TIMEOUT?s->snapshot.sleep_seconds:a==COMP_SPEECH_RATE?s->snapshot.speech_rate:s->snapshot.page_seconds;
    if(a==COMP_PAGE_INTERVAL)s->page_elapsed_ms=0;
    if(!persist&&value==previous)action(s,COMP_WAKE,0);
    else action_apply(s,a,value,persist);
}
static int rise(unsigned tick,unsigned period,unsigned pixels)
{
    unsigned half=period/2,p=tick%period;
    if(p>half)p=period-p;
    return (int)(pixels*p*p*(3*half-2*p)/(half*half*half));
}
static void position(companion_ui_handle_t s,unsigned key,int x,int y)
{
    gsp_value_t v={.type=GSP_VALUE_I32,.data.i32=x};
    checked(s,esp_gsp_component_set_property(s->ui,key,GSP_PROPERTY_KEY_X,&v));
    v.data.i32=y;
    checked(s,esp_gsp_component_set_property(s->ui,key,GSP_PROPERTY_KEY_Y,&v));
}
static void character_motion(companion_ui_handle_t s)
{
#ifdef COMPANION_CORES3
    /* Moving transparent sprites every 100 ms causes visible partial GRAM
     * repaints on the SPI LCD. Keep each pose stationary on CoreS3. */
    return;
#endif
    if(COMPANION_ANIMATED_CHARACTER)return; /* GSP owns GIF playback. */
    if(s->scene_pending||s->current_settings||s->snapshot.settings)return;
    static const unsigned keys[CHARACTER_POSES]={GSP_COMPANION_OBJ_KEY_IDLE,GSP_COMPANION_OBJ_KEY_LISTENING,
        GSP_COMPANION_OBJ_KEY_THINKING,GSP_COMPANION_OBJ_KEY_SPEAKING,GSP_COMPANION_OBJ_KEY_SLEEPY,
        GSP_COMPANION_OBJ_KEY_ERROR,GSP_COMPANION_OBJ_KEY_HAPPY,GSP_COMPANION_OBJ_KEY_BOOT};
    unsigned mode=character_pose(s,&s->snapshot);
    if(mode>=CHARACTER_POSES)return;
    if(s->motion_pose!=(int)mode){
        if(s->motion_pose>=0)position(s,keys[s->motion_pose],CHARACTER_X,CHARACTER_Y);
        s->motion_pose=(int)mode;
    }
    if(s->snapshot.asleep)return;
    unsigned t=s->tick;int dx=0,dy=0;
    switch(mode){
    case COMP_IDLE:dy=-rise(t,40,6);break;
    case COMP_LISTENING:dx=rise(t,30,4)-2;dy=-rise(t,30,3);break;
    case COMP_THINKING:dx=rise(t,60,4)-2;dy=-rise(t,40,4);break;
    case COMP_SPEAKING:dx=rise(t,24,4)-2;dy=-rise(t,12,5);break;
    case COMP_SLEEPY:dy=2-rise(t,60,3);break;
    case COMP_ERROR:dx=rise(t,10,14)-7;break;
    case 6:dx=rise(t,24,4)-2;dy=-rise(t,12,14);break;
    case 7:dx=rise(t,30,8)-4;dy=-rise(t,20,4);break;
    default:break;
    }
    position(s,keys[mode],CHARACTER_X+dx,CHARACTER_Y+dy);
    if(mode==6){
        static const unsigned hearts[]={GSP_COMPANION_OBJ_KEY_HEART_0,GSP_COMPANION_OBJ_KEY_HEART_1};
        for(unsigned i=0;i<2;++i){
            gsp_value_t v={.type=GSP_VALUE_I32,.data.i32=
#ifdef COMPANION_CORES3
                50-(int)((t+i*13)%26)
#else
                105-(int)((t+i*13)%30)
#endif
            };
            checked(s,esp_gsp_component_set_property(s->ui,hearts[i],GSP_PROPERTY_KEY_Y,&v));
        }
    }
    if(mode==COMP_THINKING){
        static esp_gsp_err_t (*const dots[])(esp_gsp_handle_t,bool)={
            gsp_companion_thought_0_set_visible,gsp_companion_thought_1_set_visible,gsp_companion_thought_2_set_visible};
        for(unsigned i=0;i<3;++i)checked(s,dots[i](s->ui,(t/4)%3==i));
    }
}
static void event(esp_gsp_handle_t ui,const esp_gsp_event_t *e,void *ctx)
{
    (void)ui;companion_ui_handle_t s=ctx;
    printf("UI event type=%u action=%u arg=%u\n",(unsigned)e->type,(unsigned)e->action_id,(unsigned)e->arg);
    if(e->type==ESP_GSP_EVENT_SCENE_CHANGED){
        if(s->scene_pending)s->current_settings=s->requested_settings;
        s->scene_pending=false;s->rendered=false;s->motion_pose=-1;render(s);return;
    }
    if(companion_platform_setup(NULL,NULL))return;
    if(gsp_companion_event_is_pet(e)){
        action(s,COMP_WAKE,0);
        if(s->snapshot.mode==COMP_IDLE){
            s->pet_ticks=40;s->greeting_ticks=0;s->rendered=false;render(s);
        }
    }
    else if(gsp_companion_event_is_ai_press(e)) {
        if(companion_platform_confirm_pairing()) action(s,COMP_WAKE,0);
        else action(s,COMP_PRESS,0);
    }
    else if(gsp_companion_event_is_ai_release(e)) action(s,COMP_RELEASE,0);
    else if(gsp_companion_event_is_mute(e)) action(s,COMP_MUTE,0);
    else if(gsp_companion_event_is_cancel(e)) action(s,COMP_CANCEL,0);
    else if(gsp_companion_event_is_settings_open(e)) action(s,COMP_SETTINGS,0);
    else if(gsp_companion_settings_event_is_phone_setup(e)){
        action(s,COMP_WAKE,0);
        if(!companion_platform_setup_request()){
            companion_model_action(s->model,COMP_BACK,0);
            companion_model_error(s->model,s->snapshot.generation,"操作繁忙，请重试");render(s);
        }
    }
    else if(gsp_companion_settings_event_is_settings_back(e)) action(s,COMP_BACK,0);
    else if(gsp_companion_settings_event_is_volume_preview(e)) slider(s,COMP_VOLUME,e->arg,false);
    else if(gsp_companion_settings_event_is_volume_change(e)) slider(s,COMP_VOLUME,e->arg,true);
    else if(gsp_companion_settings_event_is_brightness_preview(e)) slider(s,COMP_BRIGHTNESS,e->arg,false);
    else if(gsp_companion_settings_event_is_brightness_change(e)) slider(s,COMP_BRIGHTNESS,e->arg,true);
    else if(gsp_companion_settings_event_is_language_change(e)) action(s,COMP_LANGUAGE,!s->snapshot.language);
    else if(gsp_companion_settings_event_is_sleep_preview(e)||gsp_companion_settings_event_is_sleep_change(e))
        slider(s,COMP_SLEEP_TIMEOUT,sleep_options[e->arg<6?e->arg:5],gsp_companion_settings_event_is_sleep_change(e));
    else if(gsp_companion_settings_event_is_rate_preview(e)||gsp_companion_settings_event_is_rate_change(e))
        slider(s,COMP_SPEECH_RATE,e->arg,gsp_companion_settings_event_is_rate_change(e));
    else if(gsp_companion_settings_event_is_page_preview(e)||gsp_companion_settings_event_is_page_change(e))
        slider(s,COMP_PAGE_INTERVAL,e->arg,gsp_companion_settings_event_is_page_change(e));
    else if(gsp_companion_event_is_page_next(e)) action(s,COMP_PAGE_NEXT,0);
    else if(gsp_companion_event_is_page_prev(e)) action(s,COMP_PAGE_PREV,0);
    else if(gsp_companion_event_is_retry(e)) action(s,COMP_RETRY,0);
    else action(s,COMP_WAKE,0);
}
static void tick(esp_gsp_handle_t ui,void *ctx)
{
    (void)ui;companion_ui_handle_t s=ctx;
    if(companion_platform_setup(NULL,NULL)){
        companion_model_action(s->model,COMP_WAKE,0);
        companion_model_tick(s->model,UI_TICK_MS);
        ++s->tick;render(s);return;
    }
    bool online,paired,key,motion;int battery;
    companion_platform_tick(UI_TICK_MS);
    companion_platform_status(&online,&paired,&battery,&key,&motion);
    if(companion_platform_setup_request_failed()){
        companion_model_action(s->model,COMP_BACK,0);
        companion_model_snapshot(s->model,&s->snapshot);
        companion_model_error(s->model,s->snapshot.generation,"配置保存失败，请重试");
    }
    companion_model_connection(s->model,online,paired,battery);
    if(key!=s->last_key) {
        s->last_key=key;
        if(key&&companion_platform_confirm_pairing()) action(s,COMP_WAKE,0);
        else action(s,key?COMP_PRESS:COMP_RELEASE,0);
    }
    if(motion) action(s,COMP_WAKE,0);
    while(companion_platform_message(&s->message)) {
        companion_message_t *msg=&s->message;
        if(msg->kind==COMP_MSG_REPLY){
            companion_model_snapshot(s->model,&s->snapshot);
            if(msg->generation==s->snapshot.generation&&s->snapshot.mode!=COMP_LISTENING){
                companion_model_reply(s->model,msg->generation,msg->text,msg->speaking);
                s->reply_pages=page_count(msg->text);s->page_elapsed_ms=0;
                s->auto_pages=s->reply_pages>1;
            }
        }
        else if(msg->kind==COMP_MSG_DONE) companion_model_done(s->model,msg->generation);
        else companion_model_error(s->model,msg->generation,msg->text);
    }
    companion_model_tick(s->model,UI_TICK_MS);
    companion_model_snapshot(s->model,&s->snapshot);
    if(s->snapshot.mode!=COMP_IDLE||s->snapshot.asleep){
        if(s->pet_ticks||s->greeting_ticks)s->rendered=false;
        s->pet_ticks=s->greeting_ticks=0;
    }else{
        if(s->pet_ticks&&!--s->pet_ticks)s->rendered=false;
        if(s->greeting_ticks&&!--s->greeting_ticks)s->rendered=false;
    }
    if(s->auto_pages&&s->snapshot.reply[0]&&!s->scene_pending&&!s->snapshot.settings&&!s->snapshot.asleep&&
        (s->snapshot.mode==COMP_IDLE||s->snapshot.mode==COMP_SPEAKING)){
        s->page_elapsed_ms+=UI_TICK_MS;
        if(s->page_elapsed_ms>=s->snapshot.page_seconds*1000){
            s->page_elapsed_ms=0;
            if(s->snapshot.page+1<s->reply_pages){
                companion_model_action(s->model,COMP_AUTO_PAGE_NEXT,0);
                companion_model_snapshot(s->model,&s->snapshot);
            }
            if(s->snapshot.page+1>=s->reply_pages)s->auto_pages=false;
        }
    }
    unsigned mask=!s->snapshot.settings&&s->snapshot.mode==COMP_LISTENING ? (s->tick%2 ? 0x555U:0x7ffU):0;
    if(!s->scene_pending&&!s->current_settings&&!s->snapshot.settings&&mask!=s->wave_mask){
        static esp_gsp_err_t (*const bars[])(esp_gsp_handle_t,bool)={
            gsp_companion_wave_0_set_visible,gsp_companion_wave_1_set_visible,
            gsp_companion_wave_2_set_visible,gsp_companion_wave_3_set_visible,
            gsp_companion_wave_4_set_visible,gsp_companion_wave_5_set_visible,
            gsp_companion_wave_6_set_visible,gsp_companion_wave_7_set_visible,
            gsp_companion_wave_8_set_visible,gsp_companion_wave_9_set_visible,
            gsp_companion_wave_10_set_visible};
        for(unsigned i=0;i<11;++i)if((mask^s->wave_mask)&(1U<<i))checked(s,bars[i](s->ui,(mask&(1U<<i))!=0));
        s->wave_mask=mask;
    }
    ++s->tick;render(s);character_motion(s);
}
#ifdef COMPANION_CORES3
static void touch_observer(esp_gsp_handle_t ui,int32_t x,int32_t y,bool pressed,void *ctx)
{
    (void)ui;(void)ctx;static bool down;
    if(down!=pressed){printf("CoreS3 touch x=%ld y=%ld pressed=%u\n",(long)x,(long)y,pressed);down=pressed;}
}
#endif
esp_gsp_err_t companion_ui_create(esp_gsp_handle_t ui,companion_ui_handle_t *out)
{
    if(!ui||!out) return ESP_GSP_ERR_INVALID_ARG;
    *out=NULL;companion_ui_handle_t s=calloc(1,sizeof(*s));
    if(!s) return ESP_GSP_ERR_NO_MEM;
    s->ui=ui;s->last_brightness=101;s->model=companion_model_create();
    s->greeting_ticks=40;s->motion_pose=-1;
    if(!s->model) {free(s);return ESP_GSP_ERR_NO_MEM;}
    companion_model_snapshot(s->model,&s->snapshot);
    companion_platform_load_prefs(&s->snapshot);
    companion_model_prefs(s->model,s->snapshot.volume,s->snapshot.brightness,s->snapshot.muted);
    companion_model_options(s->model,s->snapshot.language,s->snapshot.sleep_seconds,s->snapshot.speech_rate,s->snapshot.page_seconds);
    checked(s,esp_gsp_on_event(ui,event,s));
#ifdef COMPANION_CORES3
    checked(s,esp_gsp_set_pointer_observer(ui,touch_observer,s));
#endif
    s->timer=esp_gsp_timer_create(ui,UI_TICK_MS,tick,s);
    if(!s->timer) checked(s,ESP_GSP_ERR_NO_MEM);
    render(s);
    if(s->last_error!=ESP_GSP_OK) {esp_gsp_err_t err=s->last_error;companion_ui_delete(s);return err;}
    *out=s;return ESP_GSP_OK;
}
void companion_ui_delete(companion_ui_handle_t s)
{
    if(!s)return;
    esp_gsp_on_event(s->ui,NULL,NULL);
    if(s->timer)esp_gsp_timer_delete(s->ui,s->timer);
    companion_model_delete(s->model);free(s);
}
