#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

if [ ! -f "build/iot_ide" ]; then
    echo "❌ build/iot_ide not found, run 'make' first"
    exit 1
fi

echo "[run] Starting iot-ide-c-v4..."
./build/iot_ide
