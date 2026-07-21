# RealWorld Weather Acoustics — v0.1 executable slice

RealWorld Weather Acoustics 是面向 Wwise 的几何条件天气声学 Source Plug-in。当前可执行切片实现雨声、四种表面响应、最多 8 个圆形 Feature、单 Listener + Yaw、共享离线/Wwise DSP，以及 Wwise 内嵌 2D Preview。兼容基线固定为 Wwise `2023.1.19.8928`、Windows x64、Visual Studio 2022/vc170、Release。

当前还不是完整跨引擎产品：游戏侧 C ABI/Scene Snapshot、SoundBank Native Host、Unity/Unreal Adapter、风、雷暴、Capture/Replay、Monitor 和 Ambisonics 属于后续里程碑。详细边界见 [产品计划](docs/PRODUCT_PLAN.md) 与 [验证报告](docs/VALIDATION_REPORT.md)。

## 一条龙构建与验证

从产品根目录运行：

```powershell
$wwise = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928'

& .\Scripts\Resolve-Environment.ps1 -WwiseRoot $wwise
& .\Scripts\Configure.ps1 -WwiseRoot $wwise
& .\Scripts\Build.ps1 -WwiseRoot $wwise
& .\Scripts\Test.ps1 -WwiseRoot $wwise
& .\Scripts\Stage.ps1
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise
```

最后一条命令默认是 dry-run，只展示 DLL/XML 的精确复制目标。确认 Wwise Authoring 已关闭后，只有显式加入 `-Apply` 才会备份同名旧文件并安装：

```powershell
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise -Apply
```

安装后可运行真实 Wwise Authoring/Transport/Profiler smoke。测试工具需要一个可导入 `waapi-client` 的 Python；它不是插件构建或运行时依赖：

```powershell
& .\Scripts\Smoke-WwiseAuthoring.ps1 `
    -WwiseRoot $wwise `
    -PythonWithWaapi 'D:\Tool\Wwise_mcp\.venv\Scripts\python.exe'
```

报告、日志和 `.prof` 全部写入 `Build\WwiseSmoke`。脚本拒绝接管已经打开的 Wwise，只关闭自己启动的进程。

## 构建边界

- `Configure.ps1` 调用官方 `wp.py premake`，随后机械修补生成的 vc170 工程。所有插件输出与 intermediates 都进入本产品的 `Build\Wwise`；Runtime 插件库依赖也从这里解析。
- Wwise SDK 的官方 headers/libs 继续从 `-WwiseRoot` 引用，但配置和构建不会把新增文件写入 Wwise SDK/Authoring 目录。
- Authoring post-build 只把 Factory Header 放入 `Artifacts\Runtime\include\AK\Plugin`。
- `Build.ps1` 构建共享 Core、测试、Offline Renderer、Runtime 与 Authoring；`Stage.ps1` 只归档清单中的四个最小文件。
- 安装脚本只处理 Authoring DLL/XML，不复制源码、完整构建树、PDB 或 Runtime 库，也不会在未指定 `-Apply` 时改动 Wwise。

完整说明见 [构建与安装文档](docs/BUILD_AND_INSTALL.md)。

> **发布前必须处理 ID：** 当前开发配置使用内部 `CompanyID=64` 和 `PluginID=31001`。任何公开或商业发布前，必须替换为 Audiokinetic 正式分配且不会冲突的 ID，并重新生成、构建和验证全部产物。
