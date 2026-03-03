#!/bin/bash

echo "======================================"
echo "HXCPlayer 崩溃调试脚本"
echo "======================================"
echo ""

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# 配置 adb 路径
if ! command -v adb &> /dev/null; then
    echo "⚠️  adb 不在 PATH 中，尝试使用 Android SDK 路径..."
    if [ -f "$HOME/Library/Android/sdk/platform-tools/adb" ]; then
        export PATH="$PATH:$HOME/Library/Android/sdk/platform-tools"
        export ANDROID_HOME="$HOME/Library/Android/sdk"
        echo "✅ 已找到 adb: $HOME/Library/Android/sdk/platform-tools/adb"
    else
        echo "❌ 未找到 adb"
        echo ""
        echo "请运行配置脚本："
        echo "  ./setup_adb.sh"
        echo "  source ~/.zshrc"
        echo ""
        exit 1
    fi
fi
echo ""

# 检查设备连接
echo "📱 检查设备连接..."
DEVICE_COUNT=$(adb devices | grep -c "device$")

if [ "$DEVICE_COUNT" -eq 0 ]; then
    echo "❌ 未检测到设备，请连接手机并启用 USB 调试"
    echo ""
    echo "如何启用 USB 调试："
    echo "  1. 打开手机「设置」"
    echo "  2. 进入「关于手机」"
    echo "  3. 连续点击「版本号」7 次，开启开发者模式"
    echo "  4. 返回「设置」→「开发者选项」"
    echo "  5. 启用「USB 调试」"
    echo ""
    exit 1
fi

# 如果有多个设备，让用户选择
DEVICE_SERIAL=""
if [ "$DEVICE_COUNT" -gt 1 ]; then
    echo "检测到多个设备："
    echo ""
    adb devices -l
    echo ""
    
    # 提取设备列表
    DEVICES=($(adb devices | grep "device$" | awk '{print $1}'))
    
    echo "请选择要测试的设备："
    for i in "${!DEVICES[@]}"; do
        DEVICE_ID="${DEVICES[$i]}"
        if [[ "$DEVICE_ID" == emulator-* ]]; then
            echo "  $((i+1)). $DEVICE_ID (模拟器)"
        else
            MODEL=$(adb -s "$DEVICE_ID" shell getprop ro.product.model 2>/dev/null | tr -d '\r')
            echo "  $((i+1)). $DEVICE_ID ($MODEL)"
        fi
    done
    echo ""
    
    read -p "请输入序号 (1-${#DEVICES[@]}): " choice
    
    if [ "$choice" -ge 1 ] && [ "$choice" -le "${#DEVICES[@]}" ]; then
        DEVICE_SERIAL="${DEVICES[$((choice-1))]}"
        echo "✅ 已选择设备: $DEVICE_SERIAL"
    else
        echo "❌ 无效的选择"
        exit 1
    fi
else
    DEVICE_SERIAL=$(adb devices | grep "device$" | awk '{print $1}' | head -1)
    echo "✅ 设备已连接: $DEVICE_SERIAL"
fi
echo ""

# 获取设备信息
echo "======================================"
echo "📱 设备信息"
echo "======================================"
echo "设备 ID: $DEVICE_SERIAL"
echo "型号: $(adb -s "$DEVICE_SERIAL" shell getprop ro.product.model)"
echo "Android 版本: $(adb -s "$DEVICE_SERIAL" shell getprop ro.build.version.release)"
echo "SDK 版本: $(adb -s "$DEVICE_SERIAL" shell getprop ro.build.version.sdk)"
echo "架构: $(adb -s "$DEVICE_SERIAL" shell getprop ro.product.cpu.abi)"
echo ""

# 清除旧日志
echo "🧹 清除旧日志..."
adb -s "$DEVICE_SERIAL" logcat -c
echo "✅ 日志已清除"
echo ""

# 创建日志文件
LOG_FILE="$HOME/Desktop/hxcplayer_crash_$(date +%Y%m%d_%H%M%S).log"
echo "📝 日志文件: $LOG_FILE"
echo ""

# 卸载旧版本
echo "🗑️  卸载旧版本..."
adb -s "$DEVICE_SERIAL" uninstall com.hxcplayer.test 2>/dev/null
echo ""

# 检查并构建 Debug APK
APK_PATH="app/build/outputs/apk/debug/app-debug.apk"
if [ ! -f "$APK_PATH" ]; then
    echo "⚠️  Debug APK 不存在，开始构建..."
    echo ""
    
    # 设置 JAVA_HOME
    if [ -z "$JAVA_HOME" ]; then
        if [ -d "/Applications/Android Studio.app/Contents/jbr/Contents/Home" ]; then
            export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
        fi
    fi
    export PATH="$JAVA_HOME/bin:$PATH"
    
    # 构建 Debug APK
    ./gradlew assembleDebug
    
    if [ ! -f "$APK_PATH" ]; then
        echo "❌ Debug APK 构建失败"
        exit 1
    fi
    echo "✅ Debug APK 构建完成"
    echo ""
fi

# 安装新版本
echo "📦 安装 Debug 版本..."
adb -s "$DEVICE_SERIAL" install "$APK_PATH"
if [ $? -ne 0 ]; then
    echo "❌ 安装失败"
    exit 1
fi
echo "✅ 安装完成"
echo ""

# 启动日志监控
echo "======================================"
echo "🔍 开始监控日志..."
echo "======================================"
echo ""
echo "📋 测试清单（请按顺序测试）："
echo ""
echo "  ✅ 基础功能："
echo "    1. 启动应用"
echo "    2. 检查界面显示"
echo "    3. 点击「打开」按钮"
echo ""
echo "  ✅ 播放功能："
echo "    4. 输入视频 URL"
echo "    5. 点击「播放」"
echo "    6. 观察视频和音频"
echo ""
echo "  ✅ 控制功能："
echo "    7. 拖动进度条"
echo "    8. 调整倍速（0.5x, 1.0x, 2.0x）"
echo "    9. 调整音量"
echo "   10. 切换画面比例"
echo ""
echo "  ✅ 边界测试："
echo "   11. 快速切换播放/暂停"
echo "   12. 旋转屏幕"
echo "   13. 切换到后台再返回"
echo ""
echo "======================================"
echo "按 Ctrl+C 停止监控并分析日志"
echo "======================================"
echo ""

# 等待 2 秒让用户看到提示
sleep 2

# 实时监控并保存日志
# 使用 --line-buffered 确保实时输出
adb -s "$DEVICE_SERIAL" logcat | tee "$LOG_FILE" | grep --line-buffered -E "(MainActivity|HXCPlayer|PlayerCore|FATAL|AndroidRuntime|ERROR|libhxcplayer|UnsatisfiedLinkError|NullPointerException)"

echo ""
echo "======================================"
echo "📝 日志已保存到: $LOG_FILE"
echo "======================================"
echo ""

# 分析崩溃
echo "🔍 分析日志..."
echo ""

if grep -q "FATAL EXCEPTION" "$LOG_FILE"; then
    echo "❌ 检测到应用崩溃！"
    echo ""
    echo "======================================"
    echo "崩溃堆栈："
    echo "======================================"
    grep -A 40 "FATAL EXCEPTION" "$LOG_FILE" | head -50
    echo ""
    echo "======================================"
    echo "崩溃原因分析："
    echo "======================================"
    
    if grep -q "UnsatisfiedLinkError" "$LOG_FILE"; then
        echo "🔴 Native 库加载失败"
        echo "   → 检查 .so 文件是否正确打包"
        echo "   → 检查库依赖关系"
    fi
    
    if grep -q "NullPointerException" "$LOG_FILE"; then
        echo "🔴 空指针异常"
        echo "   → 检查 onCreate 中的视图初始化"
        echo "   → 检查 binding 是否正确"
    fi
    
    if grep -q "signal 11\|SIGSEGV" "$LOG_FILE"; then
        echo "🔴 Native 崩溃（内存访问错误）"
        echo "   → 检查 JNI 调用"
        echo "   → 检查 Native 代码内存访问"
    fi
    
    if grep -q "AppOps\|CONTROL_AUDIO" "$LOG_FILE"; then
        echo "⚠️  权限问题（AppOps）"
        echo "   → Android 12+ 音频权限限制"
        echo "   → 已通过降低 targetSdk 到 29 解决"
    fi
    
    echo ""
else
    echo "✅ 未检测到崩溃"
    echo ""
    
    # 检查是否有错误日志
    if grep -q "ERROR" "$LOG_FILE"; then
        echo "⚠️  检测到错误日志（非崩溃）："
        echo ""
        grep "ERROR" "$LOG_FILE" | tail -10
        echo ""
    else
        echo "✅ 应用运行正常，无错误"
    fi
fi

echo "======================================"
echo "📊 日志统计"
echo "======================================"
echo "总行数: $(wc -l < "$LOG_FILE")"
echo "错误数: $(grep -c "ERROR" "$LOG_FILE" 2>/dev/null || echo 0)"
echo "警告数: $(grep -c "WARN" "$LOG_FILE" 2>/dev/null || echo 0)"
echo ""

echo "======================================"
echo "💡 下一步"
echo "======================================"
echo ""
if grep -q "FATAL EXCEPTION" "$LOG_FILE"; then
    echo "1. 查看完整日志: cat $LOG_FILE"
    echo "2. 搜索崩溃: grep -A 50 'FATAL EXCEPTION' $LOG_FILE"
    echo "3. 根据崩溃信息修复代码"
    echo "4. 重新测试: ./test_crash.sh"
else
    echo "✅ 应用运行正常！"
    echo ""
    echo "如果发现其他问题，可以："
    echo "1. 查看完整日志: cat $LOG_FILE"
    echo "2. 搜索错误: grep 'ERROR' $LOG_FILE"
    echo "3. 搜索警告: grep 'WARN' $LOG_FILE"
fi
echo ""
