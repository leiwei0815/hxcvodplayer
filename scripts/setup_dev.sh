#!/bin/bash

# 开发环境设置脚本

set -e

echo "========================================="
echo "YXVodPlayer 开发环境设置"
echo "========================================="

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

function print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

function print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# 检测操作系统
OS="$(uname -s)"
case "${OS}" in
    Linux*)     MACHINE=Linux;;
    Darwin*)    MACHINE=Mac;;
    CYGWIN*)    MACHINE=Cygwin;;
    MINGW*)     MACHINE=MinGw;;
    *)          MACHINE="UNKNOWN:${OS}"
esac

print_info "检测到操作系统: $MACHINE"

# macOS 设置
function setup_macos() {
    print_info "设置 macOS 开发环境..."
    
    # 检查 Homebrew
    if ! command -v brew &> /dev/null; then
        print_info "安装 Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    
    # 安装依赖
    print_info "安装依赖包..."
    brew install cmake pkg-config
    brew install ffmpeg sdl2 qt@5
    
    # 配置环境变量
    print_info "配置环境变量..."
    cat >> ~/.zshrc << 'EOF'

# YXVodPlayer 开发环境
export Qt5_DIR=/usr/local/opt/qt@5/lib/cmake/Qt5
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
export PATH="/usr/local/opt/qt@5/bin:$PATH"
EOF
    
    print_info "macOS 环境设置完成！"
    print_info "请运行 'source ~/.zshrc' 使环境变量生效"
}

# Linux 设置
function setup_linux() {
    print_info "设置 Linux 开发环境..."
    
    # 检测发行版
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
    else
        DISTRO="unknown"
    fi
    
    print_info "检测到发行版: $DISTRO"
    
    case $DISTRO in
        ubuntu|debian)
            print_info "安装依赖包..."
            sudo apt update
            sudo apt install -y \
                build-essential \
                cmake \
                pkg-config \
                git \
                libavcodec-dev \
                libavformat-dev \
                libavutil-dev \
                libswscale-dev \
                libswresample-dev \
                libsdl2-dev \
                qtbase5-dev \
                qtdeclarative5-dev
            ;;
        fedora|rhel|centos)
            print_info "安装依赖包..."
            sudo dnf install -y \
                gcc gcc-c++ \
                cmake \
                pkg-config \
                git \
                ffmpeg-devel \
                SDL2-devel \
                qt5-qtbase-devel
            ;;
        arch)
            print_info "安装依赖包..."
            sudo pacman -S --needed \
                base-devel \
                cmake \
                pkg-config \
                git \
                ffmpeg \
                sdl2 \
                qt5-base
            ;;
        *)
            print_warn "未知的发行版，请手动安装依赖"
            ;;
    esac
    
    print_info "Linux 环境设置完成！"
}

# Android 设置
function setup_android() {
    print_info "设置 Android 开发环境..."
    
    # 检查 Android Studio
    if [ -d "$HOME/Library/Android/sdk" ]; then
        print_info "检测到 Android SDK"
        export ANDROID_HOME="$HOME/Library/Android/sdk"
    elif [ -d "$HOME/Android/Sdk" ]; then
        print_info "检测到 Android SDK"
        export ANDROID_HOME="$HOME/Android/Sdk"
    else
        print_warn "未检测到 Android SDK，请安装 Android Studio"
        return
    fi
    
    # 配置环境变量
    cat >> ~/.bashrc << EOF

# Android SDK
export ANDROID_HOME=$ANDROID_HOME
export PATH=\$PATH:\$ANDROID_HOME/platform-tools
export PATH=\$PATH:\$ANDROID_HOME/tools
EOF
    
    print_info "Android 环境设置完成！"
}

# iOS 设置
function setup_ios() {
    if [ "$MACHINE" != "Mac" ]; then
        print_warn "iOS 开发只支持 macOS"
        return
    fi
    
    print_info "设置 iOS 开发环境..."
    
    # 检查 Xcode
    if ! command -v xcodebuild &> /dev/null; then
        print_warn "请从 App Store 安装 Xcode"
        return
    fi
    
    # 安装 Xcode Command Line Tools
    xcode-select --install 2>/dev/null || true
    
    print_info "iOS 环境设置完成！"
}

# 设置 Git hooks
function setup_git_hooks() {
    print_info "设置 Git hooks..."
    
    # Pre-commit hook
    cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash

# 检查代码格式
echo "检查代码格式..."

# 查找所有修改的 C++ 文件
STAGED_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|h|cc|hpp)$')

if [ -n "$STAGED_FILES" ]; then
    # 可以添加 clang-format 检查
    echo "找到 C++ 文件，跳过格式检查"
fi

exit 0
EOF
    
    chmod +x .git/hooks/pre-commit
    
    print_info "Git hooks 设置完成"
}

# 主函数
function main() {
    case $MACHINE in
        Mac)
            setup_macos
            setup_ios
            ;;
        Linux)
            setup_linux
            ;;
        *)
            print_warn "不支持的操作系统: $MACHINE"
            exit 1
            ;;
    esac
    
    # 设置 Git hooks
    if [ -d ".git" ]; then
        setup_git_hooks
    fi
    
    echo ""
    print_info "========================================="
    print_info "环境设置完成！"
    print_info "========================================="
    echo ""
    print_info "下一步:"
    print_info "1. 重新加载 shell 配置 (source ~/.bashrc 或 source ~/.zshrc)"
    print_info "2. 运行 ./build.sh desktop 构建桌面版本"
    print_info "3. 查看 docs/BUILD.md 了解更多构建选项"
}

main
