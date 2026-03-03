#!/bin/bash

# 快速验证 Library 模块配置是否正确

echo "=========================================="
echo "HXCPlayer Library 模块配置验证"
echo "=========================================="
echo ""

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 1. 检查目录结构
echo "📁 检查目录结构..."

check_file() {
    if [ -f "$1" ]; then
        echo "  ✅ $1"
    else
        echo "  ❌ $1 (缺失)"
        return 1
    fi
}

check_dir() {
    if [ -d "$1" ]; then
        echo "  ✅ $1/"
    else
        echo "  ❌ $1/ (缺失)"
        return 1
    fi
}

# 检查核心文件
check_file "$SCRIPT_DIR/hxcplayer/build.gradle"
check_file "$SCRIPT_DIR/hxcplayer/src/main/AndroidManifest.xml"
check_file "$SCRIPT_DIR/hxcplayer/src/main/java/com/hxcplayer/HXCPlayerControl.kt"
check_file "$SCRIPT_DIR/hxcplayer/src/main/cpp/CMakeLists.txt"
check_file "$SCRIPT_DIR/hxcplayer/src/main/cpp/hxcplayer_jni.cpp"
check_file "$SCRIPT_DIR/hxcplayer/src/main/cpp/android_player.cpp"
check_file "$SCRIPT_DIR/settings.gradle"

echo ""

# 2. 检查 .so 库
echo "📚 检查 Native 库..."

for ABI in "arm64-v8a" "armeabi-v7a" "x86_64"; do
    JNI_DIR="$SCRIPT_DIR/hxcplayer/src/main/jniLibs/$ABI"
    if [ -d "$JNI_DIR" ]; then
        SO_COUNT=$(ls "$JNI_DIR"/*.so 2>/dev/null | wc -l | xargs)
        if [ "$SO_COUNT" -gt 0 ]; then
            echo "  ✅ $ABI: $SO_COUNT 个 .so 文件"
        else
            echo "  ⚠️  $ABI: 没有 .so 文件（需要运行 copy_libs.sh）"
        fi
    else
        echo "  ⚠️  $ABI: 目录不存在"
    fi
done

echo ""

# 3. 检查配置文件内容
echo "⚙️  检查配置..."

# 检查 settings.gradle
if grep -q "include ':hxcplayer'" "$SCRIPT_DIR/settings.gradle"; then
    echo "  ✅ settings.gradle 包含 hxcplayer 模块"
else
    echo "  ❌ settings.gradle 未包含 hxcplayer 模块"
fi

# 检查 app/build.gradle
if grep -q "implementation project(':hxcplayer')" "$SCRIPT_DIR/app/build.gradle"; then
    echo "  ✅ app 依赖 hxcplayer 模块"
else
    echo "  ❌ app 未依赖 hxcplayer 模块"
fi

# 检查包名
if grep -q "package com.hxcplayer" "$SCRIPT_DIR/hxcplayer/src/main/java/com/hxcplayer/HXCPlayerControl.kt"; then
    echo "  ✅ HXCPlayerControl 包名正确"
else
    echo "  ❌ HXCPlayerControl 包名错误"
fi

# 检查 JNI 包名
if grep -q "Java_com_hxcplayer_HXCPlayerControl" "$SCRIPT_DIR/hxcplayer/src/main/cpp/hxcplayer_jni.cpp"; then
    echo "  ✅ JNI 方法包名正确"
else
    echo "  ❌ JNI 方法包名错误"
fi

echo ""

# 4. 检查 MainActivity 导入
if grep -q "import com.hxcplayer.HXCPlayerControl" "$SCRIPT_DIR/app/src/main/java/com/hxcplayer/test/MainActivity.kt"; then
    echo "  ✅ MainActivity 导入正确"
else
    echo "  ❌ MainActivity 导入错误"
fi

echo ""

# 5. 总结
echo "=========================================="
echo "验证完成！"
echo "=========================================="
echo ""
echo "下一步："
echo "1. 在 Android Studio 中打开项目"
echo "2. File → Sync Project with Gradle Files"
echo "3. 构建测试："
echo "   - 运行 app: ▶️ Run 'app'"
echo "   - 打包 AAR: Gradle → hxcplayer → Tasks → build → assembleRelease"
echo ""
echo "详细测试指南："
echo "  cat TESTING_GUIDE.md"
echo ""
