#pragma once
/*
 * trace_simulator.h
 *
 * 对应 Java 端 TraceSimulator.java
 * 单线程同步逐包发送 Trace 数据到自定义 Topic，每包等待 MQTT publish 完成后再发下一包。
 *
 * Topic：/<productKey>/<deviceName>/user/trace/data
 *
 * payload 格式：
 * {
 *   "taskId": "sim_trace_001",
 *   "seq": 1,
 *   "period": 1,
 *   "frames": [
 *     { "ts": 1, "axis1_position": 12.345, "axis1_velocity": 3.141, ... },
 *     { "ts": 2, ... }
 *   ]
 * }
 */

/* 采集周期 ms */
#define TRACE_PERIOD_MS     1
/* 每包 frame 数量 */
#define TRACE_BATCH_SIZE    100
/* 最大发送帧数 */
#define TRACE_MAX_FRAMES    10000
/* 单包最大重试次数 */
#define TRACE_MAX_RETRY     3
/* 包间发送间隔 ms */
#define TRACE_SEND_INTERVAL_MS  100

/**
 * 初始化 Trace 模拟器
 * @param mqtt_handle   aiot_mqtt handle（已连接）
 * @param product_key   设备 ProductKey
 * @param device_name   设备 DeviceName
 * @param period_ms     采集周期 ms
 * @param batch_size    每包 frame 数
 */
void trace_simulator_init(void *mqtt_handle,
                          const char *product_key,
                          const char *device_name,
                          int period_ms,
                          int batch_size);

/**
 * 异步启动发送线程（非阻塞，立即返回）
 */
void trace_simulator_start(void);

/**
 * 阻塞等待所有数据发送完成
 */
void trace_simulator_await(void);

/**
 * 停止发送
 */
void trace_simulator_stop(void);
