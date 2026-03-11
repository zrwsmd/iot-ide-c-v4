/*
 * ide_connection.c
 *
 * 对应 Java 端 IDEConnectionManager.java
 * 使用 C Link SDK 4.x aiot_dm API 上报属性。
 */
#include "ide_connection.h"
#include "thing_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

/* ── 状态 ───────────────────────────────────── */
static pthread_mutex_t g_lock       = PTHREAD_MUTEX_INITIALIZER;
static bool            g_connected  = false;
static char            g_client_id[256] = "";
static char            g_ide_info[512]  = "";
static long long       g_last_hb_ms     = 0;

static pthread_t       g_checker_tid    = 0;
static volatile int    g_running        = 0;

/* ── 工具 ────────────────────────────────────── */
static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void extract_param(const char *json, const char *key,
                          char *out, int out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        int i = 0;
        while (*p && *p != '"' && i < out_len - 1) out[i++] = *p++;
        out[i] = '\0';
    } else {
        int i = 0;
        while (*p && *p != ',' && *p != '}' && i < out_len - 1) out[i++] = *p++;
        out[i] = '\0';
    }
}

/* ── 心跳检查线程 ────────────────────────────── */
static void *heartbeat_checker(void *arg) {
    (void)arg;
    printf("[ide_conn] heartbeat checker started\n");
    while (g_running) {
        sleep(HEARTBEAT_CHECK_SEC);
        if (!g_running) break;

        pthread_mutex_lock(&g_lock);
        if (!g_connected) {
            pthread_mutex_unlock(&g_lock);
            continue;
        }
        long long elapsed = now_ms() - g_last_hb_ms;
        if (elapsed > HEARTBEAT_TIMEOUT_MS) {
            printf("[ide_conn] heartbeat timeout (%lld ms), clearing connection\n", elapsed);
            g_connected = false;
            g_client_id[0] = '\0';
            g_ide_info[0]  = '\0';
            g_last_hb_ms   = 0;
            pthread_mutex_unlock(&g_lock);

            thing_model_post_property("hasIDEConnected", "0");
            thing_model_post_property("IDEInfo",         "\"\"");
            thing_model_post_property("IDEHeartbeat", "\"0\"");
        } else {
            pthread_mutex_unlock(&g_lock);
        }
    }
    return NULL;
}

/* ── 公开接口 ────────────────────────────────── */
void ide_conn_init(void *dm_handle) {
    (void)dm_handle;
    g_running = 1;
    pthread_create(&g_checker_tid, NULL, heartbeat_checker, NULL);
    printf("[ide_conn] initialized\n");
}

void ide_conn_destroy(void) {
    g_running = 0;
    if (g_checker_tid) {
        pthread_join(g_checker_tid, NULL);
        g_checker_tid = 0;
    }
    printf("[ide_conn] destroyed\n");
}

void ide_conn_clear_on_startup(void) {
    printf("[ide_conn] clearing stale connection state on startup\n");
    pthread_mutex_lock(&g_lock);
    g_connected  = false;
    g_client_id[0] = '\0';
    g_ide_info[0]  = '\0';
    g_last_hb_ms   = 0;
    pthread_mutex_unlock(&g_lock);

    thing_model_post_property("hasIDEConnected", "0");
    thing_model_post_property("IDEInfo",         "\"\"");
    thing_model_post_property("IDEHeartbeat", "\"0\"");
}

void ide_conn_handle_connect(const char *params_json,
                             char *reply_json, int reply_len) {
    char client_id[256] = "";
    char ide_info[512]  = "";
    extract_param(params_json, "clientId", client_id, sizeof(client_id));
    extract_param(params_json, "ideInfo",  ide_info,  sizeof(ide_info));

    if (client_id[0] == '\0') {
        snprintf(reply_json, reply_len,
                 "{\"success\":false,\"message\":\"clientId is required\"}");
        return;
    }

    long long now = now_ms();
    pthread_mutex_lock(&g_lock);

    if (!g_connected) {
        /* 空位，直接占用 */
        g_connected = true;
        strncpy(g_client_id, client_id, sizeof(g_client_id) - 1);
        strncpy(g_ide_info,  ide_info,  sizeof(g_ide_info) - 1);
        g_last_hb_ms = now;
        pthread_mutex_unlock(&g_lock);

        char hb_buf[32];
        snprintf(hb_buf, sizeof(hb_buf), "\"%lld\"", now);
        thing_model_post_property("hasIDEConnected", "1");
        char ide_info_json[768];
    snprintf(ide_info_json, sizeof(ide_info_json),
            "{\"clientId\":\"%s\",\"clientInfo\":\"%s\",\"connectTime\":%lld}",
            client_id, ide_info, now);
    thing_model_post_property("IDEInfo", ide_info_json);
        //thing_model_post_property("IDEInfo",         ide_info[0] ? ide_info : "\"{\"}\"");
        thing_model_post_property("IDEHeartbeat",    hb_buf);

        snprintf(reply_json, reply_len,
                 "{\"success\":true,\"message\":\"connected\"}");

    } else if (strcmp(g_client_id, client_id) == 0) {
        /* 同一客户端重连 */
        g_last_hb_ms = now;
        pthread_mutex_unlock(&g_lock);

        char hb_buf[32];
        snprintf(hb_buf, sizeof(hb_buf), "\"%lld\"", now);
        thing_model_post_property("IDEHeartbeat", hb_buf);

        snprintf(reply_json, reply_len,
                 "{\"success\":true,\"message\":\"reconnected\"}");

    } else {
       /* 检查心跳是否超时，超时允许抢占 */
        long long elapsed = now - g_last_hb_ms;
        if (elapsed > HEARTBEAT_TIMEOUT_MS) {
            /* 超时，允许抢占 */
            g_connected = true;
            strncpy(g_client_id, client_id, sizeof(g_client_id) - 1);
            strncpy(g_ide_info,  ide_info,  sizeof(g_ide_info) - 1);
            g_last_hb_ms = now;
            pthread_mutex_unlock(&g_lock);

            char ide_info_json[768];
            snprintf(ide_info_json, sizeof(ide_info_json),
                    "{\"clientId\":\"%s\",\"clientInfo\":\"%s\",\"connectTime\":%lld}",
                    client_id, ide_info, now);
            char hb_buf[32];
            snprintf(hb_buf, sizeof(hb_buf), "\"%lld\"", now);
            thing_model_post_property("hasIDEConnected", "1");
            thing_model_post_property("IDEInfo",         ide_info_json);
            thing_model_post_property("IDEHeartbeat",    hb_buf);
            snprintf(reply_json, reply_len,
                    "{\"success\":true,\"message\":\"原连接已超时，连接成功\"}");
        } else {
            pthread_mutex_unlock(&g_lock);
            snprintf(reply_json, reply_len,
                    "{\"success\":false,\"message\":\"another client is connected\"}");
        }
    }
}

void ide_conn_handle_disconnect(const char *params_json,
                                char *reply_json, int reply_len) {
    char client_id[256] = "";
    extract_param(params_json, "clientId", client_id, sizeof(client_id));

    pthread_mutex_lock(&g_lock);
    if (!g_connected || strcmp(g_client_id, client_id) != 0) {
        pthread_mutex_unlock(&g_lock);
        snprintf(reply_json, reply_len,
                 "{\"success\":false,\"message\":\"not connected\"}");
        return;
    }
    g_connected  = false;
    g_client_id[0] = '\0';
    g_ide_info[0]  = '\0';
    g_last_hb_ms   = 0;
    pthread_mutex_unlock(&g_lock);

    thing_model_post_property("hasIDEConnected", "0");
    thing_model_post_property("IDEInfo",         "\"\"");
    thing_model_post_property("IDEHeartbeat", "\"0\"");
    //thing_model_post_property("IDEHeartbeat",    "\"\"");

    snprintf(reply_json, reply_len,
             "{\"success\":true,\"message\":\"disconnected\"}");
}

void ide_conn_handle_heartbeat(const char *params_json,
                               char *reply_json, int reply_len) {
    char client_id[256] = "";
    extract_param(params_json, "clientId", client_id, sizeof(client_id));

    pthread_mutex_lock(&g_lock);
    if (!g_connected || strcmp(g_client_id, client_id) != 0) {
        pthread_mutex_unlock(&g_lock);
        snprintf(reply_json, reply_len,
                 "{\"success\":false,\"message\":\"not connected\"}");
        return;
    }
    long long now = now_ms();
    g_last_hb_ms = now;
    pthread_mutex_unlock(&g_lock);

    char hb_buf[32];
    snprintf(hb_buf, sizeof(hb_buf), "\"%lld\"", now);
    thing_model_post_property("IDEHeartbeat", hb_buf);

    snprintf(reply_json, reply_len,
             "{\"success\":true,\"message\":\"ok\"}");
}
