# iot-ide-c-v4 — C 版上位机（C Link SDK 4.x）

对应 Java 版 `iotkit-mqtt-swj`，使用**阿里云 C Link SDK 4.x**。

相比 3.x 版本的核心变化：
- SDK 以 **zip 源码** 方式提供，直接和业务代码一起编译，**无需预编译 SDK**
- API 从 `IOT_Linkkit_XXX` 换成 `aiot_mqtt_XXX` + `aiot_dm_XXX`
- 不存在 GCC 版本兼容性问题

---

## 完整步骤

```
第一步  安装系统依赖
第二步  下载 SDK（只需一次）
第三步  填写设备配置
第四步  编译
第五步  运行
```

---

## 第一步：安装系统依赖

```bash
sudo apt-get update
sudo apt-get install -y \
    git cmake gcc make \
    libcurl4-openssl-dev \
    libzip-dev \
    libssl-dev \
    unzip curl
```

---

## 第二步：下载 SDK（只需一次）

```bash
cd iot-ide-c-v4
make sdk
```

脚本会从阿里云 OSS 下载 zip 并解压到 `LinkSDK/` 目录。

目录结构：
```
iot-ide-c-v4/
└── LinkSDK/
    ├── core/            ← MQTT 核心
    ├── components/dm/   ← 物模型
    ├── external/        ← mbedtls
    └── portfiles/       ← Linux 适配层
```

---

## 第三步：填写设备配置

编辑 `config/device_config.json`：

```json
{
  "productKey":   "你的ProductKey",
  "deviceName":   "taiyuan-pc-001",
  "deviceSecret": "你的DeviceSecret",
  "mqttHost":     "你的实例ID.mqtt.iothub.aliyuncs.com",
  "mqttPort":     1883
}
```

> **mqttHost** 在阿里云 IoT 控制台的实例详情页里找，格式为
> `iot-xxxxxx.mqtt.iothub.aliyuncs.com`

---

## 第四步：编译

### 方案 A：在 ARM64 上位机上直接编译（推荐）

```bash
cd iot-ide-c-v4
make
# 产物：build/iot_ide
```

### 方案 B：在 x86 开发机上交叉编译

```bash
# 安装交叉编译工具链
sudo apt-get install -y gcc-aarch64-linux-gnu

# 交叉编译
cd iot-ide-c-v4
make cross
# 产物：build-cross/iot_ide

# 上传到上位机
scp build-cross/iot_ide user@上位机IP:/opt/iot-ide/
scp config/device_config.json user@上位机IP:/opt/iot-ide/config/

# 上位机安装运行时库
sudo apt-get install -y libcurl4 libzip4
```

---

## 第五步：运行

### 直接运行
```bash
cd iot-ide-c-v4
./scripts/run.sh
# 或
./build/iot_ide
```

### 后台运行
```bash
nohup ./build/iot_ide > /var/log/iot-ide.log 2>&1 &
tail -f /var/log/iot-ide.log
```

### 注册为 systemd 服务（生产推荐）
```bash
sudo chmod +x scripts/install_service.sh
sudo ./scripts/install_service.sh

# 常用命令
sudo systemctl status  iot-ide
sudo systemctl restart iot-ide
journalctl -u iot-ide -f
```

---

## 与 3.x 版本 API 对照

| 功能 | 3.x API | 4.x API |
|------|---------|---------|
| 初始化 | `IOT_Linkkit_Open` | `aiot_mqtt_init` |
| 连接 | `IOT_Linkkit_Connect` | `aiot_mqtt_connect` |
| 配置参数 | `IOT_Linkkit_Setopt` | `aiot_mqtt_setopt` |
| 驱动消息 | `IOT_Linkkit_Yield` | `aiot_mqtt_process` + `aiot_mqtt_recv` |
| 注册回调 | `IOT_RegisterCallback` | `aiot_mqtt_setopt(AIOT_MQTTOPT_EVENT_HANDLER)` |
| 物模型初始化 | — | `aiot_dm_init` + `aiot_dm_setopt` |
| 属性上报 | `IOT_Linkkit_Report(ITM_MSG_POST_PROPERTY)` | `aiot_dm_send(AIOT_DMMSG_PROPERTY_POST)` |
| 服务响应 | 在回调里写 response 缓冲区 | `aiot_dm_send(AIOT_DMMSG_ASYNC_SERVICE_REPLY)` |
| 断开连接 | `IOT_Linkkit_Disconnect` | `aiot_mqtt_disconnect` |
| 销毁 | `IOT_Linkkit_Close` | `aiot_mqtt_deinit` |

---

## 常见问题

### Q: make sdk 报 curl: command not found
```bash
sudo apt-get install -y curl unzip
```

### Q: 编译报 aiot_dm_api.h not found
LinkSDK 没有下载或目录名不对，检查 `LinkSDK/components/dm/` 是否存在。

### Q: 连接失败
检查 `mqttHost` 是否填写正确，格式为 `iot-xxxxxx.mqtt.iothub.aliyuncs.com`，
在阿里云 IoT 控制台 → 实例详情 → MQTT 接入域名 里找到。
