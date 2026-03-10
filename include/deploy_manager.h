#pragma once
/*
 * 项目部署管理
 *
 * 物模型服务：deployProject
 * 物模型属性：deployStatus
 */

void deploy_manager_init(void *dm_handle);
void deploy_manager_destroy(void);
void deploy_manager_handle(const char *params_json,
                           char *reply_json, int reply_len);
