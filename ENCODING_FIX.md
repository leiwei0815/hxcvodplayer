# Windows 控制台中文乱码解决方案

## 问题说明

在 Windows PowerShell 或命令提示符中运行脚本时，如果出现中文乱码，这是由于字符编码不匹配导致的。

### 两种脚本，两种编码要求

1. **批处理脚本 (`.bat` 文件)**: 需要 UTF-8 编码，并在脚本中添加 `chcp 65001`
2. **PowerShell 脚本 (`.ps1` 文件)**: 需要 **UTF-8 with BOM** 编码

## 已修复

## 已修复

### 批处理脚本
所有批处理脚本（`.bat`文件）现在都已经添加了 UTF-8 编码支持：
- `build_windows.bat`
- `quickstart_windows.bat`
- `check_windows_env.bat`

每个脚本的开头都添加了：
```batch
@echo off
chcp 65001 >nul 2>nul
```

这会自动将控制台切换到 UTF-8 编码（代码页 65001）。

### PowerShell 脚本

PowerShell 脚本已重新保存为 **UTF-8 with BOM** 编码：
- `setup_windows_deps.ps1`

## PowerShell 脚本乱码问题

### 方法 1: 临时修改 PowerShell 编码（推荐）

在运行脚本前，先执行：
```powershell
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001
```

然后运行脚本：
```powershell
.\quickstart_windows.bat
```

### 方法 2: 使用命令提示符（CMD）代替 PowerShell

1. 按 `Win + R`
2. 输入 `cmd` 并回车
3. 切换到项目目录：
   ```cmd
   cd /d d:\git\hxcvodplayer
   ```
4. 运行脚本：
   ```cmd
   quickstart_windows.bat
   ```

### 方法 3: 修改 PowerShell 配置文件

永久修改 PowerShell 的默认编码：

1. 打开 PowerShell 配置文件：
   ```powershell
   notepad $PROFILE
   ```

2. 如果文件不存在，先创建：
   ```powershell
   New-Item -Path $PROFILE -Type File -Force
   ```

3. 添加以下内容：
   ```powershell
   # 设置控制台编码为 UTF-8
   [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
   $OutputEncoding = [System.Text.Encoding]::UTF8
   chcp 65001 | Out-Null
   ```

4. 保存文件并重启 PowerShell

### 方法 4: 使用 Windows Terminal（最佳方案）

Windows Terminal 对 UTF-8 有更好的支持：

1. 从 Microsoft Store 安装 Windows Terminal
2. 打开 Windows Terminal
3. 进入设置 -> 配置文件 -> 默认值 -> 外观
4. 字体选择支持中文的字体（如：Microsoft YaHei Mono, Cascadia Code）
5. 运行脚本

### 方法 5: 检查文件编码

**批处理文件 (`.bat`)** 应保存为 **UTF-8**：

使用 VS Code：
1. 打开批处理文件
2. 查看右下角的编码显示
3. 如果不是 UTF-8，点击编码名称
4. 选择 "通过编码保存" -> "UTF-8"

使用记事本：
1. 打开批处理文件
2. 文件 -> 另存为
3. 编码选择 "UTF-8"
4. 保存

**PowerShell 文件 (`.ps1`)** 应保存为 **UTF-8 with BOM**：

使用 VS Code：
1. 打开 PowerShell 文件
2. 点击右下角的编码显示
3. 选择 "通过编码保存"
4. 选择 "**UTF-8 with BOM**"（重要！）
5. 保存

## 测试编码是否正确

### 测试批处理脚本

运行以下命令测试：
```cmd
check_windows_env.bat
```

如果看到：
```
==========================================
YXVodPlayer Windows 环境检查
==========================================
```

说明编码正确。

如果看到类似：
```
==========================================
YXVodPlayer Windows ?????
==========================================
```

说明仍有编码问题。

### 测试 PowerShell 脚本

运行以下命令测试：
```powershell
.\setup_windows_deps.ps1 -SkipVcpkgSetup
```

如果能正常运行并显示中文，说明编码正确。

如果出现 "The string is missing the terminator" 错误，说明需要保存为 UTF-8 with BOM。

说明仍有编码问题，请尝试上述方法。

## PowerShell 中运行批处理的最佳实践

```powershell
# 方法 1: 直接运行（脚本已包含编码处理）
.\quickstart_windows.bat

# 方法 2: 使用 cmd /c（推荐）
cmd /c quickstart_windows.bat

# 方法 3: 先设置编码再运行
chcp 65001
.\quickstart_windows.bat
```

## 替代方案：使用 PowerShell 脚本

如果批处理脚本的乱码问题难以解决，可以使用 PowerShell 版本：

```powershell
# 使用已提供的 PowerShell 脚本
.\setup_windows_deps.ps1
```

PowerShell 脚本（`.ps1` 文件）原生支持 UTF-8，不会出现乱码问题。

## 常见问题

### Q: PowerShell 脚本出现 "The string is missing the terminator" 错误？
A: PowerShell 脚本需要保存为 **UTF-8 with BOM** 格式。在 VS Code 中，选择 "通过编码保存" -> "UTF-8 with BOM"。

### Q: 批处理脚本和 PowerShell 脚本编码要求不同？
A: 是的！
- **批处理脚本 (`.bat`)**: UTF-8（无 BOM）+ `chcp 65001` 命令
- **PowerShell 脚本 (`.ps1`)**: UTF-8 with BOM

### Q: 为什么 PowerShell 会有乱码？
A: PowerShell 默认使用系统的活动代码页（中文 Windows 通常是 GBK/936），而现代开发工具（如 VS Code）默认使用 UTF-8 编码保存文件。

### Q: chcp 65001 是什么？
A: `chcp` 是 "change code page" 的缩写，65001 是 UTF-8 的代码页编号。

### Q: 为什么添加 `>nul 2>nul`？
A: 这会隐藏 `chcp` 命令的输出信息，使脚本输出更简洁。

### Q: 影响脚本功能吗？
A: 不影响。编码问题只影响显示，不影响脚本的实际功能。

## 推荐配置

**最佳实践：**

1. **使用 Windows Terminal**（最佳）
   - 完美支持 UTF-8
   - 现代化界面
   - 更好的性能

2. **配置 PowerShell**
   - 修改 `$PROFILE` 设置默认 UTF-8
   - 一劳永逸

3. **使用 VS Code 终端**
   - VS Code 的集成终端默认 UTF-8
   - 无需额外配置

## 验证修复

运行所有脚本验证中文显示正常：

```powershell
# 测试 1: 环境检查
.\check_windows_env.bat

# 测试 2: 构建脚本帮助
.\build_windows.bat

# 测试 3: 快速启动
.\quickstart_windows.bat
```

如果所有脚本都能正确显示中文，说明问题已解决！

---

**注意**: 如果你使用的是 Git Bash 或其他第三方终端，它们通常默认使用 UTF-8，不会有乱码问题。
