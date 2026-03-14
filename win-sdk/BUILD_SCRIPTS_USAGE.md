# 构建脚本使用说明

## 📜 build_sdk.bat

### 基本用法

```bash
# Release 构建（默认，清理旧文件）
.\build_sdk.bat

# Debug 构建（清理旧文件）
.\build_sdk.bat debug

# Release 增量编译（保留旧文件，更快）
.\build_sdk.bat --no-clean

# Debug 增量编译
.\build_sdk.bat debug --no-clean
```

### 参数说明

| 参数 | 说明 |
|------|------|
| （无） | Release 构建，清理旧构建目录 |
| `debug` | Debug 构建，清理旧构建目录 |
| `--no-clean` | 保留现有构建目录，增量编译 |

### 使用场景

#### 场景 1：首次构建或完全重新构建

```bash
.\build_sdk.bat
```

**特点**：
- ✅ 清理旧的构建文件
- ✅ 完全重新编译
- ✅ 确保干净的构建环境
- ⏱️ 耗时较长（首次约 2-5 分钟）

#### 场景 2：修改代码后快速重新编译

```bash
.\build_sdk.bat --no-clean
```

**特点**：
- ✅ 保留 CMake 缓存和中间文件
- ✅ 只编译修改的文件
- ✅ 增量编译，速度快
- ⏱️ 耗时短（约 10-30 秒）

#### 场景 3：调试构建

```bash
.\build_sdk.bat debug
```

**特点**：
- ✅ 包含调试符号
- ✅ 未优化代码（便于调试）
- ✅ 生成 .pdb 调试文件

#### 场景 4：调试时的快速迭代

```bash
.\build_sdk.bat debug --no-clean
```

**特点**：
- ✅ Debug 模式增量编译
- ✅ 最快的调试迭代速度

### 构建输出

#### Release 模式

```
输出目录: build\win-sdk-Release\HXCPlayerSDK\
文件大小: 约 14 MB（包含所有依赖）
优化级别: /O2（速度优化）
调试信息: 无
```

#### Debug 模式

```
输出目录: build\win-sdk-Debug\HXCPlayerSDK\
文件大小: 约 20 MB（包含调试信息）
优化级别: /Od（无优化）
调试信息: 完整 .pdb 文件
```

### 常见问题

#### Q1: 构建很慢，如何加快？

**A**: 使用增量编译：

```bash
# 首次构建
.\build_sdk.bat

# 后续修改代码后
.\build_sdk.bat --no-clean  ← 快速重新编译
```

#### Q2: 遇到奇怪的编译错误怎么办？

**A**: 清理并重新构建：

```bash
# 方法 1：完全清理
.\clean_all.bat

# 方法 2：强制清理重建（默认行为）
.\build_sdk.bat
```

#### Q3: 如何切换 Debug/Release？

**A**: 直接切换即可，自动使用不同的构建目录：

```bash
# 构建 Release
.\build_sdk.bat

# 构建 Debug（不会影响 Release）
.\build_sdk.bat debug

# 两个版本可以共存
```

#### Q4: 构建目录在哪里？

**A**: 
- Release: `d:\git\hxcvodplayer\build\win-sdk-Release\`
- Debug: `d:\git\hxcvodplayer\build\win-sdk-Debug\`
- SDK 输出: `build\win-sdk-{Release|Debug}\HXCPlayerSDK\`

### 构建流程

```
用户运行 build_sdk.bat
    ↓
清理旧构建目录（可选）
    ↓
CMake 配置项目
    ↓
MSVC 编译 hxcplayer_core.lib
    ↓
MSVC 编译 hxcplayer.dll
    ↓
打包 SDK (package_sdk 目标)
    ├── 复制头文件
    ├── 复制库文件
    ├── 复制 DLL
    ├── 复制依赖 DLL (copy_dependencies.bat)
    ├── 复制示例
    └── 复制文档
    ↓
SDK 构建完成！
```

## 🧹 clean_all.bat

### 用法

```bash
.\clean_all.bat
```

### 功能

清理所有构建目录和 CMake 缓存：

- `build\` - 所有构建输出
- `build\win-sdk-Debug\` - Debug SDK
- `build\win-sdk-Release\` - Release SDK
- `build\vs2022_debug\` - Desktop Debug
- `build\vs2022_release\` - Desktop Release
- `CMakeCache.txt` - CMake 缓存
- `CMakeFiles\` - CMake 临时文件

### 使用场景

#### 1. 遇到编译错误

```bash
# 清理所有构建文件
.\clean_all.bat

# 重新构建
.\build_sdk.bat
```

#### 2. 切换 CMake 配置

```bash
# 修改了 CMakeLists.txt 或切换了 vcpkg/Qt 路径
.\clean_all.bat
.\build_sdk.bat
```

#### 3. 释放磁盘空间

```bash
# 构建目录可能占用 500MB - 1GB
.\clean_all.bat
```

### 输出示例

```
========================================
清理构建目录
========================================

[扫描] 查找构建目录...

[清理] 正在删除: build\win-sdk-Debug
[清理] ✓ 已删除: build\win-sdk-Debug

[清理] 正在删除: build\win-sdk-Release
[清理] ✓ 已删除: build\win-sdk-Release

========================================
清理完成
========================================
已清理: 2 个目录
跳过: 0 个目录

[清理] ✓ 已删除 CMakeCache.txt
[清理] ✓ 已删除 CMakeFiles

所有构建文件已清理！
```

## 📊 性能对比

### 完全重新构建 vs 增量编译

| 操作 | 命令 | 耗时 | 适用场景 |
|------|------|------|---------|
| **首次构建** | `.\build_sdk.bat` | 2-5 分钟 | 首次编译 |
| **完全重建** | `.\build_sdk.bat` | 2-5 分钟 | 清理后重建 |
| **增量编译** | `.\build_sdk.bat --no-clean` | 10-30 秒 | 修改少量文件 |
| **清理** | `.\clean_all.bat` | 5-10 秒 | 清理所有构建 |

### 修改不同文件的编译时间

| 修改内容 | 增量编译时间 | 完全重建时间 |
|---------|------------|------------|
| 修改 1 个 .cpp | 10-15 秒 | 2-5 分钟 |
| 修改头文件 | 30-60 秒 | 2-5 分钟 |
| 修改 CMakeLists.txt | 需要完全重建 | 2-5 分钟 |
| 添加新文件 | 需要完全重建 | 2-5 分钟 |

## 💡 最佳实践

### 开发工作流

```bash
# 1. 首次构建
.\build_sdk.bat

# 2. 修改代码
# ... 编辑 .cpp/.h 文件 ...

# 3. 快速重新编译
.\build_sdk.bat --no-clean

# 4. 如果遇到问题，完全重建
.\build_sdk.bat

# 5. 如果还有问题，清理所有
.\clean_all.bat
.\build_sdk.bat
```

### 日常开发

```bash
# 早上开始工作 - 完全重建（确保干净）
.\build_sdk.bat

# 白天开发 - 增量编译（快速迭代）
.\build_sdk.bat --no-clean  # 重复多次

# 晚上下班前 - 完全重建（确认没问题）
.\build_sdk.bat
```

### 发布前检查

```bash
# 1. 清理所有旧文件
.\clean_all.bat

# 2. Release 构建
.\build_sdk.bat

# 3. Debug 构建（用于提供调试版本）
.\build_sdk.bat debug

# 4. 测试两个版本
cd build\win-sdk-Release\HXCPlayerSDK\example
# ... 测试 ...

cd build\win-sdk-Debug\HXCPlayerSDK\example
# ... 测试 ...

# 5. 打包分发
cd build\win-sdk-Release
powershell Compress-Archive -Path HXCPlayerSDK -DestinationPath HXCPlayerSDK-v1.0.0.zip
```

## 🔧 高级用法

### 自定义 vcpkg 路径

编辑 `build_sdk.bat` 第 16 行：

```batch
set VCPKG_ROOT=D:\your\custom\vcpkg
```

### 自定义 Qt 路径

编辑 `build_sdk.bat` 第 17 行：

```batch
set QT_DIR=D:\Qt\5.15.2\msvc2019_64
```

### 并行编译

脚本已自动启用并行编译：

```batch
cmake --build . --config %BUILD_TYPE% --parallel
```

默认使用所有 CPU 核心。

### 查看详细编译日志

```bash
# 编辑 build_sdk.bat，在 cmake --build 命令后添加 --verbose
cmake --build . --config %BUILD_TYPE% --parallel --verbose
```

## 📞 故障排查

### 问题：构建目录无法删除

**症状**：
```
[警告] ✗ 无法删除: build\win-sdk-Release （可能被占用）
```

**原因**：Visual Studio 或其他程序正在使用这些文件

**解决**：
1. 关闭 Visual Studio
2. 关闭文件资源管理器
3. 运行 `.\clean_all.bat`
4. 如果仍然失败，重启电脑

### 问题：增量编译失败

**症状**：使用 `--no-clean` 后编译错误

**解决**：
```bash
# 强制完全重建
.\build_sdk.bat
```

### 问题：CMake 配置错误

**症状**：CMake 找不到依赖

**解决**：
```bash
# 清理 CMake 缓存
.\clean_all.bat

# 检查 vcpkg 安装
vcpkg list

# 重新构建
.\build_sdk.bat
```

## 📚 相关文档

- [BUILD_GUIDE.md](BUILD_GUIDE.md) - 完整构建指南
- [DEPENDENCIES.md](DEPENDENCIES.md) - 依赖管理
- [BUILD_SUCCESS.md](../BUILD_SUCCESS.md) - 构建成功总结
