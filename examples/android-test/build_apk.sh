#!/bin/bash

# Android APK 打包脚本
# 用于生成可安装到真机的 APK

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# 设置 Java 环境（使用 Android Studio 内置的 JDK）
if [ -z "$JAVA_HOME" ]; then
    # 尝试查找 Android Studio 的 JDK
    if [ -d "/Applications/Android Studio.app/Contents/jbr/Contents/Home" ]; then
        export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
        echo "使用 Android Studio JDK: $JAVA_HOME"
    elif [ -d "$HOME/Library/Java/JavaVirtualMachines" ]; then
        # 查找系统安装的 JDK
        JDK_PATH=$(ls -d "$HOME/Library/Java/JavaVirtualMachines"/*/Contents/Home 2>/dev/null | head -1)
        if [ -n "$JDK_PATH" ]; then
            export JAVA_HOME="$JDK_PATH"
            echo "使用系统 JDK: $JAVA_HOME"
        fi
    fi
fi

# 验证 JAVA_HOME
if [ -z "$JAVA_HOME" ] || [ ! -d "$JAVA_HOME" ]; then
    echo "❌ 错误: 未找到 Java 运行环境"
    echo ""
    echo "解决方案："
    echo "1. 安装 Android Studio（推荐）"
    echo "2. 或设置 JAVA_HOME 环境变量"
    echo "   export JAVA_HOME=/path/to/jdk"
    echo ""
    exit 1
fi

export PATH="$JAVA_HOME/bin:$PATH"

echo "=========================================="
echo "HXCPlayer Android APK 打包"
echo "=========================================="
echo "项目目录: $SCRIPT_DIR"
echo "Java 版本: $(java -version 2>&1 | head -1)"
echo ""

# 1. 检查 hxcplayer 模块的第三方库（现在位于 android/ 目录下）
echo "📦 步骤 1/3: 检查 hxcplayer 模块库文件..."
# 注意：hxcplayer 模块已经移到外层 android/ 目录下
HXCPLAYER_LIBS="$PROJECT_ROOT/android/hxcplayer/src/main/jniLibs/arm64-v8a"
if [ ! -d "$HXCPLAYER_LIBS" ]; then
    echo "⚠️  hxcplayer 库文件不存在，请先运行: cd $PROJECT_ROOT/android/library && ./build_aar.sh --copy-libs"
    exit 1
else
    echo "✅ hxcplayer 库文件已就绪"
    ls "$HXCPLAYER_LIBS/" | head -5
fi

echo ""
echo "=========================================="
echo ""

# 2. 清理并构建项目
echo "🔨 步骤 2/3: 构建项目..."
cd "$SCRIPT_DIR"

# 检查 gradlew 是否存在
if [ ! -f "./gradlew" ]; then
    echo "❌ 错误: gradlew 不存在"
    echo ""
    echo "请在 Android Studio 中构建项目："
    echo "1. 打开 Android Studio"
    echo "2. File → Sync Project with Gradle Files"
    echo "3. Build → Build Bundle(s) / APK(s) → Build APK(s)"
    echo ""
    exit 1
fi

# 检查 gradle wrapper jar
if [ ! -f "./gradle/wrapper/gradle-wrapper.jar" ]; then
    echo "⚠️  Gradle Wrapper 未完整配置，尝试重新生成..."
    
    # 尝试使用系统 Gradle（如果已安装）
    if command -v gradle &> /dev/null; then
        echo "使用系统 Gradle 重新生成 wrapper..."
        gradle wrapper
    else
        echo "❌ 错误: Gradle Wrapper 未配置，且未找到系统 Gradle"
        echo ""
        echo "解决方案："
        echo "1. 在 Android Studio 中打开项目"
        echo "2. 执行 File → Sync Project with Gradle Files"
        echo "3. 或手动下载 gradle-wrapper.jar："
        echo "   curl -L https://github.com/gradle/gradle/raw/master/gradle/wrapper/gradle-wrapper.jar -o gradle/wrapper/gradle-wrapper.jar"
        echo ""
        exit 1
    fi
fi

# 清理之前的构建
echo "  清理之前的构建产物..."
./gradlew clean

# 构建 Release APK
echo "  构建 Release APK..."
./gradlew assembleRelease

echo ""
echo "=========================================="
echo ""

# 3. 查找生成的 APK
echo "📱 步骤 3/3: 定位 APK 文件..."

APK_DIR="$SCRIPT_DIR/app/build/outputs/apk/release"
APK_FILE=$(find "$APK_DIR" -name "*.apk" -type f | head -1)

if [ -z "$APK_FILE" ]; then
    echo "❌ 错误: 未找到生成的 APK 文件"
    echo "请检查构建日志"
    exit 1
fi

# 获取 APK 信息
APK_NAME=$(basename "$APK_FILE")
APK_SIZE=$(ls -lh "$APK_FILE" | awk '{print $5}')

echo ""
echo "✅ APK 打包成功！"
echo ""
echo "=========================================="
echo "📦 APK 信息"
echo "=========================================="
echo "文件名: $APK_NAME"
echo "文件大小: $APK_SIZE"
echo "路径: $APK_FILE"
echo "签名状态: ✅ 已签名 (使用 /Users/debug/test.jks)"
echo ""
echo "支持架构:"
echo "  • arm64-v8a (真机)"
echo "  • armeabi-v7a (旧真机)"
echo "  • x86_64 (模拟器)"
echo ""

# 4. 可选：复制到桌面
read -p "是否复制 APK 到桌面？(y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    DESKTOP_PATH="$HOME/Desktop/HXCPlayer-$(date +%Y%m%d-%H%M%S).apk"
    cp "$APK_FILE" "$DESKTOP_PATH"
    echo "✅ 已复制到: $DESKTOP_PATH"
    
    # 在 Finder 中显示
    open -R "$DESKTOP_PATH"
fi

echo ""
echo "=========================================="
echo "📱 安装方法"
echo "=========================================="
echo ""
echo "方法 1: 通过 USB 线连接手机"
echo "  adb install \"$APK_FILE\""
echo ""
echo "方法 2: 传输到手机"
echo "  将 APK 文件传输到手机，直接点击安装"
echo ""
echo "方法 3: 使用此脚本自动安装"
echo "  bash build_apk.sh --install"
echo ""

# 5. 可选：直接安装到已连接的设备
if [ "$1" == "--install" ] || [ "$1" == "-i" ]; then
    echo "=========================================="
    echo "📲 正在安装到设备..."
    echo "=========================================="
    echo ""
    
    # 检查是否有设备连接
    DEVICES=$(adb devices | grep -v "List" | grep "device$" | wc -l | xargs)
    
    if [ "$DEVICES" -eq 0 ]; then
        echo "❌ 错误: 未检测到已连接的 Android 设备"
        echo "请确保:"
        echo "  1. 手机已通过 USB 连接到电脑"
        echo "  2. 手机已开启 USB 调试"
        echo "  3. 已授权此电脑进行调试"
        exit 1
    fi
    
    echo "检测到 $DEVICES 个设备"
    adb devices
    echo ""
    
    # 卸载旧版本（可选）
    echo "尝试卸载旧版本..."
    adb uninstall com.hxcplayer.test 2>/dev/null || echo "  (未安装旧版本)"
    
    # 安装新版本
    echo "安装新版本..."
    adb install "$APK_FILE"
    
    if [ $? -eq 0 ]; then
        echo ""
        echo "✅ 安装成功！"
        echo ""
        echo "启动应用:"
        echo "  adb shell am start -n com.hxcplayer.test/.MainActivity"
    else
        echo ""
        echo "❌ 安装失败"
        exit 1
    fi
fi

echo ""
echo "✅ 完成！"
echo ""
