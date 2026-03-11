/*
 * trace_simulator.c
 *
 * 对应 Java 端 TraceSimulator.java
 * 使用 C Link SDK 4.x aiot_mqtt_pub 发布自定义 Topic。
 *
 * 核心机制：
 *   - 单线程同步逐包发送（对应 Java 的 sendLoop）
 *   - 每包调用 aiot_mqtt_pub 发布后继续下一包
 *   - 失败自动重试最多 TRACE_MAX_RETRY 次
 *   - 包间等待 TRACE_SEND_INTERVAL_MS 毫秒
 *
 * 注意：4.x SDK 的 aiot_mqtt_pub 是异步发布，没有 Java 版的 ACK 回调机制，
 * 这里用发布返回值 >= 0 表示成功入队，和 Java 版语义略有差异。
 */
#include "trace_simulator.h"

#include "aiot_mqtt_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

/* ── 全局状态 ─────────────────────────────────── */
static void        *g_mqtt_handle  = NULL;
static char         g_product_key[256] = "";
static char         g_device_name[256] = "";
static int          g_period_ms    = TRACE_PERIOD_MS;
static int          g_batch_size   = TRACE_BATCH_SIZE;

static pthread_t    g_send_thread  = 0;
static volatile int g_running      = 0;

static long         g_success_count = 0;
static long         g_retry_count   = 0;

/* ── 工具：毫秒级 sleep ───────────────────────── */
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ── 工具：保留3位小数 ───────────────────────── */
static double round3(double v) {
    return round(v * 1000.0) / 1000.0;
}

/* ── 噪声 ────────────────────────────────────── */
static double noise(double scale) {
    return ((double)rand() / RAND_MAX - 0.5) * 2.0 * scale * 0.02;
}

/* ── 各物理量模拟函数（对应 Java 版 simulate* 方法）── */
static double sim_axis1_position(long ts, int period_ms) {
    double t = ts * period_ms / 1000.0;
    return round3(100.0 * sin(2 * M_PI * 0.5 * t) + noise(0.5));
}

static double sim_axis1_velocity(long ts, int period_ms) {
    double t = ts * period_ms / 1000.0;
    return round3(M_PI * 100.0 * cos(2 * M_PI * 0.5 * t) + noise(1.0));
}

static double sim_axis1_torque(long ts, int period_ms) {
    double t = ts * period_ms / 1000.0;
    return round3(50.0 + 20.0 * sin(2 * M_PI * 1.0 * t) + noise(0.5));
}

static double sim_motor_rpm(long ts, int period_ms) {
    double t = ts * period_ms / 1000.0;
    return round3(1500.0 + 300.0 * sin(2 * M_PI * 0.1 * t) + noise(5.0));
}

static double sim_pressure(long ts, int period_ms) {
    double t = ts * period_ms / 1000.0;
    return round3(5.0 + 1.5 * sin(2 * M_PI * 0.2 * t) + noise(0.05));
}

/* ── 构造 Topic ──────────────────────────────── */
static void build_topic(char *buf, int buf_len) {
    snprintf(buf, buf_len, "/%s/%s/user/trace/data",
             g_product_key, g_device_name);
}

/* ── 构造单帧 JSON ─────────────────────────────
 * 对应 Java 里 frame.put("ts", ...) 等
 * 格式：{"ts":1,"axis1_position":12.345,...}
 */
static int build_frame(char *buf, int buf_len, long ts) {
    return snprintf(buf, buf_len,
        "{\"ts\":%ld"
        ",\"axis1_position\":%.3f"
        ",\"axis1_velocity\":%.3f"
        ",\"axis1_torque\":%.3f"
        ",\"motor_rpm\":%.3f"
        ",\"pressure_bar\":%.3f"
        "}",
        ts,
        sim_axis1_position(ts, g_period_ms),
        sim_axis1_velocity(ts, g_period_ms),
        sim_axis1_torque(ts, g_period_ms),
        sim_motor_rpm(ts, g_period_ms),
        sim_pressure(ts, g_period_ms)
    );
}

/* ── 构造整包 payload JSON ────────────────────
 * 对应 Java 里的 payload JSONObject
 * 格式：{"taskId":"sim_trace_001","seq":1,"period":1,"frames":[...]}
 */
static char *build_payload(long seq, long ts_start, int frame_count) {
    /* 每帧约 120 字节，加上包头约 100 字节 */
    int buf_size = 100 + frame_count * 150;
    char *buf = (char *)malloc(buf_size);
    if (!buf) return NULL;

    int off = 0;
    off += snprintf(buf + off, buf_size - off,
                    "{\"taskId\":\"sim_trace_001\","
                    "\"seq\":%ld,"
                    "\"period\":%d,"
                    "\"frames\":[",
                    seq, g_period_ms);

    for (int i = 0; i < frame_count; i++) {
        long ts = ts_start + i;
        char frame[256];
        build_frame(frame, sizeof(frame), ts);
        off += snprintf(buf + off, buf_size - off,
                        "%s%s", frame, (i < frame_count - 1) ? "," : "");
    }

    off += snprintf(buf + off, buf_size - off, "]}");
    return buf;
}

/* ── 发布单包并重试 ───────────────────────────
 * 对应 Java 的 publishAndWaitAck（4.x 无 ACK 回调，用返回值判断）
 */
static int publish_with_retry(long seq, const char *topic,
                               const char *payload, int payload_len) {
    for (int attempt = 1; attempt <= TRACE_MAX_RETRY && g_running; attempt++) {
        if (attempt > 1) {
            g_retry_count++;
            printf("[trace] retry %d: seq=%ld\n", attempt, seq);
            sleep_ms(500 * attempt);
        }

        int ret = aiot_mqtt_pub(g_mqtt_handle,
                                (char *)topic,
                                (uint8_t *)payload,
                                payload_len,
                                1);   /* QoS 1 */
        if (ret >= 0) {
            g_success_count++;
            return 0;  /* 成功 */
        }

        printf("[trace] publish failed: seq=%ld ret=%d\n", seq, ret);
    }
    return -1;  /* 全部重试失败 */
}

/* ── 发送主循环（对应 Java 的 sendLoop）─────── */
static void *send_loop(void *arg) {
    (void)arg;

    long total_batches = (TRACE_MAX_FRAMES + g_batch_size - 1) / g_batch_size;
    long ts_counter    = 0;
    long frame_sent    = 0;

    char topic[256];
    build_topic(topic, sizeof(topic));

    printf("[trace] start: period=%dms batch=%d maxFrames=%ld topic=%s\n",
           g_period_ms, g_batch_size, (long)TRACE_MAX_FRAMES, topic);

    for (long seq = 1; seq <= total_batches && g_running; seq++) {
        /* 计算本包实际帧数（最后一包可能不满） */
        int frame_count = g_batch_size;
        if (ts_counter + frame_count > TRACE_MAX_FRAMES)
            frame_count = (int)(TRACE_MAX_FRAMES - ts_counter);
        if (frame_count <= 0) break;

        long ts_start = ts_counter + 1;

        /* 构造 payload */
        char *payload = build_payload(seq, ts_start, frame_count);
        if (!payload) {
            printf("[trace] malloc failed at seq=%ld\n", seq);
            break;
        }

        int payload_len = (int)strlen(payload);
        ts_counter  += frame_count;
        frame_sent  += frame_count;

        /* 发布 */
        int ret = publish_with_retry(seq, topic, payload, payload_len);
        free(payload);

        if (ret != 0) {
            printf("[trace] seq=%ld failed after %d retries, abort\n",
                   seq, TRACE_MAX_RETRY);
            break;
        }

        printf("[trace] seq=%ld sent frames=%ld/%ld\n",
               seq, frame_sent, (long)TRACE_MAX_FRAMES);

        /* 包间间隔（对应 Java 的 Thread.sleep(SEND_INTERVAL_MS)）*/
        if (seq < total_batches)
            sleep_ms(TRACE_SEND_INTERVAL_MS);
    }

    g_running = 0;
    printf("[trace] done: success=%ld retries=%ld frames=%ld/%ld\n",
           g_success_count, g_retry_count,
           frame_sent, (long)TRACE_MAX_FRAMES);
    return NULL;
}

/* ── 公开接口 ─────────────────────────────────── */

void trace_simulator_init(void *mqtt_handle,
                          const char *product_key,
                          const char *device_name,
                          int period_ms,
                          int batch_size) {
    g_mqtt_handle = mqtt_handle;
    strncpy(g_product_key, product_key, sizeof(g_product_key) - 1);
    strncpy(g_device_name, device_name, sizeof(g_device_name) - 1);
    g_period_ms   = period_ms  > 0 ? period_ms  : TRACE_PERIOD_MS;
    g_batch_size  = batch_size > 0 ? batch_size : TRACE_BATCH_SIZE;
    g_success_count = 0;
    g_retry_count   = 0;
    srand((unsigned int)time(NULL));
    printf("[trace] initialized\n");
}

void trace_simulator_start(void) {
    if (g_running) {
        printf("[trace] already running\n");
        return;
    }
    g_running = 1;
    /* 对应 Java 的 sendThread = new Thread(this::sendLoop) */
    pthread_create(&g_send_thread, NULL, send_loop, NULL);
}

void trace_simulator_await(void) {
    /* 对应 Java 的 sendThread.join() */
    if (g_send_thread) {
        pthread_join(g_send_thread, NULL);
        g_send_thread = 0;
    }
}

void trace_simulator_stop(void) {
    g_running = 0;
    if (g_send_thread) {
        pthread_join(g_send_thread, NULL);
        g_send_thread = 0;
    }
    printf("[trace] stopped\n");
}
