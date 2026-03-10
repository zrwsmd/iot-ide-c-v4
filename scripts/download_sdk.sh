#!/bin/bash
# 下载并解压 C Link SDK 4.x
# 只需执行一次，产物放到 ./LinkSDK/

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SDK_DIR="$PROJECT_DIR/LinkSDK"

if [ -d "$SDK_DIR" ]; then
    echo "[sdk] LinkSDK 已存在，跳过下载（如需重新下载请先删除 LinkSDK/ 目录）"
    exit 0
fi

echo "[sdk] 下载 C Link SDK 4.x..."
cd "$PROJECT_DIR"
curl -L -o LinkSDK_cn.zip \
    "https://linkkit-export.oss-cn-shanghai.aliyuncs.com/global/LinkSDK_cn.zip"

echo "[sdk] 解压..."
unzip -q LinkSDK_cn.zip
# zip 包解压后可能是 LinkSDK 或 LinkSDK_cn 目录名
if [ -d "LinkSDK_cn" ] && [ ! -d "LinkSDK" ]; then
    mv LinkSDK_cn LinkSDK
fi
rm -f LinkSDK_cn.zip

echo ""
echo "✅ SDK 下载完成：$SDK_DIR"
echo "   接下来执行：make  （本机编译）"
echo "           或：make cross  （交叉编译）"
