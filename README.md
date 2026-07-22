# RealWorld Weather Acoustics — v0.3 hybrid slice

RealWorld Weather Acoustics 是面向 Wwise 的几何条件天气声学插件包。当前 v0.3 hybrid vertical slice 已完成：保留 Source Plug-in `PluginID=31001` 的 v0.2 程序化风/雨与 261/273-byte Bank ABI 回归，同时新增 Effect Plug-in `PluginID=31002`。推荐主路径是用 Wwise Audio File Source/streamed loop 提供高质量 rain/wind bed，再把 `RealWorld Weather Acoustics Effect` 挂到 Sound、Actor-Mixer 或 Bus 上，由 Effect 添加几何与材质交互。兼容基线固定为 Wwise `2023.1.19.8928`、Windows x64、Visual Studio 2022/vc170、Release。

当前还不是完整跨引擎产品，也不是实时全物理仿真：Unity/Unreal Adapter、完整 Deflector/Aperture 风场、雷暴、Capture/Replay、Monitor、Ambisonics、高级 DSP 参数和真人主观听感验收仍属后续里程碑。v0.3 已完成 Runtime Scene `Set/Get/Clear`、Runtime Diagnostics `Reset/Get`、8 槽 scene roundtrip、Effect Profiler smoke，以及 Native Host 三态音频合同 matrix：`Wet>0 changed`、`Wet=0 wet-bypass`、`GeometryOff geometry-disabled`。详细边界见 [产品计划](docs/PRODUCT_PLAN.md)、[v0.3 实施状态](docs/V0_3_HYBRID_AUDIO_PLAN.md) 与 [验证报告](docs/VALIDATION_REPORT.md)。

## 给使用者的操作入口

第一次使用请直接阅读 [中文人工操作指南](docs/USER_GUIDE_ZH.md)。它包含：

- 五分钟安装与试听路径
- 如何打开现成的 Wwise Smoke 工程，并试听持久化的高质量雨声 demo
- 如何在自己的 Wwise 工程创建 Source，或在 Audio File Source loop 上添加 Effect
- 2D Canvas、Listener、Yaw、Feature 添加/删除/拖动/半径手柄和三个 Preset 的实际操作
- 参数含义、推荐 A/B 流程与常见问题

最快入口：安装插件后打开 `WwiseSmoke\RealWorldWeatherAcousticsSmoke\RealWorldWeatherAcousticsSmoke.wproj`。优先选择 `RWWA_Demo_Heavy_Rain_Puddles` 或事件 `Play_RWWA_Demo_Heavy_Rain_Puddles` 试听 30 秒真实雨声素材 + `RealWorld Weather Acoustics Effect`；如需程序化 Source 回归，再选择 `RWWA_Smoke` 播放并双击 `RWWA_Smoke_Source` 打开 2D Preview。

持久雨声 demo 使用用户提供的 Envato preview MP3 前 30 秒制作，并将生成的测试 WAV/WEM 作为 smoke 工程测试资产随仓库分发，便于 fresh clone 后直接试听和跑自动化验证。准备脚本为 `Scripts\Prepare-RainTestAsset.ps1`，输出 `WwiseSmoke\RealWorldWeatherAcousticsSmoke\Originals\SFX\RWWA_Heavy_Rain_Puddles_30s.wav`：48 kHz stereo PCM24，`8640102` bytes，SHA-256 `1fa150708de00627796a4e3963a9becd97595e01e5e769e6a3b0a5d1cf076adc`。发布者需要自行确认 Envato preview 对仓库分发测试 WAV/WEM 的许可覆盖。持久导入入口是 `Scripts\Import-WwiseRainDemo.ps1`，最终导入报告为 `Build\WwiseSmoke\import-rain-demo-20260722T121122186Z.json`。

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
& .\Scripts\Build-NativeHost.ps1 -WwiseRoot $wwise
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

最终 v0.3 Authoring 证据是连续第二次通过的 `Build\WwiseSmoke\wwise-authoring-smoke-20260722T123034276Z.json` 和同名 `.prof`。该 run 记录 wrapper/client `success = true`、`requireRetainedRainDemo = true`、`effectObjectSet.mode = existing-template`，说明 smoke 直接读取并播放持久雨声模板对象而不是创建重复临时 Sound；GUI `36/36`、Source/Effect/Shared assertion groups `6/6`、`7/7`、`1/1`，Feature move/resize 均使用 window-message 路径且 converged，Priority `10 -> 107 -> Undo 10`。Source 为 69 参数、273-byte current block、1 physical voice、CPU `0.1414999962 ms`、峰值 `-26.45691872 dB`；Effect 为 71 参数、281-byte block、1 voice、CPU `0.1155999973 ms`、峰值 `-16.77216339 dB`。`.prof` 为 `745058` bytes。

## 离线 DSP 试听

不启动 Wwise 时，可以用同一套 Core/DSP 生成确定性 WAV：

```powershell
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset open-wind --output Build\Core\Fixtures\open_wind.wav
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset rain-metal --output Build\Core\Fixtures\rain_metal.wav
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset weather-ring --output Build\Core\Fixtures\weather_ring.wav
```

三个 preset 分别对应 Authoring 中的 `Open Wind`、`Rain on Metal`、`Wind + Rain Ring`。这些输出用于快速回归和 A/B，不替代人工听感评审。最终 fixture `Build\NativeHost\Fixture\20260722T123034276Z` 保留 Baseline、InputRoleWetGeometry、WetZero 三个 473-byte bank；每个内部 Effect block 均为 281 bytes，10/10 个复制产物的 source/destination size 与 SHA-256 一致，连同 manifest 共 11 个文件，并包含从雨声 WAV 生成的 `Media\528110025.wem`。独立 Native SoundEngine Host 已验证生成 bank 的加载、PostEvent、render、清理退出、89-field scene full-payload roundtrip 与三态音频合同，并用两份应失败的负例证明门禁不会伪通过。

Runtime Diagnostics V1 是 96-byte C ABI，导出 `RWWA_RuntimeDiagnostics_ResetV1` 与 `RWWA_RuntimeDiagnostics_GetV1`，提供 coherent snapshot、actual wet-difference 与 non-finite sample 计数；publish/reset handshake 使用 sequential consistency，Get 把 generation 作为最后一次并发观测。`lastRuntimeSceneRevision`、`lastInputPeak`、`lastOutputPeak`、`lastWetDifferencePeak`、`lastBlockUsedRuntimeScene` 通过无等待 try-commit 形成一个完整 block tuple：争用时可落后于累计 counters/max/generation，但不会混合不同 block，未取得 tuple 提交权的 block 仍继续累加这些累计量；确定性 forced-contention 和 multi-writer encoding 测试覆盖该合同。与 publish/reset 重叠时返回 `BUSY`，控制线程重试。外部 scene 只有在 API 返回 `UNCLAIMED` 且此前从未 claim 过 runtime scene 时才回退到 Bank/Authoring 参数；first claim 竞争返回 `BUSY` 时不会短暂回退，已保留 snapshot 时继续使用上一份 runtime scene。Host smoke 的正式 `-Expectation` 模式为 `changed`、`wet-bypass`、`geometry-disabled`，`transparent` 仅保留为 legacy 模式。

Source 31001 参数 ABI 回归覆盖 69 参数、261 字节 legacy block 和 273 字节 current block。Effect 31002 参数 ABI 回归覆盖 71 参数、281 字节 block；当前实际参数为 `InputRole`、`WetMix`、`ResponseGainDb`、`TransientSensitivity`、weather/listener、`FeatureCount` 与 8 组 `X/Y/Z/Radius/Profile/Mask/Priority`。

## 构建边界

- `Configure.ps1` 调用官方 `wp.py premake`，随后机械修补生成的 vc170 工程。所有插件输出与 intermediates 都进入本产品的 `Build\Wwise`；Runtime 插件库依赖也从这里解析。
- Wwise SDK 的官方 headers/libs 继续从 `-WwiseRoot` 引用，但配置和构建不会把新增文件写入 Wwise SDK/Authoring 目录。
- Authoring post-build 只把 Factory Header 放入 `Artifacts\Runtime\include\AK\Plugin`。
- `Build.ps1` 构建共享 Core、测试、Offline Renderer、Runtime 与 Authoring；`Build-NativeHost.ps1` 构建独立 Native SoundEngine Host。
- `Stage.ps1` 严格只归档清单中的 5 个文件：Authoring DLL/XML、runtime static lib、factory header、RuntimeAPI header。
- 安装脚本只处理 Authoring DLL/XML，不复制源码、完整构建树、PDB 或 Runtime 库，也不会在未指定 `-Apply` 时改动 Wwise。

完整说明见 [构建与安装文档](docs/BUILD_AND_INSTALL.md)。

> **发布前必须处理 ID：** 当前开发配置使用内部 `CompanyID=64`、Source `PluginID=31001` 和 Effect `PluginID=31002`。任何公开或商业发布前，必须替换为 Audiokinetic 正式分配且不会冲突的 ID，并重新生成、构建和验证全部产物。
