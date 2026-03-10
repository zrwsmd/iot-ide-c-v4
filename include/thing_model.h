#pragma once
/*
 * 物模型入口
 *
 * 对应 Java 版 ThingSample.java
 * 管理属性上报、服务分发、子模块生命周期
 */
#include <stddef.h>

/**
 * 初始化物模型（在 MQTT 连接成功后调用）
 * @param mqtt_handle  aiot_mqtt handle
 * @param product_key  设备 ProductKey
 * @param device_name  设备 DeviceName
 */
void thing_model_init(void *mqtt_handle,
                      const char *product_key,
                      const char *device_name);
void thing_model_destroy(void);

/**
 * 上报单个属性
 * @param property_id  属性标识符，如 "hasIDEConnected"
 * @param value_json   属性值的 JSON 表示，如 "1" / ""hello""
 */
int thing_model_post_property(const char *property_id,
                              const char *value_json);

/**
 * 服务调用分发（由 MQTT recv 回调触发）
 */
void thing_model_on_service_invoke(const char *service_id,
                                   const char *params_json,
                                   char *reply_json, int reply_len);
