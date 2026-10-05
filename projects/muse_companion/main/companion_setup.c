// SPDX-License-Identifier: Apache-2.0
#include "companion_setup.h"
#include "companion_credentials.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

enum { REQUEST_CAP=4096, SETUP_STACK=6144, RESTART_STACK=3072 };
/* Boot-owned singleton, published before UI starts. HTTP owns all mutable
 * request buffers. No SDK radio tasks run while this service is active. */
struct setup_t {
    esp_netif_t *netif;
    httpd_handle_t server;
    bool wifi_initialized, wifi_started, saving;
    char ssid[32], nonce[33], body[REQUEST_CAP+1];
    wifi_config_t wifi;
};
static struct setup_t *setup;
static atomic_bool reconfiguration_requested;
static atomic_bool reconfiguration_failed;
extern const char html_start[] asm("_binary_setup_html_start");
extern const char html_end[] asm("_binary_setup_html_end");

static void headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"Content-Security-Policy",
        "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'");
}
static esp_err_t response(httpd_req_t *req,const char *status,const char *body)
{
    headers(req);httpd_resp_set_status(req,status);
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_send(req,body,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t home(httpd_req_t *req)
{
    headers(req);httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_send(req,html_start,html_end-html_start-1);
}
static esp_err_t session(httpd_req_t *req)
{
    struct setup_t *s=req->user_ctx;char json[64];
    snprintf(json,sizeof(json),"{\"nonce\":\"%s\"}",s->nonce);
    return response(req,"200 OK",json);
}
static void restart(void *ctx)
{
    (void)ctx;uint32_t commit=0;
    xTaskNotifyWait(0,UINT32_MAX,&commit,portMAX_DELAY);
    if(commit){vTaskDelay(pdMS_TO_TICKS(2000));esp_restart();}
    vTaskDelete(NULL);
}
static esp_err_t setup_flag(bool enabled)
{
    nvs_handle_t h;esp_err_t err=nvs_open("companion_keys",NVS_READWRITE,&h);
    if(err==ESP_OK){err=nvs_set_u8(h,"setup",enabled?1:0);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}
    return err;
}
static bool setup_requested(void)
{
    uint8_t value=0;nvs_handle_t h;
    if(nvs_open("companion_keys",NVS_READONLY,&h)==ESP_OK){nvs_get_u8(h,"setup",&value);nvs_close(h);}
    return value==1;
}
static void reconfigure(void *ctx)
{
    (void)ctx;
    if(setup_flag(true)==ESP_OK){vTaskDelay(pdMS_TO_TICKS(500));esp_restart();}
    atomic_store(&reconfiguration_failed,true);
    atomic_store(&reconfiguration_requested,false);vTaskDelete(NULL);
}
esp_err_t companion_setup_request(void)
{
    if(atomic_exchange(&reconfiguration_requested,true))return ESP_OK;
    if(xTaskCreate(reconfigure,"setup_request",4096,NULL,3,NULL)!=pdPASS){
        atomic_store(&reconfiguration_requested,false);return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
bool companion_setup_request_failed(void)
{return atomic_exchange(&reconfiguration_failed,false);}
static const char *field(cJSON *json,const char *name)
{
    cJSON *item=cJSON_GetObjectItemCaseSensitive(json,name);
    return cJSON_IsString(item)?item->valuestring:NULL;
}
static esp_err_t configure(httpd_req_t *req)
{
    struct setup_t *s=req->user_ctx;char nonce[40],type[48];
    if(s->saving)return response(req,"409 Conflict","{\"error\":\"restarting\"}");
    if(httpd_req_get_hdr_value_str(req,"X-Setup-Nonce",nonce,sizeof(nonce))!=ESP_OK||strcmp(nonce,s->nonce))
        return response(req,"403 Forbidden","{\"error\":\"session\"}");
    if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||
        (strcmp(type,"application/json")&&strcmp(type,"application/json; charset=utf-8"))||
        !req->content_len||req->content_len>REQUEST_CAP)
        return response(req,"400 Bad Request","{\"error\":\"format\"}");
    size_t received=0;
    while(received<req->content_len){
        int n=httpd_req_recv(req,s->body+received,req->content_len-received);
        if(n<=0){memset(s->body,0,sizeof(s->body));
            return response(req,n==HTTPD_SOCK_ERR_TIMEOUT?"408 Request Timeout":"400 Bad Request","{\"error\":\"receive\"}");}
        received+=(size_t)n;
    }
    s->body[received]=0;
    cJSON *json=cJSON_ParseWithLengthOpts(s->body,received+1,NULL,true);
    cJSON *rate=json?cJSON_GetObjectItemCaseSensitive(json,"tts_sample_rate"):NULL;
    const companion_credentials_config_t config={
        json?field(json,"muse_token"):NULL,json?field(json,"tts_token"):NULL,
        json?field(json,"tts_url"):NULL,json?field(json,"tts_model"):NULL,
        json?field(json,"tts_voice"):NULL,cJSON_IsNumber(rate)&&rate->valuedouble==rate->valueint?(unsigned)rate->valueint:0};
    esp_err_t err=ESP_ERR_INVALID_ARG;TaskHandle_t task=NULL;
    if(cJSON_IsObject(json)&&companion_credentials_valid(&config)){
        /* Allocate the internal-stack reboot worker before committing anything. */
        if(xTaskCreate(restart,"setup_restart",RESTART_STACK,NULL,3,&task)==pdPASS){
            err=companion_credentials_save(&config);
            if(err==ESP_OK)err=setup_flag(false);
        }else err=ESP_ERR_NO_MEM;
    }
    cJSON_Delete(json);memset(s->body,0,sizeof(s->body));
    if(err==ESP_OK)s->saving=true;
    esp_err_t sent=response(req,err==ESP_OK?"200 OK":err==ESP_ERR_INVALID_ARG?"400 Bad Request":"503 Service Unavailable",
        err==ESP_OK?"{\"saved\":true,\"restart\":true}":"{\"error\":\"save\"}");
    if(task)xTaskNotify(task,err==ESP_OK?1:0,eSetValueWithOverwrite);
    return sent;
}
static void cleanup(struct setup_t *s)
{
    if(s->server)httpd_stop(s->server);
    if(s->wifi_started)esp_wifi_stop();
    if(s->wifi_initialized)esp_wifi_deinit();
    if(s->netif)esp_netif_destroy_default_wifi(s->netif);
    memset(s,0,sizeof(*s));free(s);
}
esp_err_t companion_setup_start(void)
{
    if(setup||(companion_credentials_ready()&&!setup_requested()))return ESP_OK;
    struct setup_t *s=calloc(1,sizeof(*s));if(!s)return ESP_ERR_NO_MEM;
    uint8_t random[16],mac[6];esp_fill_random(random,sizeof(random));
    esp_err_t err=esp_read_mac(mac,ESP_MAC_WIFI_STA);if(err!=ESP_OK)goto fail;
    snprintf(s->ssid,sizeof(s->ssid),"Mochi-Setup-%02X%02X",mac[4],mac[5]);
    for(unsigned i=0;i<16;++i)snprintf(s->nonce+2*i,3,"%02x",random[i]);
    err=esp_netif_init();if(err!=ESP_OK)goto fail;
    err=esp_event_loop_create_default();if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)goto fail;
    s->netif=esp_netif_create_default_wifi_ap();if(!s->netif){err=ESP_ERR_NO_MEM;goto fail;}
    /* Startup-only aggregate fits the IDF main task stack; request/audio
     * tasks never retain this initialization configuration. */
    wifi_init_config_t *init=malloc(sizeof(*init));if(!init){err=ESP_ERR_NO_MEM;goto fail;}
    const wifi_init_config_t defaults=WIFI_INIT_CONFIG_DEFAULT();*init=defaults;
    err=esp_wifi_init(init);free(init);if(err!=ESP_OK)goto fail;s->wifi_initialized=true;
    memcpy(s->wifi.ap.ssid,s->ssid,strlen(s->ssid));s->wifi.ap.ssid_len=strlen(s->ssid);
    s->wifi.ap.channel=1;
    s->wifi.ap.authmode=WIFI_AUTH_OPEN;s->wifi.ap.max_connection=2;
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(err!=ESP_OK)goto fail;
    err=esp_wifi_set_mode(WIFI_MODE_AP);if(err!=ESP_OK)goto fail;
    err=esp_wifi_set_config(WIFI_IF_AP,&s->wifi);if(err!=ESP_OK)goto fail;
    err=esp_wifi_start();if(err!=ESP_OK)goto fail;s->wifi_started=true;
    httpd_config_t http=HTTPD_DEFAULT_CONFIG();http.stack_size=SETUP_STACK;
    http.max_open_sockets=3;http.lru_purge_enable=true;http.recv_wait_timeout=5;http.send_wait_timeout=5;
    err=httpd_start(&s->server,&http);if(err!=ESP_OK)goto fail;
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=home,.user_ctx=s},
        {.uri="/api/session",.method=HTTP_GET,.handler=session,.user_ctx=s},
        {.uri="/api/config",.method=HTTP_POST,.handler=configure,.user_ctx=s}};
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);++i){err=httpd_register_uri_handler(s->server,&routes[i]);if(err!=ESP_OK)goto fail;}
    setup=s;
    ESP_LOGI("companion_setup","Open setup hotspot ready: %s / http://192.168.4.1",s->ssid);
    return ESP_OK;
fail:
    cleanup(s);return err;
}
bool companion_setup_active(const char **ssid,const char **password)
{
    if(!setup)return false;
    if(ssid)*ssid=setup->ssid;
    if(password)*password="";
    return true;
}
