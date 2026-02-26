# Info.plist CFBundleExecutable 问题修复

## 🐛 问题描述

运行 iOS 测试项目时遇到错误：
```
Bundle at path .../HXCPlayer.framework has missing or invalid CFBundleExecutable in its Info.plist
```

## 🔍 问题原因

`HXCPlayer.framework` 的 `Info.plist` 中缺少 `CFBundleExecutable` 字段，或该字段的值未正确设置。

### 根本原因

在 `apple/framework/Info.plist.in` 模板中，使用了 CMake 变量：
```xml
<key>CFBundleExecutable</key>
<string>${MACOSX_FRAMEWORK_EXECUTABLE_NAME}</string>
```

但是在通过 Xcode 构建时，这个 CMake 变量可能没有被正确替换，导致生成的 `Info.plist` 缺少 `CFBundleExecutable` 的值。

## ✅ 修复方案

### 1. 修改 `apple/framework/Info.plist.in`

将 CMake 变量替换为硬编码的值：

```xml
<key>CFBundleExecutable</key>
<string>HXCPlayer</string>
<key>CFBundleName</key>
<string>HXCPlayer</string>
```

### 2. 更新 `apple/framework/CMakeLists.txt`

添加更多 Framework 属性以确保正确生成 Info.plist：

```cmake
set_target_properties(HXCPlayer PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION A
    MACOSX_FRAMEWORK_IDENTIFIER com.hxc.HXCPlayer
    MACOSX_FRAMEWORK_BUNDLE_VERSION 1.0.0
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0.0
    VERSION ${PROJECT_VERSION}
    SOVERSION 1.0
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
    PRIVATE_HEADER "${PRIVATE_HEADERS}"
    MACOSX_FRAMEWORK_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    XCODE_ATTRIBUTE_DEFINES_MODULE YES
    XCODE_ATTRIBUTE_MODULEMAP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/module.modulemap"
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_BUILD_LIBRARY_FOR_DISTRIBUTION YES
    XCODE_ATTRIBUTE_INFOPLIST_PREPROCESS YES      # 新增
    XCODE_ATTRIBUTE_PRODUCT_NAME "HXCPlayer"      # 新增
)
```

### 3. 重新构建 XCFramework

```bash
cd /Users/debug/project/YXVodPlayer/apple
./build_xcframework_simple.sh
```

## 🔎 验证修复

### 检查 Info.plist 内容

```bash
plutil -convert xml1 -o - apple/build_xcframework/HXCPlayer.xcframework/ios-arm64_x86_64-simulator/HXCPlayer.framework/Info.plist | grep -A 1 CFBundleExecutable
```

预期输出：
```xml
<key>CFBundleExecutable</key>
<string>HXCPlayer</string>
```

### 验证可执行文件存在

```bash
ls -lh apple/build_xcframework/HXCPlayer.xcframework/ios-arm64_x86_64-simulator/HXCPlayer.framework/HXCPlayer
```

应该看到一个大约 24MB 的可执行文件。

### 运行测试应用

```bash
# 编译
cd examples/ios-test
xcodebuild -project HXCPlayerIOSTest.xcodeproj \
           -scheme HXCPlayerIOSTest \
           -destination 'platform=iOS Simulator,name=iPhone 17' \
           build

# 安装到模拟器
xcrun simctl install booted ~/Library/Developer/Xcode/DerivedData/HXCPlayerIOSTest-*/Build/Products/Debug-iphonesimulator/HXCPlayerIOSTest.app

# 启动应用
xcrun simctl launch booted com.hxcplayer.iostest
```

如果成功，会输出进程 ID，例如：
```
com.hxcplayer.iostest: 6124
```

## ✅ 验证结果

- ✅ `CFBundleExecutable` 字段已正确设置为 `HXCPlayer`
- ✅ 可执行文件存在且为 Mach-O 格式（arm64 + x86_64）
- ✅ 应用成功安装到模拟器
- ✅ 应用成功启动，无 Info.plist 错误

## 📝 技术说明

### 为什么使用硬编码而不是 CMake 变量？

在使用 CMake 生成 Xcode 项目时：
1. CMake 会处理 `.in` 文件并替换变量（configure 阶段）
2. 但 `MACOSX_FRAMEWORK_INFO_PLIST` 属性会告诉 Xcode 使用这个文件作为模板
3. Xcode 在 build 阶段可能不会再次进行变量替换
4. 如果 CMake 变量未正确传递到 Xcode，就会导致变量值为空

因此，对于简单且固定的值（如 Framework 名称），直接硬编码是更可靠的方案。

### macOS 版本受影响吗？

理论上 macOS 版本也应该进行同样的修复，但由于 macOS Framework 的加载机制稍有不同，这个问题在 macOS 上可能不会立即显现。为了保持一致性，建议对所有平台应用相同的修复。

## 🎯 最佳实践

在创建 XCFramework 时：
1. **明确设置 `CFBundleExecutable`**：确保 Info.plist 中有这个字段
2. **验证构建输出**：每次构建后检查 Info.plist 内容
3. **测试实际安装**：在模拟器/真机上实际测试 Framework 能否正常加载
4. **避免过度依赖变量**：对于简单值，硬编码比变量替换更可靠

## 📚 相关资料

- [Apple CFBundleExecutable 文档](https://developer.apple.com/documentation/bundleresources/information_property_list/cfbundleexecutable)
- [CMake Framework 属性](https://cmake.org/cmake/help/latest/prop_tgt/FRAMEWORK.html)
- [XCFramework 构建指南](https://developer.apple.com/documentation/xcode/creating-a-multi-platform-binary-framework-bundle)
