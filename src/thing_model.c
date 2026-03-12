/*
 * thing_model.c
 *
 * 对应 Java 端 ThingSample.java
 * 使用 C Link SDK 4.x aiot_dm API：
 *   - 属性上报：aiot_dm_send (AIOT_DMMSG_PROPERTY_POST)
 *   - 服务处理：AIOT_DMRECV_ASYNC_SERVICE_INVOKE 回调
 */
#include "thing_model.h"
#include "ide_connection.h"
#include "deploy_manager.h"
#include "start_manager.h"

#include "aiot_dm_api.h"
#include "aiot_mqtt_api.h"
#include "aiot_state_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *g_dm_handle   = NULL;
static void *g_mqtt_handle = NULL;

/* ── aiot_dm 接收回调 ─────────────────────────── */
static void dm_recv_handler(void *dm_handle,
                            const aiot_dm_recv_t *recv,
                            void *userdata) {
    switch (recv->type) {

    /* 属性上报的云端应答（一般不处理） */
    case AIOT_DMRECV_GENERIC_REPLY:
        break;

    /* 云端下发属性设置 */
    case AIOT_DMRECV_PROPERTY_SET:
        printf("[thing] property set: %.*s\n",
               recv->data.property_set.params_len,
               recv->data.property_set.params);
        /* TODO: 如需处理 ADASSwitch 等可写属性，在此添加逻辑 */
        break;

    /* 异步服务调用（我们的服务全部用异步） */
    case AIOT_DMRECV_ASYNC_SERVICE_INVOKE: {
        const char *sid    = recv->data.async_service_invoke.service_id;
        const char *params = recv->data.async_service_invoke.params;
        printf("[thing] service invoke: %s\n", sid);

        char reply[1024] = "";
        thing_model_on_service_invoke(sid, params ? params : "{}",
                                      reply, sizeof(reply));

        /* 回复云端 */
        aiot_dm_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = AIOT_DMMSG_ASYNC_SERVICE_REPLY;
        msg.data.async_service_reply.msg_id     =
            recv->data.async_service_invoke.msg_id;
        msg.data.async_service_reply.code       = 200;
        msg.data.async_service_reply.service_id = sid;
        msg.data.async_service_reply.params      = reply;
        aiot_dm_send(dm_handle, &msg);
        break;
    }

    default:
        break;
    }
}

/* ── 公开接口 ─────────────────────────────────── */
void thing_model_init(void *mqtt_handle,
                      const char *product_key,
                      const char *device_name) {
    g_mqtt_handle = mqtt_handle;

    /* 创建 dm handle */
    g_dm_handle = aiot_dm_init();
    if (!g_dm_handle) {
        printf("[thing] aiot_dm_init failed\n");
        return;
    }

    /* 绑定 MQTT */
    aiot_dm_setopt(g_dm_handle, AIOT_DMOPT_MQTT_HANDLE, mqtt_handle);
    /* 注册接收回调 */
    aiot_dm_setopt(g_dm_handle, AIOT_DMOPT_RECV_HANDLER, (void *)dm_recv_handler);

    /* 初始化子模块 */
    ide_conn_init(g_dm_handle);
    deploy_manager_init(g_dm_handle);
    start_manager_init(g_dm_handle);

    /* 清除云端缓存状态 */
    ide_conn_clear_on_startup();

    printf("[thing] thing_model initialized\n");
}

void thing_model_destroy(void) {
    ide_conn_destroy();
    deploy_manager_destroy();
    start_manager_destroy();
    if (g_dm_handle) {
        aiot_dm_deinit(&g_dm_handle);
        g_dm_handle = NULL;
    }
    printf("[thing] thing_model destroyed\n");
}

int thing_model_post_property(const char *property_id,
                              const char *value_json) {
    if (!g_dm_handle) return -1;

    /* 构造 params JSON: {"propertyId": value} */
    char params[1024];
    snprintf(params, sizeof(params), "{\"%s\":%s}", property_id, value_json);

    aiot_dm_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AIOT_DMMSG_PROPERTY_POST;
    msg.data.property_post.params = params;

    int ret = aiot_dm_send(g_dm_handle, &msg);
    if (ret < 0)
        printf("[thing] post property %s failed: %d\n", property_id, ret);
    else
        printf("[thing] post property %s = %s\n", property_id, value_json);
    return ret;
}

void thing_model_on_service_invoke(const char *service_id,
                                   const char *params_json,
                                   char *reply_json, int reply_len) {
    if (strcmp(service_id, "requestConnect") == 0) {
        ide_conn_handle_connect(params_json, reply_json, reply_len);
    } else if (strcmp(service_id, "requestDisconnect") == 0) {
        ide_conn_handle_disconnect(params_json, reply_json, reply_len);
    } else if (strcmp(service_id, "ideHeartbeat") == 0) {
        ide_conn_handle_heartbeat(params_json, reply_json, reply_len);
    } else if (strcmp(service_id, "deployProject") == 0) {
        deploy_manager_handle(params_json, reply_json, reply_len);
    } else if (strcmp(service_id, "startProject") == 0) {
        start_manager_handle(params_json, reply_json, reply_len);
    } else if (strcmp(service_id, "restart") == 0) {
        snprintf(reply_json, reply_len,
                 "{\"success\":true,\"message\":\"restarting\"}");
    } else {
        snprintf(reply_json, reply_len,
                 "{\"success\":false,\"message\":\"unknown service: %s\"}", service_id);
    }
}