# TaskbarMonitor

极简 Windows 系统托盘工具，显示 CPU 和内存占用率。

## 编译

在 PowerShell 中执行：

```powershell
Use-MSVC
cd C:\Users\Mi\TaskbarMonitor
.\build.bat
```

也可以双击 `build.bat` 编译。

## 使用

运行 `TaskbarMonitor.exe` 后，程序会出现在任务栏右下角通知区域。将鼠标移到图标上可以查看当前占用率；右键图标可以刷新、设置刷新频率或退出。

当前支持的刷新频率：1 秒、2 秒、5 秒。设置仅在本次运行期间有效。程序支持单例模式，重复启动不会创建多个托盘实例。


图标来源：mood-kid.svg，构建时生成 mood-kid.ico 并嵌入 EXE。

双击托盘图标会打开 Windows 任务管理器。
