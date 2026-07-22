# RealWorld Weather Acoustics v0.2 Completed Fix Plan

## 文档状态

- 状态：已完成的历史执行计划
- 目标版本：v0.2
- 创建时间：2026-07-21（Asia/Shanghai）
- 完成证据：`docs/VALIDATION_REPORT.md` 中的 `2026-07-22T03:56:23Z` canonical smoke
- 适用工程：`D:\Tool\WwisePlugin_RealWorldWeatherSound`
- 兼容基线：Wwise `2023.1.19.8928`、Windows x64、Visual Studio 2022/vc170、Release

本文保留 v0.2 修复的目标、执行结果和未覆盖边界，供后续里程碑回看。它不是当前待办清单。

## 一、v0.1 问题基线

v0.1 已证明 Wwise 2023.1 能加载、创建、播放和 Profile 该 Source Plug-in，但两个核心用户价值未完成。

### 1. Authoring 2D Preview 只能看，不能编辑

v0.1 Canvas 中的圆形 Feature 只能被点击选中，不能拖动、添加、删除或通过 Canvas 改半径。`FeatureCount` 只是启用前 N 个固定槽位，不等同于用户可理解的 Add/Delete。

v0.2 已修复：Canvas 现在支持 Feature Add、圆心拖动、半径手柄拖动、Delete 按钮、Delete 键，并通过 Wwise Undo gate 验证 6 个可撤销编辑分支。

### 2. 默认声音缺少风层，雨声合成质感不足

v0.1 DSP 只有 `RainSynth`，没有风速、风向、阵风、风响应 Mask 或风声合成层。插件名称和 Weather 定位会让用户期待风场能力，但 v0.1 二进制实际上只输出雨声。

v0.2 已修复为物理启发的程序化风雨原型：新增风速、风向、阵风参数，新增 Wind layer，保留并改进 Rain layer。当前目标仍是可调、可回归、可用于原型验证，不是商业最终听感或全物理风雨仿真。

## 二、完成范围

### 已完成

| 要求 | 状态 | 证据 |
| --- | --- | --- |
| 固定 8 槽 Feature Add/Delete/Move/Radius 编辑 | 完成 | 最终 smoke 验证 Add、center drag、radius drag、Delete button、Delete key |
| Canvas 写入生产 PropertySet | 完成 | GUI 和 SoundBank writer 使用同一 `FeatureN*`/Weather 属性；非 Preview-only 数据 |
| Listener 点与 Yaw 箭头保留拖动 | 完成 | 现有 Preview 能力保留；无 Listener Path |
| 新增 `WindSpeed`、`WindDirectionDegrees`、`WindGustiness` | 完成 | 参数 ID 66/67/68，`NUM_PARAMS = 69` |
| ResponseMask 扩展为 0/1/2/3 | 完成 | Disabled、Rain、Wind、Rain + Wind |
| DSP 升级为雨层 + 风层 | 完成 | Core tests 记录 rain/wind/gust metrics |
| 三个 Authoring preset | 完成 | `Open Wind`、`Rain on Metal`、`Wind + Rain Ring` |
| 自动 QA 覆盖 Core、ABI、离线 WAV、脚本、构建、staging、Wwise smoke | 完成 | `LastTest.log` 5/5；最终 smoke `success = true` |
| 更新 README、USER_GUIDE、BUILD_AND_INSTALL、VALIDATION_REPORT | 完成 | 当前文档集已按 v0.2 边界更新 |

### 保留非范围

- 不做可变长 Wwise Inner Objects/ObjectStore。
- 不做游戏侧 C ABI、Scene Snapshot、Unity Adapter、Unreal Adapter 或 Native SoundEngine Host。
- 不做雷暴、闪电事件、Ambisonics、Capture/Replay、Monitor UI。
- 不引入录音素材包；v0.2 仍是程序化合成。
- 不改变 `CompanyID=64` / `PluginID=31001`。商业发布 ID 更换仍是独立 release gate。

## 三、Authoring 2D 编辑结果

v0.2 继续使用固定 8 槽 PropertySet，避免在本轮引入 Inner Objects 的保存、序列化和 WAAPI 复杂度。

| 操作 | 已实现行为 | 写入属性 |
| --- | --- | --- |
| `Add Feature` | 在 Listener 朝向前方约 4m 创建新 Feature，最多 8 个 | `FeatureCount`，新槽位 `FeatureN*` |
| 点击 Feature | 选中 Feature，并在 Inspector 显示该槽位参数 | 无 |
| 拖动 Feature 圆心 | 移动选中 Feature | `FeatureNX`、`FeatureNZ` |
| 拖动半径手柄 | 调整半径 | `FeatureNRadius` |
| `Delete Feature` 按钮 | 删除选中 Feature，后续槽位左移 | `FeatureCount` 与后续 `FeatureN*` |
| Delete 键 | 删除当前选中 Feature | `FeatureCount` 与后续 `FeatureN*` |
| 拖动 Listener | 移动 listener | `ListenerX`、`ListenerZ` |
| 拖动 Yaw 箭头 | 修改 listener 朝向 | `ListenerYawDegrees` |
| Geometry checkbox | 使用官方 Wwise populate table 绑定，由 Wwise 同步并创建原生 Undo | `GeometryEnabled` |

固定槽位规则保持不变：Add 写入第 `FeatureCount + 1` 个槽位；Delete 后后续槽位左移；`FeatureId` 当前仍是 `slot + 1` 的局部身份语义，不能作为外部稳定 ID。

### Undo 验证

最终 canonical smoke 通过了 6 个 Undo gate：

- `GeometryEnabled`：`true -> false` 后 Undo 恢复 `true`。
- `Add Feature`：`FeatureCount 4 -> 5` 后 Undo 恢复第 5 槽默认值。
- `Move Feature`：`Feature5X/Z` 改变后 Undo 恢复。
- `Resize Feature`：`Feature5Radius 2.0 -> 3.515650987625122` 后 Undo 恢复。
- `Delete Feature`：`FeatureCount 5 -> 4` 后 Undo 恢复第 5 槽属性。
- `Delete key`：键盘删除后 Undo 恢复属性。

同一次 smoke 还验证 Add 的完整默认值、13 个可见 Inspector 文本、Wwise 实际 SoundBank 生成与 Authoring 参数序列化。

## 四、参数与 ABI 结果

新增参数追加到既有枚举尾部，旧参数 ID 和旧序列化顺序保持不变。

| 参数 | ID | 默认值 | 范围 | 含义 |
| --- | ---: | ---: | --- | --- |
| `WindSpeed` | 66 | `12.0` | `0..40` | 风速，单位 m/s |
| `WindDirectionDegrees` | 67 | `0.0` | `0..360` | 顶视图风向 |
| `WindGustiness` | 68 | `0.55` | `0..1` | 阵风/湍流强度 |

`Tests/SourceParamsBankContractTests.cpp` 验证：

- 261 字节 legacy bank 参数块可读取。
- 273 字节 current bank 参数块可读取。
- 260、262、272、274 字节参数块会拒绝。
- legacy block 保留旧字段，并把 `WindSpeed`、`WindDirectionDegrees`、`WindGustiness` 强制为 0。
- current block 按追加字段读取风参数。

这个测试是参数 ABI 合同测试。canonical smoke 已进一步验证 Wwise 实际 SoundBank 生成与 Authoring 参数序列化，但仍不等同于生成 bank 由独立 Native SoundEngine Host 加载/执行。

## 五、DSP 与 Preset 结果

v0.2 的声音边界是“程序化、受物理启发的风雨天气原型”：

- `RainIntensity = 0` 时可输出 wind-only。
- `WindSpeed = 0` 时可输出 rain-only。
- `ResponseMask = 1/2/3` 分别参与 Rain、Wind、Rain + Wind。
- 默认和 `Wind + Rain Ring` 使用 4 个 Feature，仍遵守固定 8 槽、Active4 的 DSP 边界。

三个最终 Authoring preset 名称为：

| Preset | 目标 | 参数摘要 |
| --- | --- | --- |
| `Open Wind` | 没有 Feature 的基础风场 | `FeatureCount=0`，`RainIntensity=0.0`，`WindSpeed=12.0`，`WindGustiness=0.55`，Geometry off |
| `Rain on Metal` | 单个 Metal 圆形表面的雨击响应 | `FeatureCount=1`，Metal，Mask `1` |
| `Wind + Rain Ring` | 多材质雨+风组合 | `FeatureCount=4`，Metal/Wood/Glass/Tile，Mask `3` |

旧计划中的 `Open Field Wind`、`Single Metal Roof Rain`、`Storm Multi-Material Ring` 是设计阶段名称；最终用户文档使用上表名称。

## 六、QA 结果

| 层级 | 状态 | 证据 |
| --- | --- | --- |
| Core/weather tests | 通过 | `Build/Core/Testing/Temporary/LastTest.log` 中 `rwwa_core_tests` passed |
| 参数 ABI tests | 通过 | `rwwa_source_params_bank_tests` 输出 `261-byte legacy and 273-byte current blocks` passed |
| Offline renderer | 通过 | `weather_ring.wav`、`open_wind.wav`、`rain_metal.wav` 生成成功 |
| Stage | 通过 | `Artifacts/stage-record.json` 记录 4 个 staged 文件 |
| Wwise Authoring smoke | 通过 | `wwise-authoring-smoke-20260722T035623735Z.json`：wrapper/client `success = true` |
| Wwise GUI smoke | 通过 | GUI assertions 全真；Geometry、13 个可见 Inspector 文本、Add 完整默认值、Add/drag/radius/Delete、Delete key 与 6 个 Undo gate 通过 |
| SoundBank serialization | 通过 | true/false bank 各 `457` bytes；`273`-byte 参数块在 bank offset `77` 唯一匹配；Geometry block offset `16` / bank offset `93`；true=`1` false=`0`；块只差 offset `16`；generation logs 仅 `Message`；Geometry restored true |
| Fixture isolation | 通过 | fixture project digest unchanged；copy matches fixture；disposable copy removed |
| Profiler evidence | 通过 | Transport playing、1 physical voice、OutputPeak `-28.6425437927246 dB`、Source CPU `0.1396999955177307 ms`、`.prof` `368867` bytes |

## 七、未完成验收与后续风险

- 未做真人主观听感验收。WAV、频谱、包络、RMS、OutputPeak 和 Profiler 数据只证明自动回归条件，不代表听感批准。
- 未做独立 Native SoundEngine Host 加载/执行生成 bank 的端到端运行。Wwise 实际 SoundBank 生成与 Authoring 参数序列化已通过 canonical smoke。
- 未做游戏侧 C ABI、Scene Snapshot、Custom Game Data/Native Registry、Unity/Unreal Adapter。
- 固定 8 槽删除左移会改变 `slot + 1` 派生身份。稳定外部 Feature ID 留到 Runtime Registry/Snapshot 里解决。
- 不包含雷暴、完整风物理解算、完整 Deflector/Aperture 风场、跨平台打包和商业 ID。

## 八、复现命令

从产品根目录执行：

```powershell
$wwise = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928'

& .\Scripts\Resolve-Environment.ps1 -WwiseRoot $wwise
& .\Scripts\Configure.ps1 -WwiseRoot $wwise
& .\Scripts\Build.ps1 -WwiseRoot $wwise
& .\Scripts\Test.ps1 -WwiseRoot $wwise
& .\Scripts\Stage.ps1
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise -Apply
& .\Scripts\Smoke-WwiseAuthoring.ps1 `
    -WwiseRoot $wwise `
    -PythonWithWaapi 'D:\Tool\Wwise_mcp\.venv\Scripts\python.exe'
```

`Install-WwiseAuthoring.ps1` 不带 `-Apply` 时是 dry-run。`Smoke-WwiseAuthoring.ps1` 会复制 fixture 到 disposable project，下游报告应继续证明 fixture unchanged 和 disposable removed。

## 九、完成定义状态

| 完成定义 | 状态 |
| --- | --- |
| Feature 可添加、删除、拖动和改半径 | 完成 |
| 交互写入同一套生产 PropertySet | 完成 |
| Listener 与 Yaw 交互无退化 | 完成 |
| Preset 能产生风/雨/组合原型 | 自动证据完成；真人听感未批准 |
| 新风参数进入 Authoring 与 DSP | 完成 |
| ResponseMask 0/1/2/3 行为进入实现与测试 | 完成 |
| v0.1 参数块兼容策略通过测试 | 完成 |
| 自动 QA 全部通过 | 完成 |
| 手工听感 QA 记录 | 未完成 |
| README、用户指南和验证报告与实现一致 | 完成 |
| 代码提交并 push 到 GitHub | 以仓库 Git history 与远端 `origin/main` 为最终发布证据；本文不固化 commit hash |
