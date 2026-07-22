# v0.3 Hybrid Audio File Source + Geometry Effect

## 文档状态

- 状态：v0.3 hybrid vertical slice 已实现并通过当前自动化验证
- 日期：2026-07-22（Asia/Shanghai）
- 适用工程：`D:\Tool\WwisePlugin_RealWorldWeatherSound`
- 前置基线：Source Plug-in `PluginID=31001` 继续保留 69 参数、261-byte legacy block 与 273-byte current block 回归合同
- 本轮结果：新增独立 Effect `PluginID=31002`，用 Wwise Audio File Source/streamed loop 提供高质量 rain/wind bed，Effect 只添加几何与材质交互响应

## 一、核心结论

v0.3 采用混合音频路径：

```text
Wwise Audio File Source / Streamed Loop
    -> RealWorld Weather Acoustics Effect (PluginID=31002)
    -> Sound / Actor-Mixer / Bus routing
```

产品不再要求第一阶段由 Source 直接合成完整 rain/wind bed。高质量主体声音由 Wwise 标准 `Audio File Source`、循环素材、streamed WEM、Blend Container 或项目已有素材系统提供；Effect 根据输入素材和显式几何场景添加可回归的局部材质响应。

这不会废弃现有 Source：

- `RealWorld Weather Acoustics Source` 保留 `PluginID=31001`。
- Source 继续保留 69 参数，兼容 v0.1 `261` 字节 legacy block 与 v0.2 `273` 字节 current block。
- v0.3 不修改 31001 的参数顺序、旧 Bank 读取规则或回归口径。
- 新功能进入独立 Effect `PluginID=31002`，避免破坏已能播放的 Source 路径。

## 二、实施状态

| 项目 | 状态 | 证据 |
| --- | --- | --- |
| Source 31001 兼容回归 | 已完成 | `Build\Core\Testing\Temporary\LastTest.log`：`rwwa_source_params_bank_tests` 通过，261/273 ABI |
| Effect 31002 Runtime/Authoring | 已完成 | Wwise Authoring smoke `effectClassId = 2031748099`，Native Host 注册 31002 |
| Effect 参数 ABI | 已完成 | `rwwa_effect_params_bank_tests` 通过；71 参数、281-byte block |
| Audio File Source + Effect SoundBank | 已完成 | Baseline、InputRoleWetGeometry、WetZero 三个 473-byte bank 均生成并保留；内部 Effect block 均为 281 bytes |
| 持久雨声 demo | 已完成 | `RWWA_Demo_Heavy_Rain_Puddles` + `RWWA_Demo_Heavy_Rain_Puddles_Audio` + `RWWA_Demo_Weather_Geometry_Effect` + `Play_RWWA_Demo_Heavy_Rain_Puddles` 已导入 smoke 工程；当前 smoke 使用 `existing-template` |
| Runtime Diagnostics V1 | 已完成 | 96-byte ABI；`ResetV1` / `GetV1`、coherent snapshot、non-finite counter、BUSY retry，以及五字段 last-block tuple 的 no-wait try-commit 均有自动化证据 |
| Host 三态音频合同 matrix | 已完成 | `changed`、`wet-bypass`、`geometry-disabled` 三态全部通过；两份错误 expectation 负例均以 code 52 正确失败 |
| Runtime C ABI scene 提交 | 已完成 | `SetV1` / `GetV1` / `ClearV1` 三个导出均被 Host 找到并 roundtrip |
| Authoring 2D canvas 操作 | 已完成 | Listener/yaw、Add/Delete、拖圆、黄 handle 半径、Priority 10 -> 107 -> Undo 10；连续 smoke 中 move/resize 均为 window-message + converged |
| Unity Adapter | 后续 | 本轮未实现 |
| Unreal Adapter | 后续 | 本轮未实现 |
| 高级 DSP 参数 | 后续 | `EnvelopeSensitivity`、band weights、smoothing 等不属于当前实现 |
| 人工主观听感验收 | 后续 | 当前只有自动化、Profiler、fixture、Host 证据 |

## 三、使用入口

在 Wwise 中使用 v0.3 Effect：

1. 导入高质量 rain、wind 或天气 ambience 音频。
2. 将导入素材设为循环 `Audio File Source` 或 streamed loop。
3. 在承载该 loop 的 Sound、Actor-Mixer 或 Bus 上添加 `RealWorld Weather Acoustics Effect`。
4. 设置 `InputRole` 和 `WetMix`。
5. 在 Effect 的 2D canvas 中拖动 Listener 和 yaw。
6. 用 `Add` / `Delete` 管理圆形 Feature，拖动圆心调 X/Z，拖动黄色 handle 调半径。
7. 在右侧逐项编辑 `Profile`、`Mask`、`Priority` 和 `Y`。

Effect 输入素材应提供声音主体质量。插件当前只负责几何、距离、材质 profile、mask、priority 与 listener/weather 交互，不替代素材选型、循环剪辑、Wwise streaming、State/RTPC 或空间传播混音。

当前 smoke 工程保留一个可直接给人试听的雨声 demo。它使用用户提供的 Envato preview MP3 前 30 秒生成 `WwiseSmoke\RealWorldWeatherAcousticsSmoke\Originals\SFX\RWWA_Heavy_Rain_Puddles_30s.wav`，并导入为 `RWWA_Demo_Heavy_Rain_Puddles_Audio`。生成后的测试 WAV/WEM 作为仓库测试资产分发，便于 fresh clone 后直接试听和跑自动化验证；发布者需要自行确认 Envato preview 许可覆盖这种仓库分发方式。WAV 证据：48 kHz stereo PCM24、30.0 秒、`8640102` bytes、SHA-256 `1fa150708de00627796a4e3963a9becd97595e01e5e769e6a3b0a5d1cf076adc`。导入报告为 `Build\WwiseSmoke\import-rain-demo-20260722T121122186Z.json`。

## 四、Effect 31002 实际参数契约

当前 Effect 参数总数为 71，SoundBank 参数块为 281 bytes。

| 参数 | 范围 | 默认 | 说明 |
| --- | --- | --- | --- |
| `InputRole` | `Rain=0` / `Wind=1` / `Generic=2` | `Generic=2` | 选择输入素材的解释方式。 |
| `WetMix` | `0..1` | `0` | 0 为透明干声；1 为完整几何响应混合。 |
| `ResponseGainDb` | `-24..12` dB | `0` | 几何响应后级增益。 |
| `TransientSensitivity` | `0..1` | `0.5` | 输入瞬态对响应强度的影响。 |
| `RainIntensity` | `0..1` | Source 兼容默认 | 与天气状态一致的降雨强度。 |
| `WindSpeed` | `0..40` m/s | Source 兼容默认 | 与天气状态一致的风速。 |
| `WindDirectionDegrees` | `0..360` | Source 兼容默认 | 顶视风向。 |
| `WindGustiness` | `0..1` | Source 兼容默认 | 阵风/包络起伏。 |
| `Seed` | `0..2147483647` | Source 兼容默认 | 确定性随机种子。 |
| `GeometryEnabled` | bool | true | 关闭后回退为不使用几何响应。 |
| `ListenerX/Y/Z` | finite float | `0` | Listener 位置。 |
| `ListenerYawDegrees` | degrees | `0` | Listener 朝向。 |
| `FeatureCount` | `0..8` | `4` | 使用前多少个固定槽位。 |
| `Feature1..8 X/Y/Z` | finite float | 固定环形默认 | Feature 中心位置。 |
| `Feature1..8 Radius` | `0.2..10000` | `2` | Sphere proxy 半径；Source runtime clamp 也已统一为最小 `0.2` 并有测试覆盖。 |
| `Feature1..8 Profile` | `0..3` | slot pattern | `0 Metal`、`1 Wood`、`2 Glass`、`3 Tile`。 |
| `Feature1..8 Mask` | `0..3` | `3` | `0 Disabled`、`1 Rain`、`2 Wind`、`3 Rain + Wind`。 |
| `Feature1..8 Priority` | `0..1000` | `1` | Active Set 数值权重，不是四档枚举。 |

当前没有实现 `EnvelopeSensitivity`、`BandExcitationLow/Mid/High`、`ResponseSmoothingMs`、`GeometryGainDb`、`DistanceScaleMeters`、`PriorityBias` 或可调 `ActiveLimit`。这些只能作为后续高级参数讨论，不能写入当前可用参数表。

## 五、Runtime C ABI 与 Host

Runtime API header：`RealWorldWeatherAcoustics\SoundEnginePlugin\RealWorldWeatherAcousticsRuntimeAPI.h`。staging 后复制到：

```text
Artifacts\Runtime\include\AK\Plugin\RealWorldWeatherAcousticsRuntimeAPI.h
```

当前 C ABI 使用 `RWWA_RUNTIME_SCENE_ABI_VERSION = 1`，固定 `RWWA_RUNTIME_SCENE_MAX_FEATURES = 8`。

| 结构/导出 | 大小/状态 | 说明 |
| --- | ---: | --- |
| `RWWA_RuntimeFeatureV1` | 40 bytes | `id`、`x/y/z`、`radius`、`profile`、`mask`、`priority`。 |
| `RWWA_RuntimeSceneV1` | 392 bytes | ABI、revision、valid、geometry、listener、weather、8 个 feature。 |
| `RWWA_RuntimeScene_SetV1` | 已导出 | 提交最新 scene。 |
| `RWWA_RuntimeScene_GetV1` | 已导出 | 读取最新 scene，用于 roundtrip/诊断。 |
| `RWWA_RuntimeScene_ClearV1` | 已导出 | 清除外部 scene。 |
| `RWWA_RuntimeDiagnosticsV1` | 96 bytes | Execute/frame、runtime/fallback、wet bypass、geometry disabled、revision、peak 与 non-finite sample 诊断；五个 `last*` 字段组成完整 per-block tuple。 |
| `RWWA_RuntimeDiagnostics_ResetV1` | 已导出 | 在测量窗口前清零诊断。 |
| `RWWA_RuntimeDiagnostics_GetV1` | 已导出 | 读取诊断快照。 |

Native Host 命令入口：

```powershell
& .\Scripts\Build-NativeHost.ps1 -WwiseRoot $wwise

& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260722T123034276Z' `
    -Bank 'RWWA_Effect_Baseline.bnk' `
    -SceneJson 'Tools\NativeHost\scene.example.json' `
    -Expectation changed `
    -DurationMs 1200 `
    -SkipBuild

& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260722T123034276Z' `
    -Bank 'RWWA_Effect_WetZero.bnk' `
    -SceneJson 'Tools\NativeHost\scene.example.json' `
    -Expectation wet-bypass `
    -DurationMs 1200 `
    -SkipBuild

& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260722T123034276Z' `
    -Bank 'RWWA_Effect_Baseline.bnk' `
    -SceneJson 'Tools\NativeHost\scene.disabled.example.json' `
    -Expectation geometry-disabled `
    -DurationMs 1200 `
    -SkipBuild
```

`Tools\NativeHost\scene.example.json` 提交 revision 2 的 2 个 Feature；`scene.disabled.example.json` 提交 revision 3、`geometryEnabled=false` 的同类 runtime scene。GeometryOff 使用 Baseline bank + disabled runtime scene，避免 bank 与 runtime 同时关闭几何。正式 expectation 模式为 `changed`、`wet-bypass`、`geometry-disabled`；`transparent` 仅为 legacy 兼容模式。

Bank/Authoring listener、weather 与 feature 槽只在 Runtime API 返回 `RWWA_RUNTIME_STATUS_UNCLAIMED` 且该 Effect 实例此前从未 claim 过 runtime scene 时作为 authored fallback。first claim 写入竞争若返回 `BUSY`，Effect 立即进入 runtime-owned 空 scene 而不短暂回退；已有 runtime snapshot 时，`BUSY` 继续使用保留 snapshot。runtime scene 一旦被 claim，clear/error 路径也不会静默切回 authored 数据。

Scene Host roundtrip 比较全部 89 个字段：17 个 scene/header/listener/weather 字段，加 8 个 feature 的 9 个字段。最终三态与双负例均为 `roundTripPayloadMatched = true`、mismatch count 0。

Diagnostics 的 `lastRuntimeSceneRevision`、`lastInputPeak`、`lastOutputPeak`、`lastWetDifferencePeak`、`lastBlockUsedRuntimeScene` 由音频线程通过 no-wait try-commit 一次提交完整 per-block tuple。争用时该 tuple 可以落后于累计 counters、maxima 和 generation，但绝不会混合不同 block；未取得提交权的 block 仍正常累加这些累计量。确定性 forced-contention 与 multi-writer encoding 测试覆盖了“可滞后、不可混合”的合同。

## 六、已完成步骤

1. 冻结 Source 31001 回归：261/273 字节 ABI、offline renderer、Authoring smoke 保持通过。
2. 增加 Effect 31002 Runtime/Authoring、XML、factory header 与 Runtime API header。
3. 定义 Effect 参数枚举和 281-byte Bank 序列化合同。
4. 实现 `WetMix=0` 采样透明路径并由 `rwwa_geometry_interaction_tests` 覆盖。
5. 实现输入角色、瞬态敏感度、weather/listener/geometry 参数映射。
6. 实现 8 注册槽、当前 DSP Active Set 选择与 Priority 数值权重。
7. 完成 Authoring UI 与 2D canvas 操作回归。
8. 扩展 staging：严格 5 files。
9. 完成 Wwise Authoring smoke：创建 Source 回归对象，复用持久雨声 demo 的 Effect 模板对象，生成三组 Effect bank、保留 10/10 size+SHA 匹配的 Native Host fixture。
10. 完成 96-byte Runtime Diagnostics V1、lock-free counters/peaks、non-finite sample counter、coherent snapshot、`ResetV1` / `GetV1` BUSY retry 与长时 `uint64` 计数合同；publish/reset handshake 使用 seq_cst，Get 最后观测 generation；五字段 last-block tuple 采用 no-wait try-commit，争用可滞后但不混合 block，并有确定性 forced-contention、multi-writer 与 race 测试。
11. 完成 Native Host 三态 matrix：加载 bank、注册 31001/31002、提交/读取/清除 scene、PostEvent、render、诊断断言与清理退出。
12. 完成双负例门禁：非零 wet difference 错判 bypass、零 difference 但 reason 错配均以 `diagnostics-assertions` / code 52 失败。

## 七、验证证据

| 证据 | 结果 |
| --- | --- |
| `Build\Core\Testing\Temporary\LastTest.log` | `Test 8/8` 全部通过。 |
| `Build\WwiseSmoke\import-rain-demo-20260722T121122186Z.json` | 持久 Sound / AudioFileSource / Effect / Event 导入成功；71 个 Effect 属性可读。 |
| `Build\WwiseSmoke\wwise-authoring-smoke-20260722T123034276Z.json` | 连续第二次通过；wrapper/client `success = true`；`requireRetainedRainDemo = true`；`effectObjectSet.mode = existing-template`；GUI 36/36；Source/Effect/Shared groups 6/6、7/7、1/1。 |
| `Build\WwiseSmoke\wwise-authoring-smoke-20260722T123034276Z.prof` | 745058 bytes。 |
| Source / Effect Profiler | Source `0.1414999962 ms`、1 voice、`-26.45691872 dB`；Effect `0.1155999973 ms`、1 voice、`-16.77216339 dB`。 |
| Effect SoundBank serialization | 71 props、281-byte block；Baseline/InputRoleWetGeometry/WetZero 三个 bank 各 473 bytes。 |
| Fixture manifest | `Build\NativeHost\Fixture\20260722T123034276Z\RWWA_Effect_Fixture.json`；10/10 artifacts size+SHA match，另加 manifest 共 11 个保留文件，WEM 为 `Media\528110025.wem`。 |
| Host changed | `native-host-rain-changed-20260722T123034276Z.json`；110 blocks / 56320 frames / revision 2 / max diff `0.0136105493`。 |
| Host wet bypass | `native-host-rain-wet-bypass-20260722T123034276Z.json`；112 blocks / 57344 frames，wet bypass 112，input=output `0.230743408`，max diff 0。 |
| Host geometry disabled | `native-host-rain-geometry-disabled-20260722T123034276Z.json`；Baseline + disabled runtime scene，110 blocks / 56320 frames，geometry disabled 110，input=output `0.230743408`，max diff 0。 |
| Host full payload / finite gate | 三态均 89-field roundtrip match、mismatch 0、non-finite 0。 |
| Host negative gates | 错误 expectation 负例覆盖仍保留：非零 wet response 不能通过 `wet-bypass`，geometry-disabled 透明输出也不能伪装成 `wet-bypass`；均以 code 52 / `diagnostics-assertions` 正确失败。 |

## 八、停止条件

| 条件 | 状态 |
| --- | --- |
| Source 31001 v0.2 回归未退化 | 已证实。 |
| Effect 31002 能被 Wwise Authoring 发现、创建、序列化和生成 SoundBank | 已证实。 |
| Native Host 能加载生成 bank、PostEvent、render 并清理退出 | 已证实。 |
| Runtime C ABI scene set/get/clear roundtrip | 已证实。 |
| `WetMix=0` 透明与 `WetMix>0` 几何响应 | Core 单测与 Host matrix 均已证实。 |
| 8 槽 scene 与 2 槽 JSON scene Host 路径 | 已证实。 |
| Effect CPU 在首轮预算内 | 已证实，Profiler 记录 `0.1155999973 ms`。 |
| Runtime Diagnostics V1 与第三个 WetZero bank | 已证实并纳入最终 fixture/matrix。 |
| 人工主观听感验收 | 未完成，后续。 |
| Unity/Unreal Adapter | 未完成，后续。 |
| 高级 DSP 参数与完整产品化调参 | 未完成，后续。 |

## 九、后续范围

- Unity Adapter 与 Unreal Adapter。
- 高级 DSP 参数：envelope sensitivity、band excitation weights、response smoothing、distance scale、priority bias。
- Scene JSON import/export、capture/replay、monitor data。
- Plane、Box、Convex、Edge、Aperture、完整 3D Authoring。
- 主观听感验收、素材包建议、商业发布 ID 与跨平台打包。
