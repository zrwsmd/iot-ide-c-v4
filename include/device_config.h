#pragma once
/*
 * 设备配置 - 从 config/device_config.json 读取
 * 4.x 版本：需要 mqttHost 而不是 instanceId/region
 */
#include <stddef.h>

#define DEVICE_CONFIG_PATH "config/device_config.json"
#define MAX_STR_LEN 256

typedef struct {
    char product_key[MAX_STR_LEN];
    char device_name[MAX_STR_LEN];
    char device_secret[MAX_STR_LEN];
    char mqtt_host[MAX_STR_LEN];
    int  mqtt_port;
} device_config_t;

int  device_config_load(const char *path, device_config_t *out);
void device_config_print(const device_config_t *cfg);
