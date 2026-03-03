#!/bin/bash

# Android 项目配置验证脚本

echo "=========================================="
echo "Android 项目配置验证"
echo "=========================================="
echo ""

PROJECT_ROOT="/Users/debug/project/YXVodPlayer"
ANDROID_PROJECT="$PROJECT_ROOT/examples/android-test"
ANDROID_THIRD="$PROJECT_ROOT/android-third"

# 检查第三方库
echo "📦 检查第三方库..."
echo "----------------------------------------"

check_arch() {
    local ARCH=$1
    local FFMPEG_DIR="$ANDROID_THIRD/ffmpeg-build-android/FFmpeg-Android/$ARCH"
    local SOUNDTOUCH_DIR="$ANDROID_THIRD/soundtouch-build-android/SoundTouch-Android/$ARCH"
    
    echo "检查 $ARCH 架构:"
    
    # 检查 FFmpeg
    if [ -d "$FFMPEG_DIR/lib" ]; then
        local FFMPEG_LIBS=$(ls "$FFMPEG_DIR/lib"/*.a 2>/dev/null | wc -l)
        if [ $FFMPEG_LIBS -eq 5 ]; then
            echo "  ✅ FFmpeg: 5 个库文件"
        else
            echo "  ⚠️  FFmpeg: 找到 $FFMPEG_LIBS 个库（应该是 5 个）"
        fi
    else
        echo "  ❌ FFmpeg: 目录不存在"
    fi
    
    # 检查 SoundTouch
    if [ -f "$SOUNDTOUCH_DIR/lib/libSoundTouch.a" ]; then
        echo "  ✅ SoundTouch: 库文件存在"
    else
        echo "  ❌ SoundTouch: 库文件不存在"
    fi
    
    echo ""
}

check_arch "arm64-v8a"
check_arch "armeabi-v7a"
check_arch "x86_64"

# 检查 Android 项目文件
echo "📱 检查 Android 项目..."
echo "----------------------------------------"

if [ -f "$ANDROID_PROJECT/app/build.gradle" ]; then
    echo "✅ build.gradle 存在"
else
    echo "❌ build.gradle 不存在"
fi

if [ -f "$ANDROID_PROJECT/app/src/main/cpp/CMakeLists.txt" ]; then
    echo "✅ CMakeLists.txt 存在"
else
    echo "❌ CMakeLists.txt 不存在"
fi

if [ -f "$ANDROID_PROJECT/app/src/main/java/com/hxcplayer/test/MainActivity.kt" ]; then
    echo "✅ MainActivity.kt 存在"
else
    echo "❌ MainActivity.kt 不存在"
fi

if [ -f "$ANDROID_PROJECT/app/src/main/cpp/hxcplayer_jni.cpp" ]; then
    echo "✅ JNI 桥接文件存在"
else
    echo "❌ JNI 桥接文件不存在"
fi

echo ""

# 检查 core 源文件
echo "🔧 检查 Core 源文件..."
echo "----------------------------------------"

CORE_FILES=(
    "hxc_player_core.cpp"
    "hxc_decoder.cpp"
    "hxc_player_types.cpp"
    "hxc_packet_queue.cpp"
    "hxc_player_core_c_bridge.cpp"
)

for file in "${CORE_FILES[@]}"; do
    if [ -f "$PROJECT_ROOT/core/src/$file" ]; then
        echo "✅ $file"
    else
        echo "❌ $file"
    fi
done

echo ""

# 检查头文件
echo "📄 检查头文件..."
echo "----------------------------------------"

if [ -d "$PROJECT_ROOT/core/include" ]; then
    HEADER_COUNT=$(find "$PROJECT_ROOT/core/include" -name "*.h" | wc -l)
    echo "✅ Core 头文件: $HEADER_COUNT 个"
else
    echo "❌ Core include 目录不存在"
fi

if [ -d "$ANDROID_THIRD/ffmpeg-build-android/FFmpeg-Android/arm64-v8a/include" ]; then
    echo "✅ FFmpeg 头文件存在"
else
    echo "❌ FFmpeg 头文件不存在"
fi

if [ -d "$ANDROID_THIRD/soundtouch-build-android/SoundTouch-Android/arm64-v8a/include" ]; then
    echo "✅ SoundTouch 头文件存在"
else
    echo "❌ SoundTouch 头文件不存在"
fi

echo ""

# 检查 Gradle wrapper
echo "⚙️  检查 Gradle..."
echo "----------------------------------------"

if [ -f "$ANDROID_PROJECT/gradlew" ]; then
    echo "✅ Gradle wrapper 存在"
    if [ -x "$ANDROID_PROJECT/gradlew" ]; then
        echo "✅ Gradle wrapper 可执行"
    else
        echo "⚠️  Gradle wrapper 不可执行（需要 chmod +x）"
    fi
else
    echo "❌ Gradle wrapper 不存在"
fi

echo ""

# 库文件大小统计
echo "📊 库文件大小统计..."
echo "----------------------------------------"

for ARCH in "arm64-v8a" "armeabi-v7a" "x86_64"; do
    FFMPEG_SIZE=$(du -sh "$ANDROID_THIRD/ffmpeg-build-android/FFmpeg-Android/$ARCH/lib" 2>/dev/null | cut -f1)
    SOUNDTOUCH_SIZE=$(du -sh "$ANDROID_THIRD/soundtouch-build-android/SoundTouch-Android/$ARCH/lib" 2>/dev/null | cut -f1)
    
    echo "$ARCH:"
    echo "  FFmpeg: $FFMPEG_SIZE"
    echo "  SoundTouch: $SOUNDTOUCH_SIZE"
done

echo ""

# 总结
echo "=========================================="
echo "✅ 配置验证完成"
echo "=========================================="
echo ""

FFMPEG_OK=$(ls "$ANDROID_THIRD/ffmpeg-build-android/FFmpeg-Android/arm64-v8a/lib"/*.a 2>/dev/null | wc -l)
SOUNDTOUCH_OK=$(ls "$ANDROID_THIRD/soundtouch-build-android/SoundTouch-Android/arm64-v8a/lib"/*.a 2>/dev/null | wc -l)

if [ $FFMPEG_OK -eq 5 ] && [ $SOUNDTOUCH_OK -eq 1 ]; then
    echo "✅ 所有库已准备就绪，可以在 Android Studio 中打开项目！"
    echo ""
    echo "下一步："
    echo "  1. 在 Android Studio 中打开: $ANDROID_PROJECT"
    echo "  2. 等待 Gradle 同步完成"
    echo "  3. Build → Make Project"
    echo "  4. 连接设备或启动模拟器"
    echo "  5. Run → Run 'app'"
else
    echo "⚠️  第三方库不完整，请确保 FFmpeg 和 SoundTouch 编译成功"
fi

echo ""
