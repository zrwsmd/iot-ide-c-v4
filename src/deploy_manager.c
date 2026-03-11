/*
 * deploy_manager.c
 *
 * 对应 Java 端 DeployManager.java
 * 异步下载 zip、解压、执行 shell 命令、上报 deployStatus。
 */
#include "deploy_manager.h"
#include "thing_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <zip.h>

#define TMP_DIR "/tmp/deploy_tmp"

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

static void json_escape(const char *src, char *dst, int dst_len) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_len - 2; i++) {
        char c = src[i];
        if      (c == '"')  { dst[j++] = '\\'; dst[j++] = '"';  }
        else if (c == '\\') { dst[j++] = '\\'; dst[j++] = '\\'; }
        else if (c == '\n') { dst[j++] = '\\'; dst[j++] = 'n';  }
        else if (c == '\r') { dst[j++] = '\\'; dst[j++] = 'r';  }
        else if (c == '\t') { dst[j++] = '\\'; dst[j++] = 't';  }
        else dst[j++] = c;
    }
    dst[j] = '\0';
}

static void report_deploy_status(bool success, const char *message,
                                 const char *deploy_log,
                                 const char *project_name,
                                 const char *deploy_path) {
    char esc_msg[512], esc_log[4096], esc_name[256], esc_path[256];
    json_escape(message,      esc_msg,  sizeof(esc_msg));
    json_escape(deploy_log,   esc_log,  sizeof(esc_log));
    json_escape(project_name, esc_name, sizeof(esc_name));
    json_escape(deploy_path,  esc_path, sizeof(esc_path));

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long now = (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;

    char status_json[8192];
    snprintf(status_json, sizeof(status_json),
             "{\"success\":%s,\"message\":\"%s\",\"deployLog\":\"%s\","
             "\"timestamp\":%lld,\"projectName\":\"%s\",\"deployPath\":\"%s\"}",
             success ? "true" : "false",
             esc_msg, esc_log, now, esc_name, esc_path);

    thing_model_post_property("deployStatus", status_json);
}

typedef struct { FILE *fp; } curl_ctx_t;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    return fwrite(ptr, size, nmemb, ((curl_ctx_t *)ud)->fp);
}

static int download_zip(const char *url, const char *out_path,
                        char *log_buf, int log_len) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    FILE *fp = fopen(out_path, "wb");
    if (!fp) { curl_easy_cleanup(curl); return -1; }

    curl_ctx_t ctx = { fp };
    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        snprintf(log_buf, log_len, "curl error: %s\n", curl_easy_strerror(res));
        return -1;
    }
    snprintf(log_buf, log_len, "downloaded to %s\n", out_path);
    return 0;
}

static int extract_zip(const char *zip_path, const char *target_dir,
                       char *log_buf, int log_len) {
    int err = 0;
    zip_t *za = zip_open(zip_path, ZIP_RDONLY, &err);
    if (!za) { snprintf(log_buf, log_len, "zip_open failed: %d\n", err); return -1; }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", target_dir);
    system(cmd);

    zip_int64_t count = zip_get_num_entries(za, 0);
    int file_count = 0;

    for (zip_int64_t i = 0; i < count; i++) {
        const char *name = zip_get_name(za, i, 0);
        if (!name || strstr(name, "..")) continue;

        char out_path[1024];
        snprintf(out_path, sizeof(out_path), "%s/%s", target_dir, name);

        if (name[strlen(name) - 1] == '/') {
            snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", out_path);
            system(cmd);
            continue;
        }

        char parent[1024];
        strncpy(parent, out_path, sizeof(parent) - 1);
        char *slash = strrchr(parent, '/');
        if (slash) {
            *slash = '\0';
            snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", parent);
            system(cmd);
        }

        zip_file_t *zf = zip_fopen_index(za, i, 0);
        if (!zf) continue;
        FILE *fp = fopen(out_path, "wb");
        if (fp) {
            char buf[8192]; zip_int64_t n;
            while ((n = zip_fread(zf, buf, sizeof(buf))) > 0)
                fwrite(buf, 1, n, fp);
            fclose(fp);
            file_count++;
        }
        zip_fclose(zf);
    }
    zip_close(za);
    snprintf(log_buf, log_len, "extracted %d files to %s\n", file_count, target_dir);
    return 0;
}

static int run_command(const char *cmd, const char *work_dir,
                       char *log_buf, int log_len) {
    char full_cmd[2048];
    snprintf(full_cmd, sizeof(full_cmd), "cd '%s' && %s", work_dir, cmd);
    FILE *pipe = popen(full_cmd, "r");
    if (!pipe) {
        snprintf(log_buf, log_len, "popen failed: %s\n", strerror(errno));
        return -1;
    }
    int offset = 0;
    char line[256];
    while (fgets(line, sizeof(line), pipe) && offset < log_len - 1) {
        int n = snprintf(log_buf + offset, log_len - offset, "%s", line);
        if (n > 0) offset += n;
    }
    int rc = pclose(pipe);
    if (rc != 0) {
        snprintf(log_buf + offset, log_len - offset,
                 "exit code: %d\n", WEXITSTATUS(rc));
        return -1;
    }
    return 0;
}

typedef struct {
    char project_name[256];
    char download_url[1024];
    char deploy_path[512];
    char deploy_command[512];
    char start_command[512];    
} deploy_task_t;

#define APPEND_LOG(fmt, ...) do { \
    int _n = snprintf(log_buf + log_off, sizeof(log_buf) - log_off, fmt, ##__VA_ARGS__); \
    if (_n > 0) log_off += _n; \
} while(0)

static void *deploy_worker(void *arg) {
    deploy_task_t *task = (deploy_task_t *)arg;
    char log_buf[8192] = "";
    char step_log[2048];
    int  log_off = 0;

    APPEND_LOG("=== download ===\n");
    char zip_path[512];
    snprintf(zip_path, sizeof(zip_path), "%s/%s.zip", TMP_DIR, task->project_name);
    system("mkdir -p '" TMP_DIR "'");

    if (download_zip(task->download_url, zip_path, step_log, sizeof(step_log)) != 0) {
        APPEND_LOG("download failed: %s\n", step_log);
        report_deploy_status(false, "download failed", log_buf,
                             task->project_name, task->deploy_path);
        free(task); return NULL;
    }
    APPEND_LOG("%s", step_log);

    APPEND_LOG("=== extract ===\n");
    char target_path[768];
    snprintf(target_path, sizeof(target_path), "%s/%s",
             task->deploy_path, task->project_name);

    if (extract_zip(zip_path, target_path, step_log, sizeof(step_log)) != 0) {
        APPEND_LOG("extract failed: %s\n", step_log);
        unlink(zip_path);
        report_deploy_status(false, "extract failed", log_buf,
                             task->project_name, task->deploy_path);
        free(task); return NULL;
    }
    APPEND_LOG("%s", step_log);
    unlink(zip_path);

    if (task->deploy_command[0] != '\0') {
        APPEND_LOG("=== run command ===\n$ %s\n", task->deploy_command);
        if (run_command(task->deploy_command, target_path,
                        step_log, sizeof(step_log)) != 0) {
            APPEND_LOG("command failed:\n%s\n", step_log);
            report_deploy_status(false, "command failed", log_buf,
                                 task->project_name, target_path);
            free(task); return NULL;
        }
        APPEND_LOG("%s\ncommand done\n", step_log);
    }

    /* 后台启动，不检查退出码（对应 Java 的 runBackground方法） */
    if (task->start_command[0] != '\0') {
        APPEND_LOG("=== start service ===\n$ %s\n", task->start_command);
        char bg_cmd[2048];
        snprintf(bg_cmd, sizeof(bg_cmd), "cd '%s' && %s &",
                target_path, task->start_command);
        int rc = system(bg_cmd);
        (void)rc;
        APPEND_LOG("service started in background\n");
    }

    APPEND_LOG("=== deploy success ===\n");
    report_deploy_status(true, "deploy success", log_buf,
                         task->project_name, target_path);
    free(task);
    return NULL;
}

void deploy_manager_init(void *dm_handle) {
    (void)dm_handle;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    printf("[deploy] initialized\n");
}

void deploy_manager_destroy(void) {
    curl_global_cleanup();
    printf("[deploy] destroyed\n");
}

void deploy_manager_handle(const char *params_json,
                           char *reply_json, int reply_len) {
    deploy_task_t *task = (deploy_task_t *)calloc(1, sizeof(deploy_task_t));
    if (!task) {
        snprintf(reply_json, reply_len,
                 "{\"success\":false,\"message\":\"malloc failed\",\"deployLog\":\"\"}");
        return;
    }
    extract_param(params_json, "projectName",   task->project_name,   sizeof(task->project_name));
    extract_param(params_json, "downloadUrl",   task->download_url,   sizeof(task->download_url));
    extract_param(params_json, "deployPath",    task->deploy_path,    sizeof(task->deploy_path));
    extract_param(params_json, "deployCommand", task->deploy_command, sizeof(task->deploy_command));
    extract_param(params_json, "startCommand", task->start_command, sizeof(task->start_command));

    if (task->project_name[0] == '\0') strcpy(task->project_name, "project");
    if (task->deploy_path[0]  == '\0') strcpy(task->deploy_path,  "/tmp/deploy");

    if (task->download_url[0] == '\0') {
        snprintf(reply_json, reply_len,
                 "{\"success\":false,\"message\":\"downloadUrl is empty\",\"deployLog\":\"\"}");
        free(task);
        return;
    }

    snprintf(reply_json, reply_len,
             "{\"success\":true,\"message\":\"deploy task received, executing...\",\"deployLog\":\"\"}");

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, deploy_worker, task);
    pthread_attr_destroy(&attr);
}