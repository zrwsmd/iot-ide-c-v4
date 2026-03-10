/*
 * device_config.c  -  从 JSON 文件加载设备配置
 */
#include "device_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void extract_str(const char *json, const char *key,
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
    }
}

static int extract_int(const char *json, const char *key, int def) {
    char buf[32];
    extract_str(json, key, buf, sizeof(buf));
    if (buf[0] == '\0') return def;
    /* 如果是带引号的字符串数字也能处理 */
    return atoi(buf);
}

int device_config_load(const char *path, device_config_t *out) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[config] cannot open %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    extract_str(buf, "productKey",   out->product_key,  sizeof(out->product_key));
    extract_str(buf, "deviceName",   out->device_name,  sizeof(out->device_name));
    extract_str(buf, "deviceSecret", out->device_secret,sizeof(out->device_secret));
    extract_str(buf, "mqttHost",     out->mqtt_host,    sizeof(out->mqtt_host));
    out->mqtt_port = extract_int(buf, "mqttPort", 1883);

    free(buf);

    if (out->product_key[0] == '\0' || out->device_name[0] == '\0' ||
        out->device_secret[0] == '\0' || out->mqtt_host[0] == '\0') {
        fprintf(stderr, "[config] missing required fields\n");
        return -1;
    }
    return 0;
}

void device_config_print(const device_config_t *cfg) {
    printf("[config] productKey  : %s\n", cfg->product_key);
    printf("[config] deviceName  : %s\n", cfg->device_name);
    printf("[config] deviceSecret: ****\n");
    printf("[config] mqttHost    : %s\n", cfg->mqtt_host);
    printf("[config] mqttPort    : %d\n", cfg->mqtt_port);
}
