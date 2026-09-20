#!/bin/bash
# 供 android-third/build_*.sh source，统一探测 NDK 与 llvm prebuilt 目录。

hxc_detect_ndk() {
    if [ -n "$ANDROID_NDK" ] && [ -d "$ANDROID_NDK" ]; then
        NDK_PATH="$ANDROID_NDK"
    elif [ -d "$HOME/Library/Android/sdk/ndk" ]; then
        NDK_PATH=$(ls -d "$HOME/Library/Android/sdk/ndk"/* | sort -V | tail -1)
    elif [ -d "$HOME/Android/Sdk/ndk" ]; then
        NDK_PATH=$(ls -d "$HOME/Android/Sdk/ndk"/* | sort -V | tail -1)
    elif [ -d "/usr/local/android-sdk/ndk" ]; then
        NDK_PATH=$(ls -d /usr/local/android-sdk/ndk/* | sort -V | tail -1)
    else
        echo "❌ 错误: 未找到 Android NDK，请设置 ANDROID_NDK" >&2
        return 1
    fi
    export ANDROID_NDK="$NDK_PATH"
    export ANDROID_NDK_ROOT="$NDK_PATH"
}

hxc_ndk_llvm_prebuilt() {
    local ndk="${1:-$NDK_PATH}"
    local tag=""
    local candidate
    for candidate in linux-x86_64 darwin-arm64 darwin-x86_64 windows-x86_64; do
        if [ -d "$ndk/toolchains/llvm/prebuilt/$candidate" ]; then
            tag="$candidate"
            break
        fi
    done
    if [ -z "$tag" ]; then
        echo "❌ 错误: 未找到 NDK llvm prebuilt: $ndk/toolchains/llvm/prebuilt" >&2
        ls "$ndk/toolchains/llvm/prebuilt" 2>/dev/null || true
        return 1
    fi
    echo "$tag"
}

hxc_nproc() {
    nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4
}
