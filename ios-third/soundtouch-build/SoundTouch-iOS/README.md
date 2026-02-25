# SoundTouch for iOS

## 使用方法

### 1. 设置环境变量（可选）

```bash
export SOUNDTOUCH_IOS_DIR=/path/to/SoundTouch-iOS
```

### 2. 在 CMakeLists.txt 中配置

CMake 会自动查找以下位置的 SoundTouch：
- `${SOUNDTOUCH_IOS_DIR}` (CMake 变量)
- `$ENV{SOUNDTOUCH_IOS_DIR}` (环境变量)
- `${CMAKE_SOURCE_DIR}/ios/soundtouch-build/SoundTouch-iOS`

### 3. 链接

```cmake
if(SOUNDTOUCH_FOUND)
    target_include_directories(YourTarget PRIVATE ${SOUNDTOUCH_INCLUDE_DIRS})
    target_link_libraries(YourTarget ${SOUNDTOUCH_LIBRARIES})
endif()
```

## 架构信息

检查库支持的架构：

```bash
lipo -info lib/libSoundTouch.a
```

## 重新编译

如果需要重新编译：

```bash
cd /path/to/project/ios
./build_soundtouch_ios.sh --clean
```
