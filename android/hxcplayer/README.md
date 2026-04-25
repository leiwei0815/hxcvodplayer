# HXCPlayer Android 库模块

## 目录结构

```
android-test/
├── app/                    # 示例应用（展示如何使用库）
└── hxcplayer/             # 播放器库模块
    ├── build.gradle       # 库构建配置
    ├── src/main/
    │   ├── AndroidManifest.xml
    │   ├── java/com/hxcplayer/
    │   │   └── HXCPlayerControl.kt      # 播放器控制类
    │   ├── cpp/
    │   │   ├── hxcplayer_jni.cpp        # JNI 桥接
    │   │   ├── android_player.cpp/h     # Android 播放器实现
    │   │   └── CMakeLists.txt           # CMake 配置
    │   └── jniLibs/
    │       ├── arm64-v8a/               # ARM64 库
    │       ├── armeabi-v7a/             # ARMv7 库
    │       └── x86_64/                  # x86_64 库
```

## 打包 AAR 库

### 方式1: 使用脚本（推荐）

```bash
cd /Users/debug/project/YXVodPlayer/examples/android-test
./build_library.sh
```

脚本会：
1. ✅ 复制第三方库（mbedTLS、FFmpeg、SoundTouch）
2. ✅ 构建 Release AAR
3. ✅ 显示 AAR 路径和大小
4. ✅ 询问是否复制到桌面

输出：`hxcplayer/build/outputs/aar/hxcplayer-release.aar`

### 方式2: 手动构建

```bash
cd /Users/debug/project/YXVodPlayer/examples/android-test

# 1. 复制第三方库
./copy_libs.sh

# 然后手动复制到 hxcplayer/src/main/jniLibs/

# 2. 构建 AAR
./gradlew :hxcplayer:assembleRelease

# 3. 查找 AAR
find hxcplayer/build/outputs/aar/ -name "*.aar"
```

### 方式3: 在 Android Studio 中

1. 打开项目
2. 在 Gradle 面板中：**hxcplayer → Tasks → build → assembleRelease**
3. 双击执行
4. 完成后在 `hxcplayer/build/outputs/aar/` 找到 AAR

## 使用 AAR 库

### 1. 本地集成

**步骤1**: 复制 AAR 到项目

```bash
cp hxcplayer-release.aar /path/to/your-project/app/libs/
```

**步骤2**: 配置 build.gradle

```gradle
dependencies {
    implementation files('libs/hxcplayer-release.aar')
}
```

**步骤3**: Sync Project with Gradle Files

### 2. Maven 发布（可选）

在 `hxcplayer/build.gradle` 中添加：

```gradle
apply plugin: 'maven-publish'

publishing {
    publications {
        release(MavenPublication) {
            groupId = 'com.hxcplayer'
            artifactId = 'hxcplayer'
            version = '1.0.0'
            
            artifact("$buildDir/outputs/aar/hxcplayer-release.aar")
        }
    }
    
    repositories {
        maven {
            url = uri("$buildDir/repo")
        }
    }
}
```

发布：
```bash
./gradlew :hxcplayer:publish
```

### 3. 使用示例

```kotlin
import com.hxcplayer.HXCPlayerControl

class MyActivity : AppCompatActivity(), HXCPlayerControl.PlayerCallback {
    private lateinit var player: HXCPlayerControl
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // 创建播放器（可选第二参数：VideoRenderViewType.TEXTURE_VIEW）
        player = HXCPlayerControl(this)
        player.setCallback(this)
        
        // 添加视频视图（renderView / videoView 为同一实例）
        val container = findViewById<FrameLayout>(R.id.videoContainer)
        container.addView(player.renderView)
        
        // License 校验通过后再播放（Android SDK 当前为强制校验）
        player.checkLicense(
            licenseKey = "你的32位licenseKey",
            licenseUrl = "https://console-api.huaxiacloud.net/license/getLicense/111453136245362688"
        ) { success, error ->
            if (success) {
                val model = HXCPlayerControl.PlayerDataSourcePlayModel.modelWithURL(
                    url = "http://example.com/video.mp4",
                    mode = HXCPlayerControl.PlayerDataSourceMode.DEFAULT,
                    encryptedFile = false
                )
                player.openWithPlayModel(model)
                player.play()
            } else {
                // 校验失败，建议提示用户
            }
        }
    }
    
    override fun onDestroy() {
        super.onDestroy()
        player.release()
    }
    
    // 实现回调接口...
}
```

### License 校验说明（Android）

- 调用 `openURL(...)` / `openWithPlayModel(...)` / `playURL(...)` / `playWithModel(...)` / `play()` / `seekTo(...)` / `seekToPosition(...)` 前，需先 `checkLicense(...)`
- 校验通过条件：解密后的 License 数组里存在记录满足：
  - `package_name == context.packageName`
  - `finished_at > 当前 Unix 时间戳`
- `checkLicense(...)` 失败时会自动尝试本地缓存；缓存仍有效则返回成功
- 如需清空缓存并重置状态，可调用：

```kotlin
player.resetLicenseState()
```

## API 文档

完整 API 文档请参考：[API_USAGE.md](API_USAGE.md)

## 注意事项

1. **权限**：使用库的应用需要在 AndroidManifest.xml 中声明：
```xml
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
```

2. **架构支持**：
   - arm64-v8a（真机）
   - armeabi-v7a（旧真机）
   - x86_64（模拟器）

3. **依赖库**：
   - FFmpeg 8.0.1（视频解码）
   - mbedTLS 2.28.9（HTTPS 支持）
   - SoundTouch 2.3.3（倍速播放）

4. **最小 SDK**：Android 7.0 (API 24)

## 库大小

- AAR 大小：约 15-20 MB（包含所有架构）
- 单个架构（arm64-v8a）：约 5-7 MB

## 对比 iOS xcframework

| 特性 | iOS xcframework | Android AAR |
|------|----------------|-------------|
| 格式 | 静态库 (.xcframework) | 动态库 (.aar) |
| 包含内容 | .a + headers | .so + classes |
| 架构 | arm64, x86_64 | arm64-v8a, armeabi-v7a, x86_64 |
| 集成方式 | 拖入 Xcode | Gradle 依赖 |
| API 设计 | HXCPlayerControl | HXCPlayerControl |
| 视图管理 | videoView | `renderView` / `videoView`（SurfaceView 或 TextureView） |

## 故障排查

**问题1**: 找不到 native 库
```
解决：确保 jniLibs 目录包含所有 .so 文件
```

**问题2**: JNI 方法找不到
```
解决：检查包名是否正确（com.hxcplayer）
```

**问题3**: 构建失败
```
解决：
1. 清理构建：./gradlew :hxcplayer:clean
2. 重新 Sync：File → Sync Project with Gradle Files
3. 检查 NDK 是否安装
```

## License

MIT License
