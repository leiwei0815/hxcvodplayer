# Android 日志配置说明

## 功能概述

Android demo 项目已配置文件日志功能，启动时自动启用。

## 配置位置

日志配置在 `MainActivity.kt` 的 `setupLogging()` 方法中：

```kotlin
private fun setupLogging() {
    // 获取应用的外部文件目录（Android/data/包名/files/）
    val logDir = getExternalFilesDir(null)?.absolutePath ?: filesDir.absolutePath
    
    // 启用文件日志
    HXCPlayerControl.enableFileLogging(logDir, "hxcplayer")
    
    // 设置日志级别：0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR
    HXCPlayerControl.setLogLevel(1)  // INFO 级别
    
    // 设置日志保留天数
    HXCPlayerControl.setLogRetentionDays(7)  // 保留 7 天
    
    // 获取当前日志文件路径
    val currentLogFile = HXCPlayerControl.getCurrentLogFile()
}
```

## 日志存储位置

日志文件存储在应用的外部文件目录：

```
/sdcard/Android/data/com.hxcplayer.test/files/hxcplayer_YYYYMMDD_HHMMSS.log
```

**特点**：
- 无需存储权限（Android 10+）
- 应用卸载时自动删除
- 可通过 Android Studio Device File Explorer 访问

## 查看日志文件

### 方法 1：Android Studio

1. 打开 `View` -> `Tool Windows` -> `Device File Explorer`
2. 导航到 `/sdcard/Android/data/com.hxcplayer.test/files/`
3. 右键日志文件 -> `Save As...` 保存到本地

### 方法 2：adb 命令

```bash
# 列出日志文件
adb shell ls -la /sdcard/Android/data/com.hxcplayer.test/files/

# 下载日志文件
adb pull /sdcard/Android/data/com.hxcplayer.test/files/hxcplayer_*.log ./

# 实时查看日志（需要 root）
adb shell tail -f /sdcard/Android/data/com.hxcplayer.test/files/hxcplayer_*.log
```

### 方法 3：应用内显示

启动应用时会通过 Toast 显示当前日志文件路径。

## 日志级别

| 级别 | 数值 | 说明 |
|------|------|------|
| DEBUG | 0 | 调试信息（最详细） |
| INFO | 1 | 一般信息（推荐） |
| WARNING | 2 | 警告信息 |
| ERROR | 3 | 错误信息 |

## 日志管理

- **自动清理**：每次启动时清理 7 天前的日志文件
- **多文件保留**：每次启动创建新日志文件（文件名带时间戳）
- **空间占用**：日志文件会根据保留天数自动管理，不会无限增长

## API 使用示例

### 修改日志级别

```kotlin
// 设置为 DEBUG 级别（最详细）
HXCPlayerControl.setLogLevel(0)

// 设置为 ERROR 级别（只记录错误）
HXCPlayerControl.setLogLevel(3)
```

### 修改保留天数

```kotlin
// 保留 3 天
HXCPlayerControl.setLogRetentionDays(3)

// 保留 30 天
HXCPlayerControl.setLogRetentionDays(30)
```

### 临时禁用文件日志

```kotlin
// 禁用文件日志
HXCPlayerControl.disableFileLogging()

// 重新启用
val logDir = getExternalFilesDir(null)?.absolutePath ?: filesDir.absolutePath
HXCPlayerControl.enableFileLogging(logDir, "hxcplayer")
```

### 获取当前日志文件

```kotlin
val currentLogFile = HXCPlayerControl.getCurrentLogFile()
Log.i("MainActivity", "当前日志文件: $currentLogFile")
```

## 注意事项

1. **不需要额外权限**：使用 `getExternalFilesDir()` 无需申请存储权限
2. **日志敏感信息**：日志中可能包含 URL、错误信息等，分享时注意隐私
3. **性能影响**：DEBUG 级别日志较多，生产环境建议使用 INFO 或更高级别
4. **磁盘空间**：默认保留 7 天，注意磁盘空间限制

## 故障排查

### 日志文件未生成

1. 检查 `setupLogging()` 是否在 `onCreate()` 中调用
2. 检查日志目录权限：`getExternalFilesDir()` 应该总是有权限
3. 查看 Android Logcat 中是否有错误信息

### 无法访问日志文件

1. 确认设备已连接且 USB 调试已启用
2. 使用 `adb devices` 确认设备连接
3. 尝试使用 Android Studio 的 Device File Explorer

### 日志文件过大

1. 调整日志级别为 INFO 或更高
2. 减少日志保留天数
3. 定期清理旧日志文件
