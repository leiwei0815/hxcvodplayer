# Android NDK 安装问题解决指南

## 问题描述

在 Android Studio 中安装 NDK 时出现错误：
```
An error occurred while preparing SDK package NDK (Side by side) 29.0.14206865: 
Unexpected end of ZLIB input stream.
```

这通常是由于：
- 网络不稳定导致下载中断
- 下载的 ZIP 文件损坏
- 临时文件缓存问题

---

## 🔧 解决方案

### 方案 1：清理缓存后重试 ⭐

1. **关闭 Android Studio**

2. **清理 Android SDK 临时文件和缓存**

```bash
# 清理临时文件
rm -rf $HOME/Library/Android/sdk/temp/
rm -rf $HOME/Library/Android/sdk/.temp/

# 清理可能损坏的下载文件
rm -rf $HOME/Library/Caches/AndroidStudio*/tmp/
rm -rf $HOME/Library/Caches/Google/AndroidStudio*/tmp/

# 如果存在部分下载的 NDK，删除它
rm -rf $HOME/Library/Android/sdk/ndk/*
```

3. **重新打开 Android Studio**

4. **重新尝试安装 NDK**
   - Tools → SDK Manager → SDK Tools
   - 勾选 NDK (Side by side)
   - 点击 Apply

---

### 方案 2：手动下载并安装 NDK（推荐）✨

如果方案 1 还是失败，手动下载更可靠：

#### 步骤 1：下载 NDK

访问以下地址下载 NDK：

**官方下载页**（需要梯子）：
- https://developer.android.com/ndk/downloads

**推荐版本**：
- NDK r25c (推荐，稳定)
- NDK r26 或更新版本

**直接下载链接**（选择一个）：

```bash
# NDK r25c (约 1GB)
https://dl.google.com/android/repository/android-ndk-r25c-darwin.zip

# NDK r26d (约 1.1GB)
https://dl.google.com/android/repository/android-ndk-r26d-darwin.zip

# NDK r27 (约 1.1GB)
https://dl.google.com/android/repository/android-ndk-r27-darwin.zip
```

**使用命令行下载**（推荐，可以断点续传）：

```bash
# 进入下载目录
cd ~/Downloads

# 使用 curl 下载（支持断点续传）
curl -L -C - -o android-ndk-r25c-darwin.zip \
  https://dl.google.com/android/repository/android-ndk-r25c-darwin.zip

# 或者使用 wget（如果安装了）
# brew install wget
wget -c https://dl.google.com/android/repository/android-ndk-r25c-darwin.zip
```

#### 步骤 2：解压 NDK

```bash
# 创建 NDK 目录
mkdir -p $HOME/Library/Android/sdk/ndk

# 解压到 NDK 目录
unzip ~/Downloads/android-ndk-r25c-darwin.zip -d $HOME/Library/Android/sdk/ndk/

# 重命名为版本号格式（可选，与 Android Studio 格式一致）
mv $HOME/Library/Android/sdk/ndk/android-ndk-r25c \
   $HOME/Library/Android/sdk/ndk/25.2.9519653
```

#### 步骤 3：验证安装

```bash
# 检查 NDK 目录
ls -la $HOME/Library/Android/sdk/ndk/

# 检查关键文件
ls $HOME/Library/Android/sdk/ndk/25.2.9519653/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang

# 如果能看到 clang，说明安装成功！
```

#### 步骤 4：设置环境变量

```bash
# 临时设置（当前终端）
export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/25.2.9519653

# 永久设置（添加到 shell 配置文件）
echo 'export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/25.2.9519653' >> ~/.zshrc
source ~/.zshrc

# 验证
echo $ANDROID_NDK
```

---

### 方案 3：使用国内镜像下载（如果官方下载慢）

如果官方下载很慢，可以使用国内镜像：

**腾讯云镜像**：
```bash
# NDK r25c
https://mirrors.cloud.tencent.com/AndroidSDK/android-ndk-r25c-darwin.zip
```

**阿里云镜像**：
```bash
# 访问 https://developer.aliyun.com/mirror/android
# 搜索 NDK 下载
```

下载后，按照方案 2 的步骤 2-4 进行安装。

---

### 方案 4：通过 Homebrew 安装（替代方案）

虽然 Homebrew 的 NDK 可能不是最新版本，但可以快速安装：

```bash
# 安装 Android NDK（如果 brew 中有）
brew install --cask android-ndk

# 或者使用 android-sdk
brew install --cask android-sdk
```

**注意**：这种方式安装的位置可能不同，需要手动设置 `ANDROID_NDK` 环境变量。

---

## ✅ 安装成功后的验证

### 1. 检查 NDK 目录结构

```bash
ls $HOME/Library/Android/sdk/ndk/25.2.9519653/

# 应该看到：
# build/
# meta/
# platforms/
# prebuilt/
# shader-tools/
# simpleperf/
# sources/
# sysroot/
# toolchains/
# CHANGELOG.md
# README.md
# source.properties
```

### 2. 检查编译工具

```bash
# 检查 clang
$HOME/Library/Android/sdk/ndk/25.2.9519653/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang --version

# 应该输出 clang 版本信息
```

### 3. 测试编译脚本

```bash
cd /Users/debug/project/YXVodPlayer/android-third

# 设置 NDK 路径
export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/25.2.9519653

# 运行脚本（应该不再报错）
./build_ffmpeg_android.sh
```

---

## 🎯 推荐的完整流程

### 选项 A：快速手动安装（最可靠）

```bash
# 1. 下载 NDK
cd ~/Downloads
curl -L -C - -o android-ndk-r25c-darwin.zip \
  https://dl.google.com/android/repository/android-ndk-r25c-darwin.zip

# 2. 创建目录并解压
mkdir -p $HOME/Library/Android/sdk/ndk
unzip android-ndk-r25c-darwin.zip -d $HOME/Library/Android/sdk/ndk/
mv $HOME/Library/Android/sdk/ndk/android-ndk-r25c \
   $HOME/Library/Android/sdk/ndk/25.2.9519653

# 3. 设置环境变量
echo 'export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/25.2.9519653' >> ~/.zshrc
source ~/.zshrc

# 4. 验证
ls $HOME/Library/Android/sdk/ndk/25.2.9519653/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang

# 5. 运行编译脚本
cd /Users/debug/project/YXVodPlayer/android-third
./build_all.sh
```

### 选项 B：通过 Android Studio 重试

```bash
# 1. 清理缓存
rm -rf $HOME/Library/Android/sdk/temp/
rm -rf $HOME/Library/Caches/AndroidStudio*/tmp/

# 2. 重启 Android Studio

# 3. 重新安装 NDK
# Tools → SDK Manager → SDK Tools → NDK (Side by side)

# 4. 如果还是失败，使用选项 A
```

---

## 📝 常见问题

### Q1: 下载速度很慢怎么办？

**A**: 
1. 使用国内镜像（腾讯云、阿里云）
2. 使用下载工具（迅雷、IDM 等）
3. 使用代理或 VPN

### Q2: 解压时提示 "Archive corrupted"？

**A**: 
1. 重新下载 ZIP 文件
2. 验证文件完整性：
   ```bash
   # 检查文件大小（r25c 约 1GB）
   ls -lh ~/Downloads/android-ndk-r25c-darwin.zip
   
   # 尝试用其他工具解压
   unzip -t android-ndk-r25c-darwin.zip
   ```

### Q3: 需要安装哪个版本的 NDK？

**A**: 推荐版本：
- **r25c** - 稳定，与大多数项目兼容 ⭐
- **r26d** - 较新，支持更多特性
- **r27+** - 最新，可能有兼容性问题

对于本项目，**r25c 或更高版本**都可以。

### Q4: 多个版本的 NDK 可以共存吗？

**A**: 可以！Android Studio 支持 "Side by side" 安装：
```
$HOME/Library/Android/sdk/ndk/
├── 25.2.9519653/
├── 26.1.10909125/
└── 27.0.12077973/
```

使用时指定具体版本即可。

### Q5: 环境变量设置后还是找不到 NDK？

**A**: 
```bash
# 检查环境变量
echo $ANDROID_NDK

# 如果为空，重新加载配置
source ~/.zshrc  # 或 source ~/.bash_profile

# 或者临时设置
export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/25.2.9519653
```

---

## 🚀 下一步

安装成功后：

1. ✅ 验证 NDK 安装
2. ✅ 设置环境变量
3. ✅ 运行编译脚本：
   ```bash
   cd /Users/debug/project/YXVodPlayer/android-third
   ./build_all.sh
   ```

预计编译时间：50-70 分钟

---

## 📞 需要帮助？

如果以上方法都不行，请提供：
1. 你的 macOS 版本
2. Android Studio 版本
3. 网络环境（是否使用代理）
4. 具体的错误信息截图
