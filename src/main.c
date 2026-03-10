/*
 * main.c
 *
 * 对应 Java 端 HelloWorld.java
 * 使用 C Link SDK 4.x：
 *   aiot_mqtt_init / aiot_mqtt_setopt / aiot_mqtt_connect
 *   aiot_mqtt_process (心跳) + aiot_mqtt_recv (消息接收，在子线程)
 */
#include "device_config.h"
#include "thing_model.h"

#include "aiot_state_api.h"
#include "aiot_sysdep_api.h"
#include "aiot_mqtt_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

/* ── 全局 ─────────────────────────────────────── */
static volatile int  g_running          = 1;
static void         *g_mqtt_handle      = NULL;
static pthread_t     g_recv_thread      = 0;

/* ── 信号处理 ──────────────────────────────────── */
static void sig_handler(int sig) {
    (void)sig;
    printf("\n[main] signal received, shutting down...\n");
    g_running = 0;
    if (g_mqtt_handle)
        aiot_mqtt_disconnect(g_mqtt_handle);
}

/* ── MQTT 接收线程 ─────────────────────────────── */
static void *mqtt_recv_thread(void *arg) {
    void *mqtt_handle = arg;
    while (g_running) {
        int ret = aiot_mqtt_recv(mqtt_handle);
        if (ret < 0) {
            if (ret == STATE_USER_INPUT_EXEC_DISABLED) break;
            sleep(1);
        }
    }
    return NULL;
}

/* ── MQTT 事件回调 ─────────────────────────────── */
static void mqtt_event_handler(void *handle,
                               const aiot_mqtt_event_t *event,
                               void *userdata) {
    switch (event->type) {
    case AIOT_MQTTEVT_CONNECT:
        printf("[main] MQTT connected\n");
        /* 连接成功后初始化物模型 */
        {
            device_config_t *cfg = (device_config_t *)userdata;
            thing_model_init(handle, cfg->product_key, cfg->device_name);
        }
        break;
    case AIOT_MQTTEVT_RECONNECT:
        printf("[main] MQTT reconnected\n");
        break;
    case AIOT_MQTTEVT_DISCONNECT:
        printf("[main] MQTT disconnected\n");
        break;
    default:
        break;
    }
}

/* ── main ──────────────────────────────────────── */
int main(void) {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* 1. 加载设备配置 */
    device_config_t cfg;
    if (device_config_load(DEVICE_CONFIG_PATH, &cfg) != 0) {
        fprintf(stderr, "[main] Failed to load device config\n");
        return 1;
    }
    device_config_print(&cfg);

    /* 2. 初始化 MQTT */
    g_mqtt_handle = aiot_mqtt_init();
    if (!g_mqtt_handle) {
        fprintf(stderr, "[main] aiot_mqtt_init failed\n");
        return 1;
    }

    /* 3. 配置连接参数 */
    uint16_t port = (uint16_t)cfg.mqtt_port;
    aiot_mqtt_setopt(g_mqtt_handle, AIOT_MQTTOPT_HOST,          (void *)cfg.mqtt_host);
    aiot_mqtt_setopt(g_mqtt_handle, AIOT_MQTTOPT_PORT,          (void *)&port);
    aiot_mqtt_setopt(g_mqtt_handle, AIOT_MQTTOPT_PRODUCT_KEY,   (void *)cfg.product_key);
    aiot_mqtt_setopt(g_mqtt_handle, AIOT_MQTTOPT_DEVICE_NAME,   (void *)cfg.device_name);
    aiot_mqtt_setopt(g_mqtt_handle, AIOT_MQTTOPT_DEVICE_SECRET, (void *)cfg.device_secret);
    aiot_mqtt_setopt(g_mqtt_handle, AIOT_MQTTOPT_EVENT_HANDLER, (void *)mqtt_event_handler);
    aiot_mqtt_setopt(g_mqtt_handle, AIOT_MQTTOPT_USERDATA,      (void *)&cfg);

    /* TLS 安全凭据（使用 SDK 内置根证书） */
    aiot_sysdep_network_cred_t cred;
    memset(&cred, 0, sizeof(cred));
    cred.option = AIOT_SYSDEP_NETWORK_CRED_SVRCERT_CA;
    cred.x509_server_cert = NULL;        /* 使用 SDK 内置 */
    cred.x509_server_cert_len = 0;
    aiot_mqtt_setopt(g_mqtt_handle, AIOT_MQTTOPT_NETWORK_CRED, (void *)&cred);

    /* 4. 建立连接 */
    int ret = aiot_mqtt_connect(g_mqtt_handle);
    if (ret < 0) {
        fprintf(stderr, "[main] aiot_mqtt_connect failed: %d\n", ret);
        aiot_mqtt_deinit(&g_mqtt_handle);
        return 1;
    }

    /* 5. 启动接收线程 */
    pthread_create(&g_recv_thread, NULL, mqtt_recv_thread, g_mqtt_handle);

    printf("\n============================================\n");
    printf("  iot-ide-c-v4 device running\n");
    printf("  deviceName : %s\n", cfg.device_name);
    printf("  productKey : %s\n", cfg.product_key);
    printf("  Press Ctrl+C to exit\n");
    printf("============================================\n\n");

    /* 6. 主循环：定期驱动心跳和重传 */
    while (g_running) {
        aiot_mqtt_process(g_mqtt_handle);
        sleep(1);
    }

    /* 7. 清理 */
    thing_model_destroy();
    if (g_recv_thread) {
        pthread_join(g_recv_thread, NULL);
    }
    aiot_mqtt_deinit(&g_mqtt_handle);

    printf("[main] exited cleanly\n");
    return 0;
}
