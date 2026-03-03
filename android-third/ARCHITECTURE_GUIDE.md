# Android 架构支持说明

## 支持的架构

本项目的 Android 编译脚本支持三种架构：

### 1. arm64-v8a (64位 ARM)
- **用途**: Android 真机 + Apple Silicon Mac 模拟器
- **设备**: 
  - 2017年后的绝大多数 Android 手机和平板
  - Apple Silicon Mac (M1/M2/M3) 上的 Android 模拟器
- **特点**: 
  - 主流架构，性能最好
  - 现代设备必备

### 2. armeabi-v7a (32位 ARM)
- **用途**: 旧 Android 真机
- **设备**: 2011-2017 年间的 Android 设备
- **特点**: 
  - 兼容老设备
  - 库体积相对较小

### 3. x86_64 (64位 x86)
- **用途**: Intel Mac 上的 Android 模拟器
- **设备**: 
  - Intel Mac 上的 Android 模拟器
  - 少数 x86 架构的 Android 设备（罕见）
- **特点**: 
  - 主要用于开发测试
  - 真机发布时可以不包含

---

## 模拟器支持

### Apple Silicon Mac (M1/M2/M3)

✅ **推荐配置**: 只编译 `arm64-v8a`

```gradle
// app/build.gradle
ndk {
    abiFilters 'arm64-v8a'  // ARM 架构即可
}
```

**原因**: 
- Apple Silicon 是 ARM 架构
- Android 模拟器运行在 ARM 模式下
- 可以直接使用 `arm64-v8a` 的库
- 真机和模拟器共用同一套库

### Intel Mac

✅ **推荐配置**: 编译 `arm64-v8a` + `x86_64`

```gradle
// app/build.gradle
ndk {
    abiFilters 'arm64-v8a', 'x86_64'
}
```

**原因**: 
- Intel Mac 是 x86_64 架构
- Android 模拟器需要 `x86_64` 库
- 真机需要 `arm64-v8a` 库
- 需要两套库同时存在

---

## 架构选择建议

### 场景 1: Apple Silicon Mac 开发

**最简配置**:
```bash
# 只编译 arm64-v8a
# 修改脚本，注释掉其他架构的编译
```

```gradle
ndk {
    abiFilters 'arm64-v8a'
}
```

**优点**:
- ✅ 编译时间最短（约 15-20 分钟）
- ✅ APK 体积最小
- ✅ 模拟器和真机都支持

**适用**: 
- 团队全是 Apple Silicon Mac
- 只关心主流设备

### 场景 2: Intel Mac 开发

**推荐配置**:
```gradle
ndk {
    abiFilters 'arm64-v8a', 'x86_64'
}
```

**优点**:
- ✅ 模拟器调试（x86_64）
- ✅ 真机测试（arm64-v8a）

**缺点**:
- ⚠️ 编译时间较长（约 40-50 分钟）
- ⚠️ APK 体积较大

### 场景 3: 生产发布（全架构）

**完整配置**:
```gradle
ndk {
    abiFilters 'arm64-v8a', 'armeabi-v7a', 'x86_64'
}
```

**优点**:
- ✅ 兼容最多设备
- ✅ 支持旧设备（armeabi-v7a）
- ✅ 支持 x86 平板（罕见）

**缺点**:
- ⚠️ 编译时间最长（约 50-70 分钟）
- ⚠️ APK 体积最大（增加约 60-80 MB）

### 场景 4: 生产发布（仅真机）

**推荐配置**:
```gradle
ndk {
    abiFilters 'arm64-v8a', 'armeabi-v7a'
}
```

**优点**:
- ✅ 覆盖几乎所有真机
- ✅ 不包含模拟器架构，体积较小

**适用**: 
- Google Play 发布
- 面向终端用户

---

## 编译时间对比

| 架构组合 | 编译时间 (Apple Silicon) | APK 增加体积 |
|---------|------------------------|-------------|
| `arm64-v8a` | ~15-20 分钟 | +30 MB |
| `arm64-v8a` + `armeabi-v7a` | ~30-40 分钟 | +60 MB |
| `arm64-v8a` + `x86_64` | ~30-40 分钟 | +60 MB |
| 全部三个架构 | ~50-70 分钟 | +90 MB |

---

## 如何修改支持的架构

### 方法 1: 修改编译脚本

编辑 `build_ffmpeg_android.sh`:

```bash
# 注释掉不需要的架构
# build_ffmpeg "armv7-a" "armeabi-v7a"  # 注释掉 32 位
# build_ffmpeg "x86_64" "x86_64"       # 注释掉模拟器
```

编辑 `build_soundtouch_android.sh`:

```bash
# 注释掉不需要的架构
# build_soundtouch "armv7-a" "armeabi-v7a"
# build_soundtouch "x86_64" "x86_64"
```

### 方法 2: 只修改 Gradle 配置

保留编译好的所有架构，只在 `app/build.gradle` 中选择使用哪些：

```gradle
ndk {
    // 只选择你需要的架构
    abiFilters 'arm64-v8a'
}
```

这样即使编译了三个架构，APK 也只会包含指定的架构。

---

## 常见问题

### Q1: 我是 Apple Silicon Mac，需要编译 x86_64 吗？

**A**: 不需要！Apple Silicon Mac 的模拟器运行在 ARM 模式，直接用 `arm64-v8a` 即可。

### Q2: 为什么模拟器提示缺少库？

**A**: 
- **Intel Mac**: 确保编译了 `x86_64` 架构
- **Apple Silicon Mac**: 确保模拟器运行在 ARM 模式（默认）

检查模拟器架构：
```bash
adb shell getprop ro.product.cpu.abi
# 输出应该是 arm64-v8a (Apple Silicon) 或 x86_64 (Intel)
```

### Q3: 可以只为真机发布编译部分架构吗？

**A**: 可以！Google Play 会根据设备自动分发对应架构的 APK。

推荐配置：
```gradle
ndk {
    abiFilters 'arm64-v8a'  // 只支持现代设备
    // 或
    abiFilters 'arm64-v8a', 'armeabi-v7a'  // 兼容旧设备
}
```

### Q4: 编译时间太长怎么办？

**A**: 
1. 只编译需要的架构（见上文）
2. 使用更多 CPU 核心：修改脚本中的 `make -j$(sysctl -n hw.ncpu)`
3. 首次编译后，增量编译会快很多

---

## 推荐配置总结

| 场景 | 架构选择 | 说明 |
|-----|---------|-----|
| 🧑‍💻 Apple Silicon Mac 开发 | `arm64-v8a` | 最快，模拟器+真机都支持 |
| 🧑‍💻 Intel Mac 开发 | `arm64-v8a`, `x86_64` | 模拟器需要 x86_64 |
| 🚀 生产发布（现代设备） | `arm64-v8a` | 小体积，覆盖 95%+ 设备 |
| 🚀 生产发布（完整兼容） | `arm64-v8a`, `armeabi-v7a` | 兼容老设备 |
| 🧪 完整测试 | 全部三个 | 测试所有平台 |

---

**建议**: 开发阶段只编译 `arm64-v8a`，发布时再根据需要添加其他架构。
