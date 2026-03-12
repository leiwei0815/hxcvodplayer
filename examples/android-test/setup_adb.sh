#!/bin/bash

# Android SDK 环境配置脚本
# 用于配置 adb 和 Android SDK 工具路径

echo "======================================"
echo "Android SDK 环境配置"
echo "======================================"
echo ""

# Android SDK 路径
ANDROID_SDK="$HOME/Library/Android/sdk"

# 检查 SDK 是否存在
if [ ! -d "$ANDROID_SDK" ]; then
    echo "❌ 未找到 Android SDK"
    echo "请先安装 Android Studio"
    exit 1
fi

echo "✅ 找到 Android SDK: $ANDROID_SDK"
echo ""

# 检查 adb 是否存在
if [ -f "$ANDROID_SDK/platform-tools/adb" ]; then
    echo "✅ adb 已存在: $ANDROID_SDK/platform-tools/adb"
else
    echo "❌ 未找到 adb"
    exit 1
fi
echo ""

# 检测 Shell 类型
SHELL_TYPE=$(basename "$SHELL")
echo "检测到 Shell 类型: $SHELL_TYPE"
echo ""

# 根据 Shell 类型选择配置文件
case "$SHELL_TYPE" in
    "zsh")
        CONFIG_FILE="$HOME/.zshrc"
        ;;
    "bash")
        CONFIG_FILE="$HOME/.bash_profile"
        if [ ! -f "$CONFIG_FILE" ]; then
            CONFIG_FILE="$HOME/.bashrc"
        fi
        ;;
    *)
        echo "⚠️  未识别的 Shell: $SHELL_TYPE"
        CONFIG_FILE="$HOME/.profile"
        ;;
esac

echo "配置文件: $CONFIG_FILE"
echo ""

# 检查是否已配置
if grep -q "ANDROID_HOME" "$CONFIG_FILE" 2>/dev/null; then
    echo "⚠️  检测到已有 ANDROID 配置"
    echo ""
    grep "ANDROID" "$CONFIG_FILE"
    echo ""
    read -p "是否覆盖？(y/n): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "已取消"
        exit 0
    fi
    
    # 备份配置文件
    cp "$CONFIG_FILE" "${CONFIG_FILE}.backup.$(date +%Y%m%d_%H%M%S)"
    echo "✅ 已备份配置文件"
    
    # 删除旧配置
    sed -i '' '/ANDROID_HOME/d' "$CONFIG_FILE"
    sed -i '' '/ANDROID_SDK/d' "$CONFIG_FILE"
    sed -i '' '/platform-tools/d' "$CONFIG_FILE"
fi

# 添加配置
echo "" >> "$CONFIG_FILE"
echo "# Android SDK - 由脚本自动添加 $(date +%Y-%m-%d)" >> "$CONFIG_FILE"
echo "export ANDROID_HOME=\"$ANDROID_SDK\"" >> "$CONFIG_FILE"
echo "export ANDROID_SDK=\"$ANDROID_SDK\"" >> "$CONFIG_FILE"
echo "export PATH=\"\$PATH:\$ANDROID_SDK/platform-tools:\$ANDROID_SDK/tools\"" >> "$CONFIG_FILE"

echo "✅ 已添加配置到 $CONFIG_FILE"
echo ""

echo "======================================"
echo "配置完成！"
echo "======================================"
echo ""
echo "请运行以下命令使配置生效："
echo ""

if [ "$SHELL_TYPE" = "zsh" ]; then
    echo "  source ~/.zshrc"
else
    echo "  source $CONFIG_FILE"
fi

echo ""
echo "或者重新打开终端窗口"
echo ""
echo "然后测试："
echo "  adb --version"
echo ""
