#!/bin/bash

# HXCPlayer 测试项目构建脚本
# 用于快速生成和打开测试项目

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "🔨 HXCPlayer 测试项目构建工具"
echo "================================"
echo ""

# 检查 XCFramework 是否存在
XCFRAMEWORK_PATH="$PROJECT_ROOT/apple/build_xcframework/HXCPlayer.xcframework"
if [ ! -d "$XCFRAMEWORK_PATH" ]; then
    echo "❌ 错误：HXCPlayer.xcframework 不存在"
    echo ""
    echo "请先运行以下命令构建 XCFramework："
    echo "  cd $PROJECT_ROOT/apple"
    echo "  ./build_xcframework_simple.sh"
    echo ""
    exit 1
fi

echo "✅ 找到 HXCPlayer.xcframework"
echo ""

# 检查 XcodeGen 是否安装
if ! command -v xcodegen &> /dev/null; then
    echo "⚠️  警告：未安装 XcodeGen"
    echo ""
    echo "XcodeGen 用于生成 Xcode 项目文件。"
    echo "如果你想使用 project.yml 配置，请安装："
    echo "  brew install xcodegen"
    echo ""
    read -p "是否继续（将只创建目录结构）? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
    SKIP_XCODEGEN=true
else
    echo "✅ 找到 XcodeGen"
    SKIP_XCODEGEN=false
fi

echo ""
echo "请选择要构建的项目："
echo "  1) iOS 测试项目"
echo "  2) macOS 测试项目"
echo "  3) 两者都构建"
echo ""
read -p "请输入选项 (1-3): " choice

case $choice in
    1)
        PROJECTS=("ios-test")
        ;;
    2)
        PROJECTS=("macos-test")
        ;;
    3)
        PROJECTS=("ios-test" "macos-test")
        ;;
    *)
        echo "❌ 无效选项"
        exit 1
        ;;
esac

echo ""

for project in "${PROJECTS[@]}"; do
    echo "🔨 处理 $project..."
    cd "$SCRIPT_DIR/$project"
    
    if [ "$SKIP_XCODEGEN" = false ]; then
        echo "   生成 Xcode 项目..."
        xcodegen generate
        
        if [ $? -eq 0 ]; then
            echo "   ✅ 生成成功"
        else
            echo "   ❌ 生成失败"
            exit 1
        fi
    fi
    
    echo ""
done

echo "================================"
echo "🎉 完成！"
echo ""
echo "📂 项目位置："
for project in "${PROJECTS[@]}"; do
    if [ "$project" = "ios-test" ]; then
        PROJECT_NAME="HXCPlayerIOSTest"
    else
        PROJECT_NAME="HXCPlayerMacOSTest"
    fi
    
    XCODEPROJ="$SCRIPT_DIR/$project/$PROJECT_NAME.xcodeproj"
    if [ -d "$XCODEPROJ" ]; then
        echo "   $XCODEPROJ"
    else
        echo "   $SCRIPT_DIR/$project/ (未生成 .xcodeproj)"
    fi
done

echo ""
echo "🚀 使用说明："
echo ""

if [ "$SKIP_XCODEGEN" = false ]; then
    echo "方式 1：使用 Xcode"
    for project in "${PROJECTS[@]}"; do
        if [ "$project" = "ios-test" ]; then
            PROJECT_NAME="HXCPlayerIOSTest"
        else
            PROJECT_NAME="HXCPlayerMacOSTest"
        fi
        echo "  open $SCRIPT_DIR/$project/$PROJECT_NAME.xcodeproj"
    done
    
    echo ""
    echo "方式 2：使用命令行"
    for project in "${PROJECTS[@]}"; do
        if [ "$project" = "ios-test" ]; then
            PROJECT_NAME="HXCPlayerIOSTest"
            echo "  cd $SCRIPT_DIR/$project"
            echo "  xcodebuild -project $PROJECT_NAME.xcodeproj -scheme $PROJECT_NAME -destination 'platform=iOS Simulator,name=iPhone 14' run"
        else
            PROJECT_NAME="HXCPlayerMacOSTest"
            echo "  cd $SCRIPT_DIR/$project"
            echo "  xcodebuild -project $PROJECT_NAME.xcodeproj -scheme $PROJECT_NAME run"
        fi
    done
else
    echo "请安装 XcodeGen 后重新运行此脚本："
    echo "  brew install xcodegen"
    echo "  $0"
fi

echo ""
echo "📚 更多信息请查看 examples/README.md"
echo ""
