#!/bin/bash
#============================================================
# FFmpeg 8.0.1 Windows 交叉编译脚本 (WSL/Ubuntu)
#============================================================
# 
# 功能：
#   - 在 WSL/Ubuntu 中编译 Windows 版本的 FFmpeg
#   - 使用 MinGW 交叉编译工具链
#   - 生成 Windows DLL 动态库
#   - 输出到 win-third/ffmpeg-install
#
# 使用方法：
#   1. 在 WSL/Ubuntu 终端执行：
#      cd /mnt/d/git/hxcvodplayer/win-third
#      chmod +x build_ffmpeg_wsl.sh
#      ./build_ffmpeg_wsl.sh
#
#============================================================

set -e  # 遇到错误立即退出

echo "========================================"
echo "FFmpeg 8.0.1 Windows 交叉编译 (WSL)"
echo "========================================"
echo ""

#========================================
# 配置参数
#========================================
FFMPEG_VERSION="8.0.1"
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="${SCRIPT_DIR}/ffmpeg-src"
BUILD_DIR="${SCRIPT_DIR}/ffmpeg-build-wsl"
INSTALL_DIR="${SCRIPT_DIR}/ffmpeg-install"

echo "配置信息："
echo "  源码目录: ${SOURCE_DIR}"
echo "  构建目录: ${BUILD_DIR}"
echo "  安装目录: ${INSTALL_DIR}"
echo ""

#========================================
# 第 1 步：检查并安装依赖
#========================================
echo "[1/5] 检查编译工具..."
echo ""

# 检查是否有 sudo 权限
if ! sudo -n true 2>/dev/null; then
    echo "需要 sudo 权限来安装依赖包"
fi

# 检查必需工具
MISSING_TOOLS=""

if ! command -v x86_64-w64-mingw32-gcc &> /dev/null; then
    MISSING_TOOLS="${MISSING_TOOLS} mingw-w64"
fi

if ! command -v yasm &> /dev/null; then
    MISSING_TOOLS="${MISSING_TOOLS} yasm"
fi

if ! command -v nasm &> /dev/null; then
    MISSING_TOOLS="${MISSING_TOOLS} nasm"
fi

if ! command -v make &> /dev/null; then
    MISSING_TOOLS="${MISSING_TOOLS} build-essential"
fi

if [ -n "${MISSING_TOOLS}" ]; then
    echo "缺少必需工具，正在安装:${MISSING_TOOLS}"
    echo ""
    
    sudo apt update
    sudo apt install -y build-essential mingw-w64 yasm nasm pkg-config
    
    echo ""
    echo "✓ 依赖工具安装完成"
else
    echo "✓ 所有必需工具已安装"
fi

echo ""

# 显示工具版本
echo "工具版本信息："
x86_64-w64-mingw32-gcc --version | head -n 1
yasm --version | head -n 1
nasm --version | head -n 1
echo ""

#========================================
# 第 2 步：下载 FFmpeg 源码
#========================================
if [ ! -d "${SOURCE_DIR}" ]; then
    echo "[2/5] 正在下载 FFmpeg ${FFMPEG_VERSION} 源码..."
    echo ""
    
    # 创建临时目录
    TEMP_DIR="${SCRIPT_DIR}/temp"
    mkdir -p "${TEMP_DIR}"
    
    # 下载源码
    echo "下载地址: ${FFMPEG_URL}"
    if command -v wget &> /dev/null; then
        wget -O "${TEMP_DIR}/ffmpeg.tar.xz" "${FFMPEG_URL}"
    else
        curl -L -o "${TEMP_DIR}/ffmpeg.tar.xz" "${FFMPEG_URL}"
    fi
    
    echo "✓ 下载完成"
    echo ""
    
    # 解压源码
    echo "正在解压..."
    tar -xf "${TEMP_DIR}/ffmpeg.tar.xz" -C "${TEMP_DIR}"
    
    # 重命名目录
    mv "${TEMP_DIR}/ffmpeg-${FFMPEG_VERSION}" "${SOURCE_DIR}"
    
    # 清理临时文件
    rm -rf "${TEMP_DIR}"
    
    echo "✓ 源码准备完成"
    echo ""
else
    echo "[2/5] ✓ FFmpeg 源码已存在"
    echo ""
fi

#========================================
# 第 3 步：准备构建目录
#========================================
echo "[3/5] 准备构建目录..."

if [ -d "${BUILD_DIR}" ]; then
    echo "清理旧的构建目录..."
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "✓ 构建目录已准备"
echo ""

#========================================
# 第 4 步：配置 FFmpeg
#========================================
echo "[4/5] 配置 FFmpeg (Windows x64)..."
echo ""

# 配置为 Windows 构建
"${SOURCE_DIR}/configure" \
    --arch=x86_64 \
    --target-os=mingw32 \
    --cross-prefix=x86_64-w64-mingw32- \
    --prefix="${INSTALL_DIR}" \
    --enable-shared \
    --disable-static \
    --disable-doc \
    --disable-ffplay \
    --disable-ffprobe \
    --disable-ffmpeg \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swscale \
    --enable-swresample \
    --enable-network \
    --enable-protocol=file \
    --enable-protocol=http \
    --enable-protocol=https \
    --enable-protocol=crypto \
    --enable-protocol=tcp \
    --enable-protocol=udp \
    --enable-decoder=h264 \
    --enable-decoder=hevc \
    --enable-decoder=aac \
    --enable-decoder=mp3 \
    --enable-parser=h264 \
    --enable-parser=hevc \
    --enable-demuxer=mov \
    --enable-demuxer=mpegts \
    --enable-demuxer=flv \
    --enable-demuxer=hls \
    --enable-small \
    --disable-debug

if [ $? -ne 0 ]; then
    echo ""
    echo "[错误] FFmpeg 配置失败"
    exit 1
fi

echo ""
echo "✓ FFmpeg 配置完成"
echo ""

#========================================
# 第 5 步：编译和安装
#========================================
echo "[5/5] 编译 FFmpeg (这可能需要 20-40 分钟)..."
echo "提示：可以使用 htop 查看编译进度"
echo ""

# 获取 CPU 核心数
NPROC=$(nproc)
echo "使用 ${NPROC} 个并行任务编译..."
echo ""

# 编译
make -j${NPROC}

if [ $? -ne 0 ]; then
    echo ""
    echo "[错误] FFmpeg 编译失败"
    exit 1
fi

echo ""
echo "✓ FFmpeg 编译完成"
echo ""

# 安装
echo "正在安装到: ${INSTALL_DIR}"
make install

if [ $? -ne 0 ]; then
    echo ""
    echo "[错误] FFmpeg 安装失败"
    exit 1
fi

echo "✓ FFmpeg 安装完成"
echo ""

#========================================
# 生成 MSVC 导入库（可选）
#========================================
echo "[额外步骤] 检查是否需要生成 MSVC 导入库..."
echo ""

# 检查是否在 Windows 环境中有 Visual Studio
if command -v cmd.exe &> /dev/null; then
    echo "检测到 Windows 环境，可以稍后生成 MSVC .lib 文件"
    echo "在 VS 开发者命令提示符中运行 generate_msvc_libs.bat"
else
    echo "跳过 MSVC .lib 生成（需要在 Windows 中运行）"
fi

echo ""

#========================================
# 验证输出
#========================================
echo "========================================"
echo "验证编译输出..."
echo "========================================"
echo ""

if [ -d "${INSTALL_DIR}/bin" ]; then
    echo "DLL 文件:"
    ls -lh "${INSTALL_DIR}/bin"/*.dll 2>/dev/null || echo "  (未找到 DLL 文件)"
fi

echo ""

if [ -d "${INSTALL_DIR}/lib" ]; then
    echo "LIB 文件:"
    ls -lh "${INSTALL_DIR}/lib"/*.a 2>/dev/null || echo "  (未找到 LIB 文件)"
fi

echo ""

if [ -d "${INSTALL_DIR}/include" ]; then
    echo "头文件目录:"
    ls -d "${INSTALL_DIR}/include"/*/ 2>/dev/null || echo "  (未找到头文件)"
fi

echo ""

#========================================
# 完成
#========================================
echo "========================================"
echo "✓ FFmpeg ${FFMPEG_VERSION} 编译完成！"
echo "========================================"
echo ""
echo "安装目录: ${INSTALL_DIR}"
echo "  - 头文件: ${INSTALL_DIR}/include"
echo "  - DLL:     ${INSTALL_DIR}/bin"
echo "  - LIB:     ${INSTALL_DIR}/lib"
echo ""
echo "下一步："
echo "  1. 返回 Windows，运行 build_sdl2.bat 编译 SDL2"
echo "  2. 运行 win-third/install_all.bat 配置项目"
echo ""
echo "提示：DLL 文件可以在 Windows 中直接使用"
echo ""

exit 0
