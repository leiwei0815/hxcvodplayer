#!/bin/bash

# Android NDK 手动安装脚本
# 适用于 Android Studio 下载失败的情况

set -e

echo "=========================================="
echo "Android NDK 手动安装脚本"
echo "=========================================="
echo ""

# NDK 版本（可以修改为其他版本）
NDK_VERSION="r25c"
NDK_VERSION_NUM="25.2.9519653"
NDK_FILENAME="android-ndk-${NDK_VERSION}-darwin.zip"
NDK_URL="https://dl.google.com/android/repository/${NDK_FILENAME}"

# 目标目录
SDK_DIR="$HOME/Library/Android/sdk"
NDK_DIR="$SDK_DIR/ndk"
INSTALL_DIR="$NDK_DIR/$NDK_VERSION_NUM"
DOWNLOAD_DIR="$HOME/Downloads"

echo "📦 配置信息:"
echo "  NDK 版本: $NDK_VERSION ($NDK_VERSION_NUM)"
echo "  下载 URL: $NDK_URL"
echo "  安装目录: $INSTALL_DIR"
echo ""

# 检查是否已安装
if [ -d "$INSTALL_DIR" ]; then
    echo "⚠️  NDK $NDK_VERSION_NUM 已经安装在:"
    echo "   $INSTALL_DIR"
    echo ""
    read -p "是否要重新安装? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "跳过安装。"
        exit 0
    fi
    echo "正在删除旧版本..."
    rm -rf "$INSTALL_DIR"
fi

# 创建目录
mkdir -p "$NDK_DIR"
cd "$DOWNLOAD_DIR"

# 检查是否已下载
if [ -f "$NDK_FILENAME" ]; then
    echo "✅ 发现已下载的文件: $NDK_FILENAME"
    echo ""
    read -p "是否使用已下载的文件? (Y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Nn]$ ]]; then
        echo "正在删除旧文件..."
        rm -f "$NDK_FILENAME"
    else
        echo "使用已下载的文件。"
    fi
fi

# 下载 NDK
if [ ! -f "$NDK_FILENAME" ]; then
    echo ""
    echo "📥 开始下载 NDK $NDK_VERSION..."
    echo "   文件大小: 约 1 GB"
    echo "   下载位置: $DOWNLOAD_DIR/$NDK_FILENAME"
    echo ""
    echo "提示: 如果下载速度很慢，可以:"
    echo "  1. 使用代理或 VPN"
    echo "  2. 使用浏览器下载（复制上面的 URL）"
    echo "  3. 使用国内镜像（见文档）"
    echo ""
    
    # 使用 curl 下载（支持断点续传）
    curl -L -C - --progress-bar -o "$NDK_FILENAME" "$NDK_URL"
    
    if [ $? -ne 0 ]; then
        echo ""
        echo "❌ 下载失败！"
        echo ""
        echo "请尝试:"
        echo "  1. 检查网络连接"
        echo "  2. 使用浏览器手动下载: $NDK_URL"
        echo "  3. 下载后将文件保存到: $DOWNLOAD_DIR/$NDK_FILENAME"
        echo "  4. 重新运行此脚本"
        exit 1
    fi
fi

# 验证文件
echo ""
echo "📊 验证下载的文件..."
FILE_SIZE=$(stat -f%z "$NDK_FILENAME" 2>/dev/null || stat -c%s "$NDK_FILENAME")
FILE_SIZE_MB=$((FILE_SIZE / 1024 / 1024))
echo "   文件大小: ${FILE_SIZE_MB} MB"

if [ $FILE_SIZE_MB -lt 800 ]; then
    echo ""
    echo "⚠️  警告: 文件大小异常 (${FILE_SIZE_MB} MB < 800 MB)"
    echo "   文件可能下载不完整或损坏"
    echo ""
    read -p "是否继续安装? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "已取消安装。请重新下载文件。"
        rm -f "$NDK_FILENAME"
        exit 1
    fi
fi

# 解压
echo ""
echo "📦 正在解压 NDK..."
echo "   目标目录: $NDK_DIR"
echo "   这可能需要几分钟..."
echo ""

unzip -q "$NDK_FILENAME" -d "$NDK_DIR"

if [ $? -ne 0 ]; then
    echo ""
    echo "❌ 解压失败！"
    echo ""
    echo "可能的原因:"
    echo "  1. ZIP 文件损坏（重新下载）"
    echo "  2. 磁盘空间不足（需要约 3-4 GB）"
    exit 1
fi

# 重命名
echo "📝 重命名目录..."
EXTRACTED_DIR="$NDK_DIR/android-ndk-$NDK_VERSION"
if [ -d "$EXTRACTED_DIR" ]; then
    mv "$EXTRACTED_DIR" "$INSTALL_DIR"
else
    echo "⚠️  未找到解压的目录: $EXTRACTED_DIR"
    ls -la "$NDK_DIR"
    exit 1
fi

# 验证安装
echo ""
echo "✅ 验证安装..."
if [ -f "$INSTALL_DIR/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang" ]; then
    echo "   ✅ NDK 安装成功！"
else
    echo "   ❌ 安装验证失败"
    exit 1
fi

# 设置环境变量
echo ""
echo "📝 设置环境变量..."
SHELL_RC="$HOME/.zshrc"
if [ ! -f "$SHELL_RC" ]; then
    SHELL_RC="$HOME/.bash_profile"
fi

ENV_LINE="export ANDROID_NDK=$INSTALL_DIR"
if grep -q "ANDROID_NDK=" "$SHELL_RC" 2>/dev/null; then
    echo "   环境变量已存在于 $SHELL_RC"
else
    echo "$ENV_LINE" >> "$SHELL_RC"
    echo "   已添加到 $SHELL_RC"
fi

# 临时设置
export ANDROID_NDK="$INSTALL_DIR"

echo ""
echo "=========================================="
echo "✅ NDK 安装完成！"
echo "=========================================="
echo ""
echo "📍 安装位置:"
echo "   $INSTALL_DIR"
echo ""
echo "📝 环境变量:"
echo "   ANDROID_NDK=$INSTALL_DIR"
echo ""
echo "🚀 下一步:"
echo "   1. 重新打开终端，或运行:"
echo "      source $SHELL_RC"
echo ""
echo "   2. 验证安装:"
echo "      echo \$ANDROID_NDK"
echo ""
echo "   3. 开始编译 FFmpeg:"
echo "      cd /Users/debug/project/YXVodPlayer/android-third"
echo "      ./build_all.sh"
echo ""

# 询问是否删除下载文件
echo ""
read -p "是否删除下载的 ZIP 文件 (${FILE_SIZE_MB} MB)? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -f "$DOWNLOAD_DIR/$NDK_FILENAME"
    echo "已删除: $NDK_FILENAME"
fi

echo ""
echo "🎉 完成！"
