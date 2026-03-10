#pragma once
/*
 * IDE 连接锁管理
 *
 * 物模型属性：
 *   hasIDEConnected  (bool)  - 是否有控制端已连接
 *   IDEInfo          (text)  - 已连接控制端信息 JSON
 *   IDEHeartbeat     (text)  - 最后心跳时间戳（毫秒字符串）
 *
 * 物模型服务：
 *   requestConnect    - 申请连接
 *   requestDisconnect - 断开连接
 *   ideHeartbeat      - 心跳
 */
#include <stdbool.h>

#define HEARTBEAT_TIMEOUT_MS  (2 * 60 * 1000LL)
#define HEARTBEAT_CHECK_SEC   30

/**
 * 初始化连接管理器（启动心跳检查线程）
 * @param dm_handle  aiot_dm handle，用于上报属性
 */
void ide_conn_init(void *dm_handle);
void ide_conn_destroy(void);

/** 服务处理 */
void ide_conn_handle_connect(const char *params_json,
                             char *reply_json, int reply_len);
void ide_conn_handle_disconnect(const char *params_json,
                                char *reply_json, int reply_len);
void ide_conn_handle_heartbeat(const char *params_json,
                               char *reply_json, int reply_len);

/** 启动时清除云端缓存状态 */
void ide_conn_clear_on_startup(void);
