# RealWorld Weather Acoustics — v0.2 executable slice

RealWorld Weather Acoustics 是面向 Wwise 的几何条件天气声学 Source Plug-in。当前可执行切片实现程序化、受物理启发的风声和改进雨声、四种表面响应、固定 8 槽圆形 Feature、当前 DSP Active4、单 Listener + Yaw、共享离线/Wwise DSP，以及 Wwise 内嵌 2D Preview。兼容基线固定为 Wwise `2023.1.19.8928`、Windows x64、Visual Studio 2022/vc170、Release。

当前还不是完整跨引擎产品，也不是实时全物理仿真：游戏侧 C ABI/Scene Snapshot、SoundBank Native Host、Unity/Unreal Adapter、完整 Deflector/Aperture 风场、雷暴、Capture/Replay、Monitor 和 Ambisonics 属于后续里程碑。v0.2 没有完成真人主观听感验收；WAV、频谱和 smoke 指标只作为回归证据。详细边界见 [产品计划](docs/PRODUCT_PLAN.md) 与 [验证报告](docs/VALIDATION_REPORT.md)。

## 给使用者的操作入口

第一次使用请直接阅读 [中文人工操作指南](docs/USER_GUIDE_ZH.md)。它包含：

- 五分钟安装与试听路径
- 如何打开现成的 Wwise Smoke 工程
- 如何在自己的 Wwise 工程创建 Source
- 2D Canvas、Listener、Yaw、Feature 添加/删除/拖动/半径手柄和三个 Preset 的实际操作
- 参数含义、推荐 A/B 流程与常见问题

最快入口：安装插件后打开 `WwiseSmoke\RealWorldWeatherAcousticsSmoke\RealWorldWeatherAcousticsSmoke.wproj`，选择 `RWWA_Smoke` 播放，再双击 `RWWA_Smoke_Source` 打开 2D Preview。

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

最终 v0.2 smoke 证据是 `Build\WwiseSmoke\wwise-authoring-smoke-20260722T035623735Z.json` 和同名 `.prof`。该 run 记录 wrapper/client `success = true`、GUI assertions 全真、fixture unchanged、copy matches fixture、disposable project removed、Geometry checkbox 通过 Wwise 官方 populate binding 完成 `true -> false` + Undo、13 个可见 Inspector 文本匹配 stable preset（`60/0.75/24681357/20/4 / 8/14/35/0.65/Feature1 0,6,2.5,3,10`）、Add 完整默认值、Add/drag/radius/Delete 与 6 个 Undo gate、Wwise 实际 SoundBank 生成和 Authoring 参数序列化、Transport playing、1 physical voice、`OutputPeak = -28.6425437927246 dB`、Source CPU `0.1396999955177307 ms`、`.prof` `368867` bytes。

## 离线 DSP 试听

不启动 Wwise 时，可以用同一套 Core/DSP 生成确定性 WAV：

```powershell
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset open-wind --output Build\Core\Fixtures\open_wind.wav
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset rain-metal --output Build\Core\Fixtures\rain_metal.wav
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset weather-ring --output Build\Core\Fixtures\weather_ring.wav
```

三个 preset 分别对应 Authoring 中的 `Open Wind`、`Rain on Metal`、`Wind + Rain Ring`。这些输出用于快速回归和 A/B，不替代人工听感评审。当前 smoke 已验证 Wwise 实际 SoundBank 生成与 Authoring 参数序列化；仍未验证生成 bank 由独立 Native SoundEngine Host 加载/执行。

参数 ABI 回归覆盖 261 字节 legacy block 和 273 字节 current block，证明 `SetParamsBlock` 的旧/新参数块读取合同；它不是 SoundBank Native Host 端到端验收。

## 构建边界

- `Configure.ps1` 调用官方 `wp.py premake`，随后机械修补生成的 vc170 工程。所有插件输出与 intermediates 都进入本产品的 `Build\Wwise`；Runtime 插件库依赖也从这里解析。
- Wwise SDK 的官方 headers/libs 继续从 `-WwiseRoot` 引用，但配置和构建不会把新增文件写入 Wwise SDK/Authoring 目录。
- Authoring post-build 只把 Factory Header 放入 `Artifacts\Runtime\include\AK\Plugin`。
- `Build.ps1` 构建共享 Core、测试、Offline Renderer、Runtime 与 Authoring；`Stage.ps1` 只归档清单中的四个最小文件。
- 安装脚本只处理 Authoring DLL/XML，不复制源码、完整构建树、PDB 或 Runtime 库，也不会在未指定 `-Apply` 时改动 Wwise。

完整说明见 [构建与安装文档](docs/BUILD_AND_INSTALL.md)。

> **发布前必须处理 ID：** 当前开发配置使用内部 `CompanyID=64` 和 `PluginID=31001`。任何公开或商业发布前，必须替换为 Audiokinetic 正式分配且不会冲突的 ID，并重新生成、构建和验证全部产物。
