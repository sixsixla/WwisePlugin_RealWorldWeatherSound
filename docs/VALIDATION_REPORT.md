# RealWorld Weather Acoustics v0.3 Validation Report

## Verdict

The v0.3 hybrid vertical slice passes its current automated acceptance boundary on Wwise `2023.1.19.8928`, Windows x64, Visual Studio 2022/vc170, Release.

Latest source changes after the original v0.3 evidence refine the Effect Authoring rain audition path: the main panel now exposes only audible rain/material controls, appends `Plastic=4`, and makes `RainIntensity` / `Rain Amount` drive impact density and energy. Fresh Core build/CTest, Wwise Authoring smoke, and NativeHost automated QA for that change have passed. Manual subjective listening remains a separate pending gate.

This verdict covers:

- v0.2 Source `PluginID=31001` compatibility regression: 69 parameters, 261-byte legacy block, 273-byte current block.
- v0.3 Effect `PluginID=31002`: 71 parameters, 281-byte block, Authoring creation/serialization, three SoundBank variants, Profiler CPU/output evidence.
- Retained rain demo: persistent Wwise Sound/Event using a 30-second high-quality rain WAV as the Effect input source.
- Runtime Scene C ABI: `SetV1` / `GetV1` / `ClearV1`, 392-byte scene, 40-byte feature, 8 feature slots, 89-field full-payload comparison, and UNCLAIMED-only authored fallback.
- Runtime Diagnostics V1: 96-byte ABI, `ResetV1` / `GetV1`, coherent snapshots, BUSY retry, execute/frame/selection/reason counters, actual peaks and non-finite samples, plus a coherent five-field last-block tuple committed without waiting.
- Native SoundEngine Host three-state audio-contract matrix plus two expected-failure negative gates.

It does not claim subjective listening approval, Unity/Unreal integration, commercial release identity, or future advanced DSP parameters.

## Implemented Boundary

### Source 31001 retained boundary

- Programmatic, physically inspired wind/rain Source remains available.
- Source parameter count remains 69.
- Source Bank ABI regression keeps 261-byte legacy and 273-byte current blocks.
- Wwise Authoring 2D Preview continues to support Listener drag, yaw drag, Feature Add, Feature center drag, radius-handle drag, Delete button, and Delete key.

### Effect 31002 v0.3 boundary

- Effect attaches to Wwise Audio File Source/streamed loop paths on Sound, Actor-Mixer, or Bus.
- Input material provides the high-quality rain/wind/ambience bed; the plug-in adds geometry/material interaction only. The rain path uses the input rain bed as the body sound and adds surface impact/resonance instead of synthesizing the whole rain sound from scratch.
- Actual current parameters:

| Parameter | Range | Default |
| --- | --- | --- |
| `InputRole` | `Rain=0`, `Wind=1`, `Generic=2` | `Generic=2` |
| `WetMix` | `0..1` | `0` |
| `ResponseGainDb` | `-24..12` dB | `0` |
| `TransientSensitivity` | `0..1` | `0.5` |
| `RainIntensity` | `0..1` | Source-compatible default |
| `WindSpeed` | `0..40` m/s | Source-compatible default |
| `WindDirectionDegrees` | `0..360` | Source-compatible default |
| `WindGustiness` | `0..1` | Source-compatible default |
| `Seed` | `0..2147483647` | Source-compatible default |
| `GeometryEnabled` | bool | true |
| `ListenerX/Y/Z` | finite float | `0` |
| `ListenerYawDegrees` | degrees | `0` |
| `FeatureCount` | `0..8` | `4` |
| `Feature1..8 X/Y/Z/Radius/Profile/Mask/Priority` | fixed 8 slots; Profile `0..4` | fixed ring defaults; Radius min clamp is 0.2 |

Profile values are `0 Metal`, `1 Wood`, `2 Glass`, `3 Tile`, and appended `4 Plastic`; the legacy `0..3` meanings do not change. `Priority` is a numeric `0..1000` weight. It is not a four-level enum.

Runtime scene profile IDs use the same values. `RealWorldWeatherAcousticsRuntimeAPI.h` exposes `RWWA_RUNTIME_PROFILE_METAL=0`, `WOOD=1`, `GLASS=2`, `TILE=3`, `PLASTIC=4`, and `RWWA_RUNTIME_PROFILE_MAX=RWWA_RUNTIME_PROFILE_PLASTIC`. The previous runtime-side clamp-to-3 defect is fixed; game-side `RWWA_RuntimeScene_SetV1` payloads can now submit Plastic surfaces.

Effect Authoring rain audition UI intentionally hides `Seed`, `ListenerY`, `Priority`, and wind parameters from the main rain test path while keeping them in ABI/Bank/runtime. The visible controls are `Input Audio`, `Rain Amount`, `Surface Mix`, `Impact Gain dB`, `Impact Sharpness`, `Geometry Response`, and per-Surface `X/Z/Radius/Material/Weather Response`.

Not implemented in v0.3: `EnvelopeSensitivity`, band weights, smoothing, distance scale, priority bias, Unity Adapter, Unreal Adapter, advanced geometry types, Capture/Replay, Monitor UI, Ambisonics, and manual subjective listening acceptance.

### Latest authoring-audibility validation status

The latest rain/material UI and DSP tuning pass has completed automated Core, Wwise Authoring, and NativeHost coverage. Human listening remains a separate pending gate.

| Check | Status | Evidence |
| --- | --- | --- |
| Build and CTest after latest source changes | PASS | Fresh `Build/Core/Testing/Temporary/LastTest.log`, 8/8 tests, started at 2026-07-22 21:30 local time. |
| Effect ABI remains compatible | PASS | `rwwa_effect_params_bank_tests`: `281-byte block, defaults, mapping, rejects, and clamps`; Source tests still report 261/273-byte contracts. |
| Plastic appended safely | PASS | Runtime API exposes `RWWA_RUNTIME_PROFILE_PLASTIC=4` and `RWWA_RUNTIME_PROFILE_MAX=PLASTIC`; profile `0..3` meanings remain unchanged. |
| Rain Amount / material response automated delta | PASS | `Build/NativeHost/native-host-rain-material-changed-20260723T124436929Z.json`: max wet difference `0.0905741826`. |
| Material position scene | PASS | `Tools/NativeHost/scene.rain-material-lab.example.json`: revision 4, 4 Surface circles, Listener at lower Metal, right Surface `profile=4` Plastic. |
| WetMix0 / GeometryOff A/B | PASS | `native-host-rain-material-wet-bypass-20260723T124436929Z.json` and `native-host-rain-material-geometry-disabled-20260723T124436929Z.json` both report max wet difference `0`. |
| Authoring UI smoke | PASS | `Build/WwiseSmoke/wwise-authoring-smoke-20260723T124436929Z.json`: wrapper/client `success = true`, Wwise `2023.1.19.8928`, Profiler `988049` bytes, fixture unchanged. |
| Manual acceptance | PENDING | During playback, drag Listener into the lower/material Surface and hear corresponding rain-hit response; `Surface Mix=0` and `Geometry Response=false` remove the response for A/B. |

### Retained rain demo boundary

The smoke project now keeps a human-openable Effect demo instead of relying only on disposable test objects:

| Object | Purpose |
| --- | --- |
| `RWWA_Demo_Heavy_Rain_Puddles` | Persistent Wwise Sound for manual audition |
| `RWWA_Demo_Heavy_Rain_Puddles_Audio` | Standard Wwise `AudioFileSource` referencing the retained WAV |
| `RWWA_Demo_Weather_Geometry_Effect` | `RealWorld Weather Acoustics Effect` on the demo Sound |
| `Play_RWWA_Demo_Heavy_Rain_Puddles` | Persistent Event targeting the demo Sound |

The source MP3 is the user-provided Envato preview at `C:\Users\Administrator\Downloads\Envato_Rain_Previews\02_Heavy_Rain_With_Puddles_YDZSTXK_preview.mp3`. `Scripts\Prepare-RainTestAsset.ps1` cuts the first 30 seconds and writes:

```text
WwiseSmoke/RealWorldWeatherAcousticsSmoke/Originals/SFX/RWWA_Heavy_Rain_Puddles_30s.wav
```

Verified WAV format: 48 kHz stereo PCM24, 30.0 seconds, `8640102` bytes, SHA-256 `1fa150708de00627796a4e3963a9becd97595e01e5e769e6a3b0a5d1cf076adc`. The generated test WAV and generated WEM are repository test assets so a fresh clone can immediately audition and validate the retained rain demo. Publishers must confirm that the Envato preview license covers repository distribution of those derived test assets.

Final persistent import report:

```text
Build/WwiseSmoke/import-rain-demo-20260722T121122186Z.json
```

## Fresh Verification Evidence

### Build and CTest

Primary CTest evidence:

```text
Build/Core/Testing/Temporary/LastTest.log
```

`Test 8/8` passed:

| Test | Result | Evidence |
| --- | --- | --- |
| `rwwa_core_tests` | PASS | rain/wind/gust metrics logged |
| `rwwa_geometry_interaction_tests` | PASS | includes `WetMix=0` sample transparency and role/geometry interaction checks |
| `rwwa_runtime_scene_api_tests` | PASS | 89-field scene payload, first-claim/BUSY behavior, UNCLAIMED-only fallback, coherent Diagnostics/Reset BUSY retry, non-finite counter, long-run `uint64_t` behavior, forced-contention last-tuple coherence, deterministic multi-writer encoding, and concurrency |
| `rwwa_source_params_bank_tests` | PASS | `SourceParams bank ABI contract passed: 261-byte legacy and 273-byte current blocks.` |
| `rwwa_effect_params_bank_tests` | PASS | `EffectParams bank ABI contract passed: 281-byte block, defaults, mapping, rejects, and clamps.` |
| `rwwa_offline_renderer_weather_ring` | PASS | wrote `Build/Core/Fixtures/weather_ring.wav` |
| `rwwa_offline_renderer_open_wind` | PASS | wrote `Build/Core/Fixtures/open_wind.wav` |
| `rwwa_offline_renderer_rain_metal` | PASS | wrote `Build/Core/Fixtures/rain_metal.wav` |

Latest run note: this `LastTest.log` was refreshed at 2026-07-22 21:30 local time after the Plastic/runtime-profile and rain-material tuning changes.

### Staging and install

`Artifacts/stage-record.json` records `stagedAtUtc = 2026-07-22T13:37:03.4692394Z` and exactly 5 staged files:

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| Authoring `RealWorldWeatherAcoustics.dll` | 129024 | `bee0f3170a5eaad9c647ad1904cad1469482ca4d3183072cc8b048ed99deaf5b` |
| Authoring `RealWorldWeatherAcoustics.xml` | 55757 | `abf38ea5f1e45345b83036fc9571dbfe1e8ba266c6f7d2b6c076dadd468cc672` |
| Runtime `RealWorldWeatherAcousticsSource.lib` | 578412 | `5eaca01efa3548078469c183d2d0f6507394dac0662fa9d868a34c04c0c39f89` |
| Runtime `RealWorldWeatherAcousticsSourceFactory.h` | 1614 | `04417df0762ae809a6d2848cc535fc3b042307c079d7fbe4cc148bd88e35a389` |
| Runtime `RealWorldWeatherAcousticsRuntimeAPI.h` | 5798 | `3463dcc92c2c6e4470fe07bd50c6abf82aab22ad99cc69ad9883999e344a1db9` |

Wwise Authoring installation copies only the DLL/XML. Latest backup:

```text
Artifacts/InstallBackup/20260723T123841958Z
```

### Wwise Authoring smoke

Primary final smoke evidence:

```text
Build/WwiseSmoke/wwise-authoring-smoke-20260723T124436929Z.json
Build/WwiseSmoke/wwise-authoring-smoke-20260723T124436929Z.prof
```

The final smoke started at `2026-07-23T12:44:36.9315904Z` and finished at `2026-07-23T12:44:45.1534028Z`.

| Claim | Result | Evidence |
| --- | --- | --- |
| Wwise Authoring smoke completed | PASS | wrapper `success = true`; client `success = true`; `error = null` |
| Installed Authoring files match staging | PASS | DLL/XML hashes match `Artifacts/stage-record.json` |
| Wwise version | PASS | `client.waapiCompatibility.wwiseVersion = 2023.1.19.8928` |
| Retained rain demo required | PASS | `requireRetainedRainDemo = true` |
| Retained rain demo template valid | PASS | Sound, AudioFileSource, Effect, Event, and Event target assertions are true |
| Effect smoke uses retained template | PASS | `effectObjectSet.mode = existing-template`; Sound `RWWA_Demo_Heavy_Rain_Puddles`; Effect `RWWA_Demo_Weather_Geometry_Effect` |
| Retained input WAV valid | PASS | `RWWA_Heavy_Rain_Puddles_30s.wav`; 48 kHz stereo PCM24; 30.0 s; `8640102` bytes; SHA-256 `1fa150708de00627796a4e3963a9becd97595e01e5e769e6a3b0a5d1cf076adc` |
| Fixture smoke project unchanged | PASS | `fixtureUnchanged = true`; 35 files, `9491498` bytes, digest `40a178df4a421cb5a68631bf54a763d39fd0e92516b6e0280449999460d66c20` |
| Disposable copy isolated | PASS | `copyMatchesFixtureBytes = true`; `disposableProjectRemoved = true` |
| Source discovered and instantiated | PASS | Source class ID `2031682562` |
| Effect discovered and instantiated | PASS | Effect class ID `2031748099`, `PluginID=31002` |
| Source GUI smoke remains valid | PASS | all 36/36 GUI assertions; stable preset text, Add/Delete, window-message move/resize converged, radius, Delete key, Undo gates |
| Effect Priority edit works | PASS | `Feature1Priority` `10 -> 107 -> Undo 10` |
| Source Bank serialization remains valid | PASS | 69 parameters, 273-byte current block, 261-byte legacy regression retained |
| Effect Bank serialization works | PASS | 71 properties, 281-byte block, `InputRole`/`WetMix`/`GeometryEnabled` variant bytes match |
| Three Effect bank variants retained | PASS | Baseline, InputRoleWetGeometry, WetZero; each 473 bytes with a 281-byte Effect block |
| Fixture manifest written | PASS | `Build/NativeHost/Fixture/20260723T124436929Z/RWWA_Effect_Fixture.json`; 10/10 selected artifacts match source/destination size and SHA-256 |
| Assertion groups | PASS | Source 6/6, Effect 7/7, Shared 1/1 |
| Source executes | PASS | 1 physical voice; CPU `0.1395999938 ms`; peak `-28.79373550 dB` |
| Effect executes | PASS | 1 voice; CPU `0.1159999967 ms`; peak `-15.69230461 dB` |
| Profiler capture saved | PASS | `.prof` size `988049` bytes |

The retained fixture has 10 copied artifacts plus its manifest, 11 files total:

```text
Build/NativeHost/Fixture/20260723T124436929Z/RWWA_Effect_Fixture.json
Build/NativeHost/Fixture/20260723T124436929Z/RWWA_Effect_Baseline.bnk
Build/NativeHost/Fixture/20260723T124436929Z/RWWA_Effect_InputRoleWetGeometry.bnk
Build/NativeHost/Fixture/20260723T124436929Z/RWWA_Effect_WetZero.bnk
Build/NativeHost/Fixture/20260723T124436929Z/Init.bnk
Build/NativeHost/Fixture/20260723T124436929Z/Init.txt
Build/NativeHost/Fixture/20260723T124436929Z/PlatformInfo.xml
Build/NativeHost/Fixture/20260723T124436929Z/PluginInfo.xml
Build/NativeHost/Fixture/20260723T124436929Z/RWWA_Effect_WetZero.txt
Build/NativeHost/Fixture/20260723T124436929Z/SoundbanksInfo.xml
Build/NativeHost/Fixture/20260723T124436929Z/Media/528110025.wem
```

### Native Host audio-contract matrix

Runtime Scene V1 remains 392 bytes with eight 40-byte features. The Host compares all 89 payload fields: 17 scene/header/listener/weather fields and 9 fields for each feature. Runtime Diagnostics V1 is 96 bytes and is reset/read through `RWWA_RuntimeDiagnostics_ResetV1` / `RWWA_RuntimeDiagnostics_GetV1`. Its audio-thread publisher records measured wet difference and non-finite samples even when a bypass/off reason flag is set; flags do not overwrite measurements. Cumulative counters are `uint64_t` atomics.

Authored Bank/Authoring parameters are selected only when `RWWA_RuntimeScene_GetV1` returns `UNCLAIMED` and the Effect instance has never claimed a runtime scene. During first claim, `BUSY` selects a runtime-owned empty scene rather than authored fallback; after a snapshot exists, `BUSY` reuses that retained snapshot. Clear/error handling empties runtime state instead of silently selecting authored fallback.

Diagnostics Get returns only a coherent snapshot. The five fields `lastRuntimeSceneRevision`, `lastInputPeak`, `lastOutputPeak`, `lastWetDifferencePeak`, and `lastBlockUsedRuntimeScene` are a complete per-block tuple written with a no-wait try-commit. If publishers contend, the tuple may lag the cumulative counters, maxima, and generation, but it never combines fields from different blocks; every contending block still contributes to those cumulative values. Publish/reset handshake uses sequential consistency, and Get keeps generation as its final concurrency observation so a completed publisher cannot be missed after the payload read. If Get/Reset overlaps a publish or reset, it returns `RWWA_RUNTIME_STATUS_BUSY` without exposing partial state; control-thread callers retry. Deterministic tests cover forced try-commit contention, four-writer tuple encoding, generation changes during Get, publish/reset overlap, post-retry coherence, reset-to-zero, non-finite counting, concurrency, and long-run counter behavior.

| Report | Isolated condition | Diagnostics result |
| --- | --- | --- |
| `Build/NativeHost/native-host-rain-material-changed-20260723T124436929Z.json` | Baseline + `scene.rain-material-lab.example.json`; `changed` | revision 4 / 4 features / 110 executes / 56320 frames / runtime 110 / fallback 0 / wet 0 / geometry 0 / non-finite 0; max input `0.230743408`, output `0.270008653`, wet difference `0.0905741826` |
| `Build/NativeHost/native-host-rain-material-wet-bypass-20260723T124436929Z.json` | WetZero + `scene.rain-material-lab.example.json`; `wet-bypass` | revision 4 / 4 features / 113 executes / 57856 frames / runtime 113 / fallback 0 / wet 113 / geometry 0 / non-finite 0; input=output `0.230743408`, wet difference 0 |
| `Build/NativeHost/native-host-rain-material-geometry-disabled-20260723T124436929Z.json` | **Baseline** + disabled revision-3 runtime scene; `geometry-disabled` | 112 executes / 57344 frames / runtime 112 / fallback 0 / wet 0 / geometry 112 / non-finite 0; input=output `0.230743408`, wet difference 0 |

All three reports show Source 31001 and Effect 31002 registered, 89-field scene `Set/Get/Clear` full-payload match with mismatch count 0, Diagnostics `Reset/Get`, non-finite count 0, `Init.bnk` and requested-bank load/unload, successful PostEvent, render 0 failures, clean termination, and `exitStatus.success = true`. GeometryOff uses the Baseline bank rather than the InputRoleWetGeometry bank, so the disabled runtime scene is the only geometry-off variable.

The 20260722 fixture and reports remain useful as historical evidence, but current summaries and recommended commands use `Build/NativeHost/Fixture/20260723T124436929Z` and the timestamped Rain Material Lab reports above. The Rain Material Lab changed/wet-bypass reports exercise the four-circle material layout and runtime-submitted Plastic profile.

The final CLI contract uses `-Expectation changed|wet-bypass|geometry-disabled`. `transparent` remains accepted only as a legacy mode; final acceptance uses reason-aware modes that gate both wet-difference magnitude and the relevant reason counter.

Negative evidence:

| Report | Deliberately wrong expectation | Required result |
| --- | --- | --- |
| Negative gate: changed-as-wet | Nonzero Baseline response asserted as `wet-bypass` | wet-difference and wet-bypass reason both fail; code 52 / `diagnostics-assertions` |
| Negative gate: geometry-as-wet | Geometry-disabled transparent output asserted as `wet-bypass` | difference is zero and wet magnitude passes, but reason counters fail; code 52 / `diagnostics-assertions` |

Both negative runs still have full-payload match, mismatch count 0, non-finite count 0, and clean cleanup. These failures show that neither a nonzero response nor an unrelated transparent reason can pass the matrix accidentally.

## Reproduction

Run from the product root after closing other Wwise Authoring instances:

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
& .\Scripts\Build-NativeHost.ps1 -WwiseRoot $wwise
```

Native Host baseline:

```powershell
& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260723T124436929Z' `
    -Bank 'RWWA_Effect_Baseline.bnk' `
    -SceneJson 'Tools\NativeHost\scene.rain-material-lab.example.json' `
    -Expectation changed `
    -DurationMs 1200 `
    -SkipBuild
```

Native Host WetZero bypass:

```powershell
& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260723T124436929Z' `
    -Bank 'RWWA_Effect_WetZero.bnk' `
    -SceneJson 'Tools\NativeHost\scene.rain-material-lab.example.json' `
    -Expectation wet-bypass `
    -DurationMs 1200 `
    -SkipBuild
```

Native Host runtime GeometryOff:

```powershell
& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260723T124436929Z' `
    -Bank 'RWWA_Effect_Baseline.bnk' `
    -SceneJson 'Tools\NativeHost\scene.disabled.example.json' `
    -Expectation geometry-disabled `
    -DurationMs 1200 `
    -SkipBuild
```

`-PythonWithWaapi` is local smoke-test tooling only. It is not a product build or runtime dependency.

## What This Does Not Prove

- No human subjective listening acceptance was completed. WAV files, DSP metrics, output peak, Profiler CPU, and smoke reports are regression evidence only.
- Unity and Unreal adapters are not implemented or validated.
- Advanced DSP controls such as `EnvelopeSensitivity`, band weights, response smoothing, distance scale, and priority bias are not implemented.
- Current geometry remains fixed 8-slot sphere-style data for this slice. Plane, Box, Convex, Edge, Aperture, full 3D Authoring, Capture/Replay, and Monitor UI are future work.
- Development IDs remain internal only: `CompanyID=64`, Source `PluginID=31001`, Effect `PluginID=31002`. Public or commercial distribution requires Audiokinetic-assigned non-conflicting IDs and full rebuild/revalidation.

## Remaining Risks

- Subjective rain/wind/material quality still needs a human listening pass.
- Engine lifecycle and packaging risks remain for Unity/Unreal.
- The Runtime C ABI is validated in Native Host, but it has not been exercised through an actual game adapter.
- The current Effect relies on input material quality; poor loops or badly cut streamed assets will still sound poor.
