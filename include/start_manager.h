#pragma once
/*
 * 项目启动管理
 *
 * 物模型服务：startProject
 * 职责：在指定目录后台执行 startCommand，不下载不构建
 */

void start_manager_init(void *dm_handle);
void start_manager_destroy(void);
void start_manager_handle(const char *params_json,
                          char *reply_json, int reply_len);