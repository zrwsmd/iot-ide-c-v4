/*
 * start_manager.c
 *
 * 对应 Java 端 StartManager.java
 * 在指定目录后台执行 startCommand，不下载不构建。
 */
#include "start_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

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

typedef struct {
    char project_name[256];
    char deploy_path[512];
    char start_command[512];
} start_task_t;

static void *start_worker(void *arg) {
    start_task_t *task = (start_task_t *)arg;

    char target_path[768];
    snprintf(target_path, sizeof(target_path), "%s/%s",
             task->deploy_path, task->project_name);

    printf("[start] starting service in: %s\n", target_path);
    printf("[start] command: %s\n", task->start_command);

    /* 用 bash -l 加载 ~/.profile，确保 nvm 等工具的 PATH 可见 */
    char bg_cmd[2048];
    snprintf(bg_cmd, sizeof(bg_cmd),
             "/bin/bash -l -c \"cd '%s' && %s\" &",
             target_path, task->start_command);

    int rc = system(bg_cmd);
    (void)rc;

    printf("[start] service started in background\n");

    free(task);
    return NULL;
}

void start_manager_init(void *dm_handle) {
    (void)dm_handle;
    printf("[start] initialized\n");
}

void start_manager_destroy(void) {
    printf("[start] destroyed\n");
}

void start_manager_handle(const char *params_json,
                          char *reply_json, int reply_len) {
    start_task_t *task = (start_task_t *)calloc(1, sizeof(start_task_t));
    if (!task) {
        snprintf(reply_json, reply_len,
                 "{\"success\":false,\"message\":\"malloc failed\"}");
        return;
    }

    extract_param(params_json, "projectName",  task->project_name,  sizeof(task->project_name));
    extract_param(params_json, "deployPath",   task->deploy_path,   sizeof(task->deploy_path));
    extract_param(params_json, "startCommand", task->start_command, sizeof(task->start_command));

    if (task->project_name[0] == '\0') strcpy(task->project_name, "project");
    if (task->deploy_path[0]  == '\0') strcpy(task->deploy_path,  "/tmp/deploy");

    if (task->start_command[0] == '\0') {
        snprintf(reply_json, reply_len,
                 "{\"success\":false,\"message\":\"startCommand is empty\"}");
        free(task);
        return;
    }

    /* 立即回复控制端，后台异步启动 */
    snprintf(reply_json, reply_len,
             "{\"success\":true,\"message\":\"start task received, executing...\"}");

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, start_worker, task);
    pthread_attr_destroy(&attr);
}