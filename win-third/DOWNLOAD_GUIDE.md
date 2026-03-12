# SoundTouch 下载备选方案

## 方案 1: 使用下载脚本（推荐）

```cmd
cd win-third
.\download_soundtouch.bat
```

这个脚本提供三种方式：
1. GitHub 直接下载（需要良好网络）
2. GitHub 代理下载（如果你有代理）
3. 手动下载（最可靠）

## 方案 2: 手动下载 ZIP

### 步骤

1. **下载源码**
   
   访问以下任一地址：
   - GitHub: https://github.com/soundtouch/soundtouch/archive/refs/heads/master.zip
   - 或在浏览器打开: https://github.com/soundtouch/soundtouch
     点击绿色 "Code" 按钮 → "Download ZIP"

2. **解压**
   
   将下载的 ZIP 文件解压到：
   ```
   D:\git\hxcvodplayer\win-third\soundtouch-src\
   ```
   
   **注意**: 如果解压后是 `soundtouch-master` 目录，需要将里面的所有文件移动到 `soundtouch-src` 目录。

3. **验证**
   
   确保存在以下文件：
   ```
   D:\git\hxcvodplayer\win-third\soundtouch-src\CMakeLists.txt
   D:\git\hxcvodplayer\win-third\soundtouch-src\source\
   D:\git\hxcvodplayer\win-third\soundtouch-src\include\
   ```

4. **继续编译**
   ```cmd
   cd win-third
   .\build_soundtouch.bat
   ```

## 方案 3: 使用 Git 代理

如果你有代理（如 7890 端口）：

```cmd
cd win-third
git -c http.proxy=http://127.0.0.1:7890 clone https://github.com/soundtouch/soundtouch.git soundtouch-src
```

## 方案 4: 使用国内镜像

Gitee 镜像（如果可用）：

```cmd
cd win-third
git clone https://gitee.com/mirrors/soundtouch.git soundtouch-src
```

## 验证下载

下载完成后，运行：

```cmd
dir soundtouch-src\CMakeLists.txt
```

应该显示该文件存在。

## 常见问题

### Q: 下载很慢或失败

**A**: 使用方案 2（手动下载 ZIP），这是最可靠的方式。

### Q: 解压后找不到 CMakeLists.txt

**A**: 检查是否解压到了子目录。正确的目录结构：
```
soundtouch-src/
  ├── CMakeLists.txt
  ├── source/
  └── include/
```

错误的结构：
```
soundtouch-src/
  └── soundtouch-master/  ← 多了一层
      ├── CMakeLists.txt
      └── ...
```

需要将 `soundtouch-master` 里的文件移到上一级。

### Q: Git 命令不存在

**A**: 使用手动下载方式，不需要 Git。

---

## 快速命令参考

```cmd
# 下载源码
cd d:\git\hxcvodplayer\win-third
.\download_soundtouch.bat

# 编译
.\build_soundtouch.bat

# 配置项目
.\configure_project.bat

# 或一键完成
.\install_all.bat
```

---

如有问题，请查看主 README.md 或项目文档。
