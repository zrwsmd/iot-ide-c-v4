#!/bin/bash
# 注册为 systemd 服务（在 ARM64 上位机上执行）
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

SERVICE_FILE="/etc/systemd/system/iot-ide.service"

cat > "$SERVICE_FILE" << EOF
[Unit]
Description=IoT IDE C v4 Device Service
After=network.target

[Service]
Type=simple
WorkingDirectory=$PROJECT_DIR
ExecStart=$PROJECT_DIR/build/iot_ide
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable iot-ide
systemctl start  iot-ide

echo "✅ 服务已安装并启动"
echo "   查看状态：sudo systemctl status iot-ide"
echo "   查看日志：journalctl -u iot-ide -f"
