# RealWorld Weather Acoustics 人工操作入口

这份文档给第一次接触本插件的人使用。目标是用最短路径在 Wwise 里听到 v0.3 hybrid slice：用高质量 Audio File Source/loop 提供天气主体，再由 `RealWorld Weather Acoustics Effect` 添加几何与材质交互；也保留 v0.2 Source 的程序化风/雨回归入口。

当前版本是 `v0.3 hybrid slice`，固定支持：

- Wwise `2023.1.19.8928`
- Windows x64 / Release / Visual Studio 2022 vc170
- 物理启发的程序化风声与改进雨声
- Metal、Wood、Glass、Tile 四种表面响应
- 最多 8 个圆形 `SphereProxy` Feature，其中最多 4 个进入当前 DSP Active Set
- 单个 Listener 点和 Yaw 箭头
- 立体声输出
- Wwise Authoring 内嵌 2D Preview
- 新增 Effect `PluginID=31002`，可挂在 Sound、Actor-Mixer 或 Bus 上
- Runtime C ABI scene `Set/Get/Clear`、Diagnostics `Reset/Get`，8 槽 scene 与 Native Host 三态 SoundBank matrix

它还不包含 Unity/Unreal Adapter、完整 Deflector/Aperture 风场、雷暴、Listener Path、Capture/Replay、Monitor、Ambisonics、高级 DSP 参数或人工主观听感验收。当前听感目标是“物理启发、可调、可回归”，不是实时全物理仿真；自动生成的 WAV、频谱、Profiler 或 smoke 指标也不等同于真人主观听感批准。

v0.3 的主路径是混合音频：保留 Source `PluginID=31001` 和旧 Bank ABI，新增 Effect `PluginID=31002`。请在 Wwise 中导入高质量 rain/wind/ambience 素材并设置循环 Audio File Source，再在 Sound、Actor-Mixer 或 Bus 上添加 `RealWorld Weather Acoustics Effect`。Effect 输入素材负责声音主体，插件只根据 `InputRole`、`WetMix`、listener/weather 和显式 Feature 添加几何材质交互。详情见 `docs/V0_3_HYBRID_AUDIO_PLAN.md`。

下文路径使用已经验证过的开发机位置。若仓库或 Wwise 安装在其他目录，只需替换产品根目录和 `$wwise`。

## 先选择你的入口

| 目的 | 从哪里开始 |
| --- | --- |
| 只想尽快听到高质量雨声 + 几何 Effect | 安装 DLL/XML，然后打开仓库内的 `WwiseSmoke` 工程，播放 `RWWA_Demo_Heavy_Rain_Puddles` 或事件 `Play_RWWA_Demo_Heavy_Rain_Puddles` |
| 只想回归程序化 Source | 打开 `RWWA_Smoke`，播放活动 Source `RWWA_Smoke_Source` |
| 在自己的 Wwise 工程中使用 v0.3 Effect | 导入/循环 Audio File Source，在 Sound、Actor-Mixer 或 Bus 上添加 `RealWorld Weather Acoustics Effect` |
| 在自己的 Wwise 工程中使用 | 安装插件，在 Sound SFX 上添加 `RealWorld Weather Acoustics` Source |
| 不启动 Wwise 做 DSP 回归 | 运行 `Build\Core\bin\Release\rwwa_offline_renderer.exe` 的三个 preset |
| 验证插件确实在 Wwise 中运行 | 执行 `Scripts\Smoke-WwiseAuthoring.ps1` |
| 修改源码、重新编译 | 阅读 `README.md` 和 `docs/BUILD_AND_INSTALL.md` |

## 五分钟快速试听

### 1. 安装 Authoring 插件

先关闭所有 Wwise Authoring 窗口，然后在 PowerShell 中执行：

```powershell
Set-Location 'D:\Tool\WwisePlugin_RealWorldWeatherSound'
$wwise = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928'

# 先预览将要复制的文件，不修改 Wwise。
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise

# 确认输出只有 DLL 和 XML 后，正式安装。
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise -Apply
```

脚本只会把以下两个文件复制到 Wwise Authoring；Source 和 Effect 都由这组 DLL/XML 暴露：

```text
Authoring\x64\Release\bin\Plugins\RealWorldWeatherAcoustics.dll
Authoring\x64\Release\bin\Plugins\RealWorldWeatherAcoustics.xml
```

安装脚本会校验 staging hash；如果目标位置已有同名文件，会先备份到 `Artifacts\InstallBackup`。最新已验证备份目录是 `Artifacts\InstallBackup\20260722T094813054Z`。

### 2. 打开现成的试听工程

打开：

```text
D:\Tool\WwisePlugin_RealWorldWeatherSound\WwiseSmoke\RealWorldWeatherAcousticsSmoke\RealWorldWeatherAcousticsSmoke.wproj
```

在 Actor-Mixer Hierarchy 中优先找到持久雨声 demo：

```text
Default Work Unit
└─ RWWA_Demo_Heavy_Rain_Puddles
   └─ RWWA_Demo_Heavy_Rain_Puddles_Audio
      └─ Effect: RWWA_Demo_Weather_Geometry_Effect
```

也可以在 Events 中播放：

```text
Play_RWWA_Demo_Heavy_Rain_Puddles
```

该 Sound 使用 30 秒真实雨声 WAV 作为 Wwise 标准 `AudioFileSource`，Effect 插在 Sound 上。播放它可以验证“输入素材负责主体雨声，插件负责几何/材质响应”的目标路径。

程序化 Source 回归入口仍保留在：

```text
Default Work Unit
└─ RWWA_Smoke
```

选择 `RWWA_Smoke` 后，在 Contents Editor 中找到它的活动 Source `RWWA_Smoke_Source`。使用 Wwise Transport 播放；双击 Source 行即可打开 `RealWorld Weather Acoustics` 设置界面。

如果打开工程时提示插件缺失，先完全退出 Wwise，重新执行上一节的安装命令，再启动 Wwise。

### 测试雨声素材来源

测试 WAV 由用户提供的 Envato preview MP3 截取前 30 秒生成：

```text
C:\Users\Administrator\Downloads\Envato_Rain_Previews\02_Heavy_Rain_With_Puddles_YDZSTXK_preview.mp3
```

当前仓库将生成后的测试 WAV/WEM 作为 smoke 工程测试资产分发，便于 fresh clone 后直接试听和跑自动化验证；发布者需要自行确认 Envato preview 许可是否覆盖这种仓库分发方式。仓库内持久测试 WAV 为：

```text
WwiseSmoke\RealWorldWeatherAcousticsSmoke\Originals\SFX\RWWA_Heavy_Rain_Puddles_30s.wav
```

格式证据：48 kHz、stereo、PCM24、30.0 秒、`8640102` bytes、SHA-256 `1fa150708de00627796a4e3963a9becd97595e01e5e769e6a3b0a5d1cf076adc`。准备脚本为 `Scripts\Prepare-RainTestAsset.ps1`，持久 Wwise 对象导入脚本为 `Scripts\Import-WwiseRainDemo.ps1`，最终导入报告是 `Build\WwiseSmoke\import-rain-demo-20260722T121122186Z.json`。`Build\WwiseSmoke\PersistentImport\` 仍只是临时导入工作目录，不作为人工入口。

### 3. 先听三个 Preset

建议保持 Transport 播放，依次点击插件界面上方的三个按钮：

1. `Open Wind`：关闭 Geometry Response，只听没有 Feature 的基础风场。
2. `Rain on Metal`：在 Listener 前方放置一个 Metal 圆形表面，只让它参与雨声响应。
3. `Wind + Rain Ring`：在 Listener 周围放置 Metal、Wood、Glass、Tile 四个 Feature，Mask 为 `Rain + Wind`。

Preset 会写入和 SoundBank/runtime 相同的生产 PropertySet，不是 Preview-only 数据。

快速 A/B 建议：

- 在 `Open Wind` 中调整 `Wind Speed`、`Wind Direction`、`Wind Gustiness`。
- 在 `Rain on Metal` 中拖动 Listener 或 Feature，比较近距离雨打金属变化。
- 在 `Wind + Rain Ring` 中把某个 Feature 的 Mask 在 `Rain`、`Wind`、`Rain + Wind` 之间切换。

## 2D Preview 怎么操作

Canvas 是 runtime `SphereProxy` 的俯视图，不是另外一套声音模拟。它编辑的就是 Source DSP 和 SoundBank writer 使用的参数。

```text
Z (+forward)
    ↑
    │     圆形 = Feature/SphereProxy
    │     红点 = Listener
    │     箭头 = Listener Yaw
    └────────────→ X (+right)
```

### 鼠标和按钮操作

- 拖动红色 Listener 点：修改 `Listener X/Z`。
- 拖动箭头末端的小圆点：修改 `Listener Yaw`。
- 点击一个 Feature 圆：选中它。
- 拖动选中的 Feature 圆心：修改该 Feature 的 `X/Z`。
- 拖动选中圆右侧的黄色半径手柄：修改 `Radius`。
- 点击 `Add Feature`：追加一个圆形 Feature，最多 8 个。
- 点击 `Delete Feature` 或按 Delete：删除当前选中 Feature；后续槽位会左移。
- 右侧 `Selected feature` 区域可精确编辑 `X/Y/Z/Radius/Profile/Mask/Priority`。
- 文本框在失去焦点时提交；输入后按 Tab 或点击别处。
- `Geometry enabled` 使用 Wwise 官方 populate table 绑定，由 Wwise Host 同步 checkbox 和原生 Undo；其他数值控件由插件 GUI 解析、规范化并写入同一套 PropertySet。

Yaw 0° 指向 `+Z`；正角度朝 `+X` 方向旋转。`Y` 不显示成 2D 位移，但仍会参与三维距离计算。

## 参数说明

### Runtime、Weather 和 Listener

| 参数 | 含义 | 常用值 |
| --- | --- | --- |
| Duration | Source 的播放时长，单位秒 | 60 |
| Master gain (dB) | 插件总输出增益 | -18 到 -6 |
| Rain intensity | 降雨强度，范围 0–1 | 0.25、0.75、0.8 |
| Wind speed | 风速，单位 m/s，范围 0–40 | 10、12、14 |
| Wind direction | 顶视图风向角，范围 0–360°；0° 表示朝 +Z 流动，正角朝 +X | 0、45 |
| Wind gustiness | 阵风/湍流强度，范围 0–1 | 0.45、0.65 |
| Seed | 确定性随机种子；相同输入便于 A/B | 1337 |
| Geometry enabled | 是否计算已配置 Feature 的表面/风响应 | 开启 |
| Listener X/Y/Z | 虚拟 Listener 世界坐标 | 从 0 开始 |
| Listener yaw | Listener 朝向角 | 0° |
| Feature count | 启用前多少个固定槽位，范围 0–8 | 0、1、4 |

### Selected feature

| 参数 | 含义 |
| --- | --- |
| X/Y/Z | Feature 中心位置 |
| Radius | 圆形/SphereProxy 半径，runtime 最小 clamp 为 0.2 |
| Profile | 材质响应编号，见下表 |
| Mask | 是否参与雨/风模块 |
| Priority | 候选重要性，范围 0–1000 |

Profile 数字映射：

| 值 | Profile |
| ---: | --- |
| 0 | Metal |
| 1 | Wood |
| 2 | Glass |
| 3 | Tile |

Mask 数字映射：

| 值 | 含义 |
| ---: | --- |
| 0 | Disabled，不参与天气声学 |
| 1 | Rain，只参与雨声响应 |
| 2 | Wind，只参与风声响应 |
| 3 | Rain + Wind，同时参与雨声和风声 |

当有效 Feature 多于 4 个时，Core 根据 Priority、Radius 和 Listener 到表面的距离计算 selection score，只让最高的 4 个进入当前 DSP Active Set。因此调高 Priority 会增强被选中的机会，但距离和半径仍会共同参与。

## 离线 DSP 试听

构建完成后，可以直接生成三个 WAV，不启动 Wwise：

```powershell
Set-Location 'D:\Tool\WwisePlugin_RealWorldWeatherSound'

& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset open-wind --output Build\Core\Fixtures\open_wind.wav
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset rain-metal --output Build\Core\Fixtures\rain_metal.wav
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset weather-ring --output Build\Core\Fixtures\weather_ring.wav
```

这些 WAV 复用同一个 Core/DSP，适合做确定性回归和快速 A/B。它们不能证明 Wwise Authoring 插件加载、SoundBank 序列化或游戏 Runtime 链路。

## 在自己的 Wwise 工程里使用 v0.3 Effect

完成插件安装并重启 Wwise 后：

1. 导入一段高质量 rain、wind 或天气 ambience WAV/WEM。
2. 在 Sound SFX 中使用 Wwise 标准 Audio File Source 播放该素材，并设置循环或 streamed loop。
3. 在该 Sound、上层 Actor-Mixer 或目标 Bus 的 Effects 链上添加 `RealWorld Weather Acoustics Effect`。
4. 设置 `InputRole`：`Rain=0`、`Wind=1`、`Generic=2`；不确定素材类型时使用默认 `Generic`。
5. 设置 `WetMix`：`0` 为透明干声，逐步提高到需要的几何响应量。
6. 使用 2D canvas 拖动 Listener 点和 yaw 手柄。
7. 点击 `Add` / `Delete` 管理圆形 Feature；拖圆心调 `X/Z`，拖黄色 handle 调 `Radius`。
8. 在右侧逐项编辑 `Profile`、`Mask`、`Priority` 和 `Y`。

Effect 当前实际参数是：

| 参数 | 范围 | 默认 |
| --- | --- | --- |
| InputRole | Rain=0 / Wind=1 / Generic=2 | Generic=2 |
| WetMix | 0..1 | 0 |
| ResponseGainDb | -24..12 dB | 0 |
| TransientSensitivity | 0..1 | 0.5 |
| Feature slots | 8 组 X/Y/Z/Radius/Profile/Mask/Priority | 固定默认环形 |

`Priority` 是 `0..1000` 的数值权重，不是四档枚举。当前没有 `EnvelopeSensitivity`、频带权重或 smoothing 控件；这些属于后续高级 DSP 参数。

如果游戏或 Host 没有通过 Runtime C ABI 提交外部 scene，Effect 会回退到 Bank/Authoring 中保存的 listener、weather 和 Feature 槽。

## 在自己的 Wwise 工程里创建 Source

完成插件安装并重启 Wwise 后：

1. 在 Actor-Mixer Hierarchy 中创建一个 Sound SFX。
2. 给该 Sound 添加或替换 Source Plug-in。
3. 在 Source Plug-ins 列表中选择 `RealWorld Weather Acoustics`。
4. 双击 Source 打开插件设置页。
5. 先点击 `Wind + Rain Ring`，使用 Transport 播放。
6. 确认能听到声音后，再按场景修改 Weather、Listener 与 Feature 参数。
7. 保存 Wwise 工程。

不同 Wwise 布局或语言下菜单位置可能不同。这个入口创建的是 Source Plug-in；v0.3 主路径请使用上一节的 Effect Plug-in。

## 推荐的人工验收流程

### 风声基础层

1. 选择 `Open Wind`。
2. 把 `Wind Speed` 从 0 提到 10 或 14。
3. 改变 `Wind Direction`，检查左右声像方向是否连续变化。
4. 把 `Wind Gustiness` 从 0 改到 1，检查包络起伏是否增强。

### 材质差异

1. 选择 `Rain on Metal`。
2. 保持 Listener 与 Feature 位置不变。
3. 依次把 Profile 改为 0、1、2、3。
4. 每次修改后把焦点移出文本框，听撞击瞬态和共振尾音差异。

### Feature 编辑

1. 选择 `Wind + Rain Ring`。
2. 点击一个圆形 Feature。
3. 拖动圆心，确认声像和主导材质随位置变化。
4. 拖动黄色半径手柄，确认贡献范围变化。
5. 点击 `Add Feature` 添加第 5 个圆，再点击 `Delete Feature` 删除它。

### Mask A/B

1. 选择 `Wind + Rain Ring`。
2. 选中一个 Feature。
3. 把 Mask 依次设为 1、2、3。
4. 预期结果是 Rain-only、Wind-only、Rain+Wind 有可辨识差异，且无爆音或静音异常。

### 稳定复现

1. 固定 Seed、Weather、Listener 和 Feature 参数。
2. 停止并重新播放。
3. 相同构建/工具链下，结果应保持确定性，便于做听感 A/B 和离线回归。

## 自动化 Wwise Smoke

自动 smoke 会复制仓库内 fixture 工程到 `Build\WwiseSmoke\Projects\<timestamp>` 的 disposable 目录，启动这份副本，静态验证持久雨声 demo 模板对象，复用 `RWWA_Demo_Heavy_Rain_Puddles` 上的既有 `RWWA_Demo_Weather_Geometry_Effect` 做 Effect 播放测试，并创建临时程序化 Source 回归对象。模板工程不变，测试改动只存在于 disposable copy 中。运行前必须关闭其他 Wwise 实例：

```powershell
Set-Location 'D:\Tool\WwisePlugin_RealWorldWeatherSound'

& .\Scripts\Smoke-WwiseAuthoring.ps1 `
    -WwiseRoot 'E:\WwiseSoft2023\Wwise_2023.1.19.8928' `
    -PythonWithWaapi 'D:\Tool\Wwise_mcp\.venv\Scripts\python.exe'
```

`-PythonWithWaapi` 只用于测试，需要能导入 `waapi-client`；手工使用插件和正常构建不依赖它。报告和 Profiler 文件输出到：

```text
Build\WwiseSmoke
```

最终 v0.3 证据文件是：

```text
Build\WwiseSmoke\wwise-authoring-smoke-20260722T123034276Z.json
Build\WwiseSmoke\wwise-authoring-smoke-20260722T123034276Z.prof
```

成功报告至少应满足：

- `success = true`
- wrapper/client `success = true`
- `requireRetainedRainDemo = true`
- 持久雨声 demo 模板验证通过：`RWWA_Demo_Heavy_Rain_Puddles`、`RWWA_Demo_Heavy_Rain_Puddles_Audio`、`RWWA_Demo_Weather_Geometry_Effect`、`Play_RWWA_Demo_Heavy_Rain_Puddles` 存在且引用正确
- `effectObjectSet.mode = existing-template`，说明 smoke 直接读取并播放模板持久对象，不再创建重复临时 Sound
- Effect 输入 WAV 为 `RWWA_Heavy_Rain_Puddles_30s.wav`，48 kHz stereo PCM24，30.0 秒，SHA-256 `1fa150708de00627796a4e3963a9becd97595e01e5e769e6a3b0a5d1cf076adc`
- fixture 工程 unchanged，disposable copy matches fixture，disposable 工程 removed
- GUI smoke 找到 Wwise-populated `GeometryEnabled` checkbox、Add/Delete 按钮和 Canvas
- Source `inspectorTextMatchesStablePreset = true`，13 个可见 Inspector 文本匹配 stable preset：`60/0.75/24681357/20/4 / 8/14/35/0.65/Feature1 0,6,2.5,3,10`
- Add 完整默认值断言为 true
- Effect Priority `10 -> 107 -> Undo 10`
- Geometry `true -> false` 后 Undo 恢复
- Add、Feature 圆心拖动、半径拖动、Delete button、Delete key 均有 Undo 还原证据；Feature move/resize 均使用 window-message 路径并 converged；最终 smoke 合计覆盖 Add/drag/radius/Delete 和 6 个 Undo gate
- Source SoundBank 生成与 Authoring 参数序列化通过：69 参数、`273`-byte 参数块，保留 261-byte legacy ABI 回归
- Effect SoundBank 生成与 Authoring 参数序列化通过：71 参数、`281`-byte 参数块，Baseline/InputRoleWetGeometry/WetZero 三种 bank variant，fixture manifest 写入 `Build\NativeHost\Fixture\20260722T123034276Z\RWWA_Effect_Fixture.json`
- `transportState = playing`
- Source 与 Effect 各存在 1 个启动且非虚拟的 Voice、各 1 条 CPU row
- Source CPU `0.1414999962 ms`、峰值 `-26.45691872 dB`；Effect CPU `0.1155999973 ms`、峰值 `-16.77216339 dB`
- assertion groups 为 Source `6/6`、Effect `7/7`、Shared `1/1`；GUI `36/36`
- `profilerCaptureSaved = true`；最终 `.prof` 为 `745058` bytes

## Native Host Smoke

v0.3 还保留了独立 Native SoundEngine Host fixture。构建 Host：

```powershell
Set-Location 'D:\Tool\WwisePlugin_RealWorldWeatherSound'
$wwise = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928'

& .\Scripts\Build-NativeHost.ps1 -WwiseRoot $wwise
```

使用 Authoring smoke 生成并保留的 fixture 后，三态 Host matrix 均已通过：

```text
Build\NativeHost\native-host-rain-changed-20260722T123034276Z.json
Build\NativeHost\native-host-rain-wet-bypass-20260722T123034276Z.json
Build\NativeHost\native-host-rain-geometry-disabled-20260722T123034276Z.json
```

| 场景 | Bank / runtime scene | `-Expectation` | 最终诊断 |
| --- | --- | --- | --- |
| Wet response changed | Baseline + `scene.example.json` | `changed` | 110 blocks / 56320 frames；runtime 110，fallback 0；max input `0.230743408`，max output `0.227470636`，max wet diff `0.0136105493` |
| WetMix=0 transparent | WetZero + `scene.example.json` | `wet-bypass` | 112 blocks / 57344 frames；112/112 wet-bypass；input=output `0.230743408`，max wet diff `0` |
| Runtime GeometryOff transparent | **Baseline** + `scene.disabled.example.json` | `geometry-disabled` | 110 blocks / 56320 frames；110/110 geometry-disabled；input=output `0.230743408`，max wet diff `0` |

GeometryOff 特意使用 Baseline bank，只通过 disabled runtime scene 关闭几何，避免 bank 与 runtime 双重关闭。三份报告都使用 runtime scene、authored fallback 为 0，完成 scene `Set/Get/Clear` 的 89-field full-payload roundtrip（mismatch 0）、31001/31002 注册、bank load、PostEvent、render 0 failures、non-finite 0 和 clean term。

Runtime Diagnostics V1 是 96-byte 结构，通过 `RWWA_RuntimeDiagnostics_ResetV1` / `GetV1` 读取 execute/frame、runtime/fallback、wet-bypass、geometry-disabled、revision、input/output/wet-difference peaks 和 non-finite sample count。Get 只返回 coherent snapshot；publish/reset handshake 使用 sequential consistency，Get 最后观测 generation。五个 `last*` 字段通过 no-wait try-commit 一次提交完整 per-block tuple；争用时 tuple 可落后于累计 counters/max/generation，但绝不会混合不同 block，未提交 tuple 的 block 仍继续累加这些累计量。与 publish/reset 重叠时 Get/Reset 返回 `BUSY`，控制线程应重试；确定性 race、forced-contention 与 multi-writer encoding 测试覆盖这些合同。Bank/Authoring 参数只在 Runtime API 返回 `UNCLAIMED` 且该实例此前从未 claim runtime scene 时回退；first claim `BUSY` 不回退，已有 runtime snapshot 时继续使用保留值。正式 `-Expectation` 模式为 `changed`、`wet-bypass`、`geometry-disabled`；`transparent` 仅保留 legacy 兼容。

门禁也保留了错误 expectation 负例覆盖：非零 wet response 不能伪装成 `wet-bypass`，geometry-disabled 的透明输出也不能伪装成 `wet-bypass`。这些负例以 code 52、stage `diagnostics-assertions` 失败，证明差值与原因计数器都参与门禁。

## 常见问题

### Wwise 中找不到插件

- 确认 Wwise 版本严格为 `2023.1.19.8928`。
- 确认 DLL 与 XML 同时位于 `Authoring\x64\Release\bin\Plugins`。
- 完全退出并重新启动 Wwise；安装时不要让 Wwise 保持运行。
- 重新执行安装脚本的 dry-run，检查源文件与目标路径。

### 能创建 Source，但没有声音

- 确认 `Master gain` 没有设得过低。
- 如果在 `Open Wind` 中测试，确认 `Wind speed > 0`。
- 如果在雨声场景中测试，确认 `Rain intensity > 0`。
- 确认 Duration 尚未结束，然后重新触发 Transport。
- 如果只有 Feature 响应缺失，确认 `Geometry enabled`、`Feature count` 和 Mask 不是 0。

### Feature 拖动后听感没有明显变化

- 使用 `Rain on Metal` 或 `Wind + Rain Ring`，不要使用 `Open Wind`。
- 确认选中的是 Feature 圆心，不是 Listener 点或 Yaw 手柄。
- 检查 Radius、Priority 与 Mask。
- 若 Feature 多于 4 个，只有 Active Set 中最高分的 4 个进入 DSP。

### Smoke 提示 Wwise 已经运行

这是保护机制。脚本不会接管或关闭用户已经打开的 Wwise。保存工作并退出 Wwise 后重新运行。

### 想验证游戏 Runtime 或生成后的 SoundBank

当前 v0.3 已验证生成 bank 由独立 Native SoundEngine Host 加载、PostEvent、render 和 clean term，Runtime C ABI scene `Set/Get/Clear` roundtrip、Diagnostics `Reset/Get` 以及三态音频合同均通过。尚未完成的是 Unity/Unreal Adapter、游戏引擎生命周期接入、平台打包和人工主观听感验收。

## 进一步阅读

- `README.md`：工程总入口和构建命令
- `docs/BUILD_AND_INSTALL.md`：构建、staging、安装和 smoke 细节
- `docs/V0_3_HYBRID_AUDIO_PLAN.md`：v0.3 Hybrid Audio File Source + Geometry Effect 实施状态
- `docs/VALIDATION_REPORT.md`：当前版本的实机验证证据
- `docs/PRODUCT_PLAN.md`：产品边界与后续开发计划
