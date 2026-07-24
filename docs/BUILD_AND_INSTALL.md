# Build, validation, staging, and Authoring installation

## Supported environment

v0.3 intentionally targets one ABI/toolchain combination:

| Component | Required value |
| --- | --- |
| Wwise | 2023.1.19.8928 |
| Host/target | Windows x64 |
| Visual Studio | 2022 with C++ and MSBuild |
| Wwise toolset | vc170 |
| Configuration | Release |
| CMake | 3.20 or newer |
| Python | Python 3 (`py -3` is supported) |

Every script accepts an explicit `-WwiseRoot` where Wwise is needed. Without it, resolution checks `WWISE_ROOT`, `WWISEROOT`, the known development installation, and immediate `Wwise*` directories under standard installation parents. The SDK version header must resolve to exactly `2023.1.19.8928`; another SDK fails before generation or build.

## Reproducible workflow

Run all commands from `D:\Tool\WwisePlugin_RealWorldWeatherSound`:

```powershell
$wwise = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928'

# Validate Python, CMake/CTest, VS2022 MSBuild, wp.py, and the Wwise SDK version.
& .\Scripts\Resolve-Environment.ps1 -WwiseRoot $wwise

# Regenerate official projects, then redirect every owned output/intermediate.
& .\Scripts\Configure.ps1 -WwiseRoot $wwise

# Build Core/tests/Offline Renderer and Wwise Runtime + Authoring Release x64.
# The script first builds the Runtime Release(StaticCRT) dependency required
# by the generated shared Runtime project, then the normal Release projects.
& .\Scripts\Build.ps1 -WwiseRoot $wwise

# Parse every PowerShell script, run CTest, and verify local Wwise outputs.
& .\Scripts\Test.ps1 -WwiseRoot $wwise

# Recreate the five-file minimal staging tree and SHA-256 record.
& .\Scripts\Stage.ps1

# Dry-run only; no Wwise files are modified.
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise

# Build the independent Native SoundEngine Host used by v0.3 runtime smoke.
& .\Scripts\Build-NativeHost.ps1 -WwiseRoot $wwise
```

`Configure.ps1` is intentionally repeatable. Audiokinetic Premake recreates the solution/project files first, and `Patch-WwiseProjects.ps1` then performs these bounded rewrites:

1. Runtime static/shared `OutDir` and `IntDir` -> `Build\Wwise\Runtime\...`
2. Authoring `OutDir` and `IntDir` -> `Build\Wwise\Authoring\...`
3. Runtime plug-in `RealWorldWeatherAcousticsSource.lib` dependencies -> the local Runtime build tree
4. Factory Header post-build destination -> `Artifacts\Runtime\include\AK\Plugin`

The isolation check examines only owned output paths, the product Runtime library dependency, and the Factory Header copy. Official Wwise headers and platform libraries remain referenced from the installed SDK and are not duplicated into this repository.

## Output layout

Full development output stays below the product root:

```text
Build/
  Core/                                  CMake projects, objects, tests, renderer
  Wwise/
    Runtime/x64_vc170/Release/           complete Runtime build output
    Authoring/x64/Release/bin/Plugins/   complete Authoring output

Artifacts/
  Authoring/x64/Release/bin/Plugins/
    RealWorldWeatherAcoustics.dll
    RealWorldWeatherAcoustics.xml
  Runtime/x64_vc170/Release/lib/
    RealWorldWeatherAcousticsSource.lib
  Runtime/include/AK/Plugin/
    RealWorldWeatherAcousticsSourceFactory.h
    RealWorldWeatherAcousticsRuntimeAPI.h
  stage-record.json
```

`Artifacts/manifest.json` declares the only allowed staged files. `Stage.ps1` validates every source before clearing the two generated staging subtrees, copies exactly these 5 entries, rejects unexpected staged files, and records file sizes and SHA-256 hashes. Generated artifacts are ignored by Git.

Current staged files from `Artifacts\stage-record.json` (`stagedAtUtc = 2026-07-22T09:46:33.2694904Z`):

| ID | Path | Size |
| --- | --- | ---: |
| `authoring-dll` | `Artifacts\Authoring\x64\Release\bin\Plugins\RealWorldWeatherAcoustics.dll` | 128000 |
| `authoring-xml` | `Artifacts\Authoring\x64\Release\bin\Plugins\RealWorldWeatherAcoustics.xml` | 55101 |
| `runtime-static-library` | `Artifacts\Runtime\x64_vc170\Release\lib\RealWorldWeatherAcousticsSource.lib` | 577940 |
| `runtime-factory-header` | `Artifacts\Runtime\include\AK\Plugin\RealWorldWeatherAcousticsSourceFactory.h` | 1614 |
| `runtime-scene-api-header` | `Artifacts\Runtime\include\AK\Plugin\RealWorldWeatherAcousticsRuntimeAPI.h` | 5545 |

## Authoring installation safety

The default install invocation is a dry-run. It validates the staged DLL/XML and prints their exact sources and destinations without creating directories, backups, or installed files.

Installation requires an explicit opt-in:

```powershell
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise -Apply
```

With `-Apply`, the script:

1. Rejects the operation while the `Wwise` Authoring process is running.
2. Requires the exact existing destination `Authoring\x64\Release\bin\Plugins`.
3. Refuses manifest destinations outside that directory.
4. Backs up existing same-name DLL/XML files under `Artifacts\InstallBackup\<UTC timestamp>`; latest verified backup is `Artifacts\InstallBackup\20260722T094813054Z`.
5. Copies only `RealWorldWeatherAcoustics.dll` and `RealWorldWeatherAcoustics.xml`.
6. Verifies installed SHA-256 hashes against the staged files.

There is no implicit Runtime SDK installation. The staged `.lib`, Factory Header, and RuntimeAPI header are inputs to a Native Host or engine/integration packaging workflow, not files to scatter into the Wwise SDK.

## Wwise Authoring playback and Profiler smoke

After `Stage.ps1` and the explicit `-Apply` installation, run the isolated smoke project with a Python interpreter that can import `waapi-client`:

```powershell
& .\Scripts\Smoke-WwiseAuthoring.ps1 `
    -WwiseRoot $wwise `
    -PythonWithWaapi 'D:\Tool\Wwise_mcp\.venv\Scripts\python.exe'
```

The Python environment is test tooling only; it is not linked into the plug-in and is not needed by product builds or deployed Runtime libraries. The wrapper:

1. Verifies that the installed Authoring DLL/XML match the staged hashes.
2. Refuses to attach to a pre-existing Wwise process.
3. Copies the fixture project under `Build\WwiseSmoke\Projects\<timestamp>` and runs against that disposable copy.
4. Statically verifies the retained rain demo template: `RWWA_Demo_Heavy_Rain_Puddles`, `RWWA_Demo_Heavy_Rain_Puddles_Audio`, `RWWA_Demo_Weather_Geometry_Effect`, and `Play_RWWA_Demo_Heavy_Rain_Puddles`.
5. Uses `effectObjectSet.mode = existing-template` for the Effect playback path, so the smoke reads and plays the retained rain demo rather than creating a duplicate temporary Sound.
6. Creates a temporary programmatic Source regression object, authors the four-material ring, saves the disposable project, and generates Source/Effect SoundBanks.
7. Opens the Authoring UI and verifies the real GUI controls: the Wwise-populated `GeometryEnabled` checkbox, `Add`, `Delete`, and the preview canvas.
8. Verifies Undo gates including Geometry `true -> false -> true`, Add, Feature center drag, radius-handle drag, Delete, Delete key, and Effect Priority `10 -> 107 -> 10`.
9. Starts Profiler capture and Transport playback, then asserts a started non-virtual voice plus finite output peak and Effect CPU evidence above the configured silence floor.
10. Saves structured JSON, stdout/stderr logs, and a Wwise `.prof` capture under `Build\WwiseSmoke`.
11. Stops only the exact Wwise process started by the wrapper unless `-KeepWwiseOpen` is specified, checks that the fixture project is unchanged, and removes the disposable copy unless retention was requested.

The retained rain demo WAV is prepared by:

```powershell
& .\Scripts\Prepare-RainTestAsset.ps1 `
    -InputMp3 'C:\Users\Administrator\Downloads\Envato_Rain_Previews\02_Heavy_Rain_With_Puddles_YDZSTXK_preview.mp3'
```

It writes `WwiseSmoke\RealWorldWeatherAcousticsSmoke\Originals\SFX\RWWA_Heavy_Rain_Puddles_30s.wav`, the first 30 seconds of the user-provided Envato preview. The generated test WAV and generated WEM are repository test assets so a fresh clone can immediately audition and validate the retained rain demo. Publishers must confirm that the Envato preview license covers repository distribution of those derived test assets. Final verified WAV format: 48 kHz stereo PCM24, 30.0 seconds, `8640102` bytes, SHA-256 `1fa150708de00627796a4e3963a9becd97595e01e5e769e6a3b0a5d1cf076adc`.

The retained Wwise demo objects are imported by:

```powershell
& .\Scripts\Import-WwiseRainDemo.ps1 `
    -WwiseRoot $wwise `
    -PythonWithWaapi 'D:\Tool\Wwise_mcp\.venv\Scripts\python.exe'
```

Final import report:

```text
Build\WwiseSmoke\import-rain-demo-20260722T121122186Z.json
```

`Build\WwiseSmoke\PersistentImport\` is only the temporary project-copy/import workspace and remains disposable. The retained Authoring project objects, WAV, and generated WEM are the stable test entry points.

This proves the Authoring binary can be discovered, instantiated, edited through the GUI smoke path, serialized into generated Wwise SoundBanks, executed, and observed inside the target Wwise build. The retained Effect SoundBank fixture is also consumed by the independent Native SoundEngine Host smoke described below.

The final v0.3 evidence file is:

```text
Build\WwiseSmoke\wwise-authoring-smoke-20260723T124436929Z.json
Build\WwiseSmoke\wwise-authoring-smoke-20260723T124436929Z.prof
```

That run recorded wrapper/client `success = true`, `requireRetainedRainDemo = true`, retained rain demo assertions passing, `effectObjectSet.mode = existing-template`, GUI `36/36`, Source/Effect/Shared assertion groups `6/6`, `7/7`, and `1/1`, fixture unchanged, disposable copy matching the fixture, disposable project removed, Feature move/resize through window-message with converged results, Priority `10 -> 107 -> Undo 10`, and actual Wwise SoundBank generation. Source recorded 1 physical voice, CPU `0.1395999938 ms`, and peak `-28.79373550 dB`; Effect recorded 1 voice, CPU `0.1159999967 ms`, and peak `-15.69230461 dB`. The `.prof` is `988049` bytes.

The Source SoundBank serialization gate keeps the 69-parameter, 273-byte current block and 261-byte legacy ABI regression. The Effect gate generated three retained variants: `RWWA_Effect_Baseline.bnk`, `RWWA_Effect_InputRoleWetGeometry.bnk`, and `RWWA_Effect_WetZero.bnk`. Each bank is 473 bytes and contains one 281-byte block for 71 Effect properties. The fixture manifest is `Build\NativeHost\Fixture\20260723T124436929Z\RWWA_Effect_Fixture.json`; all 10 selected artifacts match source/destination size and SHA-256, including `Media\528110025.wem` (`5760096` bytes, SHA-256 `765be0d731a6d25811deb475f74a4019acaf998a93b0c0197e72759e92df1ec5`). Counting the manifest, the retained fixture contains 11 files.

## Offline renderer presets

The offline renderer uses the same Core/DSP as the Wwise Source. After `Build.ps1`, these presets generate deterministic stereo WAV files for quick A/B and regression checks:

```powershell
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset open-wind --output Build\Core\Fixtures\open_wind.wav
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset rain-metal --output Build\Core\Fixtures\rain_metal.wav
& .\Build\Core\bin\Release\rwwa_offline_renderer.exe --preset weather-ring --output Build\Core\Fixtures\weather_ring.wav
```

Preset mapping:

| Offline preset | Authoring preset | Purpose |
| --- | --- | --- |
| `open-wind` | `Open Wind` | Wind-only base layer, no Feature response |
| `rain-metal` | `Rain on Metal` | Rain-only Metal surface response |
| `weather-ring` | `Wind + Rain Ring` | Four-material ring with Rain + Wind masks |

These WAVs are regression artifacts. They do not prove Wwise Authoring discovery, Transport playback, subjective listening approval, SoundBank serialization, or game Runtime integration.

## Completed v0.3 Effect and Native Host boundary

v0.3 is implemented as a Hybrid Audio File Source + Geometry Effect slice, not as a replacement for the Source build. The Source plug-in keeps development `PluginID=31001`, 69 parameters, and its 261/273-byte Bank ABI regression coverage. The Effect uses development `PluginID=31002`, 71 parameters, and a 281-byte parameter block.

Current Effect bank/runtime parameters are `InputRole`, `WetMix`, `ResponseGainDb`, `TransientSensitivity`, weather/listener fields, `FeatureCount`, and 8 fixed feature slots of `X/Y/Z/Radius/Profile/Mask/Priority`. The main Rain Material Lab Authoring panel intentionally exposes only Input Audio, Rain Amount, Surface Mix, Impact Gain, Impact Sharpness, Geometry Response, the listener/canvas controls, and per-surface geometry/material fields. The audible Effect defaults are `Rain`, WetMix `1`, ResponseGain `+10 dB`, TransientSensitivity `0.85`, Rain Amount `0.9`, with Tile/Plastic/Metal/Wood arranged around the listener. `EnvelopeSensitivity`, band weights, smoothing, distance scale, and priority bias remain future-only parameters.

Native Host smoke entry:

```powershell
& .\Scripts\Build-NativeHost.ps1 -WwiseRoot $wwise

& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260723T124436929Z' `
    -Bank 'RWWA_Effect_Baseline.bnk' `
    -SceneJson 'Tools\NativeHost\scene.rain-material-lab.example.json' `
    -Expectation changed `
    -DurationMs 1200 `
    -SkipBuild

& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260723T124436929Z' `
    -Bank 'RWWA_Effect_WetZero.bnk' `
    -SceneJson 'Tools\NativeHost\scene.rain-material-lab.example.json' `
    -Expectation wet-bypass `
    -DurationMs 1200 `
    -SkipBuild

& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260723T124436929Z' `
    -Bank 'RWWA_Effect_Baseline.bnk' `
    -SceneJson 'Tools\NativeHost\scene.disabled.example.json' `
    -Expectation geometry-disabled `
    -DurationMs 1200 `
    -SkipBuild
```

The runtime scene ABI exports `RWWA_RuntimeScene_SetV1`, `RWWA_RuntimeScene_GetV1`, and `RWWA_RuntimeScene_ClearV1`. `RWWA_RuntimeSceneV1` is 392 bytes and contains 8 `RWWA_RuntimeFeatureV1` entries of 40 bytes each. The Host compares all 89 payload fields: 17 scene/header/listener/weather fields plus 9 fields for each of 8 features. Authored Bank/Authoring values are used only when `GetV1` returns `RWWA_RUNTIME_STATUS_UNCLAIMED` and that Effect instance has never claimed a runtime scene. A first-claim `BUSY` transition selects a runtime-owned empty scene instead of authored fallback; a later `BUSY` reuses the retained runtime snapshot when available. Clear/error paths do not silently fall back.

Runtime Diagnostics V1 is a 96-byte C ABI with `RWWA_RuntimeDiagnostics_ResetV1` and `RWWA_RuntimeDiagnostics_GetV1`. It reports execute/frame counts, runtime/authored selection counts, wet-bypass and geometry-disabled reason counts, last revision, input/output peaks, actual wet-difference peaks, non-finite sample count, and whether the last block used a runtime scene. The five `last*` fields are one complete per-block tuple committed through a no-wait try-commit. Under contention that tuple may lag cumulative counters, maxima, and generation, but it never mixes fields from different blocks; contending blocks still contribute to those cumulative values. Publish/reset handshake uses sequential consistency, and Get observes generation last before returning a coherent snapshot. If Get/Reset overlaps publish or another reset it returns `RWWA_RUNTIME_STATUS_BUSY`; control-thread callers retry. Deterministic forced-contention, multi-writer encoding, and race tests cover these transitions.

`Smoke-WwiseNativeHost.ps1 -Expectation` accepts the final reason-aware modes `changed`, `wet-bypass`, and `geometry-disabled`; `transparent` remains only as a legacy compatibility mode. The reason-aware modes gate both the measured wet difference and the corresponding reason counters.

Final Native Host evidence:

| Report | Bank / scene / expectation | Result |
| --- | --- | --- |
| `Build\NativeHost\native-host-rain-material-changed-20260723T124436929Z.json` | Baseline / `scene.rain-material-lab.example.json` / `changed` | 110 executes, 56320 frames, runtime 110, fallback 0, revision 4, 4 features; max input `0.230743408`, output `0.270008653`, wet difference `0.0905741826` |
| `Build\NativeHost\native-host-rain-material-wet-bypass-20260723T124436929Z.json` | WetZero / `scene.rain-material-lab.example.json` / `wet-bypass` | 113 executes, 57856 frames, runtime 113, fallback 0, wet-bypass 113; input=output `0.230743408`, wet difference 0 |
| `Build\NativeHost\native-host-rain-material-geometry-disabled-20260723T124436929Z.json` | **Baseline** / `scene.disabled.example.json` / `geometry-disabled` | 112 executes, 57344 frames, runtime 112, fallback 0, geometry-disabled 112, revision 3; input=output `0.230743408`, wet difference 0 |

All three runs registered 31001/31002, verified the 89-field scene `Set/Get/Clear` payload with mismatch count 0, captured non-finite count 0, loaded `Init.bnk` and the requested bank, posted the event, rendered with 0 failures, and terminated cleanly. The GeometryOff case deliberately uses the Baseline bank plus a disabled runtime scene so only the runtime override closes the geometry path.

Negative reports prove that this is an active gate: a nonzero Baseline response asserted as `wet-bypass` fails both the difference and wet-bypass reason expectations, while a geometry-disabled transparent output asserted as `wet-bypass` fails the reason counter. Both cases terminate at `diagnostics-assertions` with exit code 52.

Unity and Unreal adapters are still future integration work.

## Useful bounded variants

Core-only or Wwise-only builds remain local:

```powershell
& .\Scripts\Build.ps1 -WwiseRoot $wwise -SkipWwise
& .\Scripts\Build.ps1 -WwiseRoot $wwise -SkipCore
```

To validate script syntax, CTest, and project isolation before Wwise binaries exist, omit only artifact checks:

```powershell
& .\Scripts\Test.ps1 -WwiseRoot $wwise -SkipArtifactChecks
```

## Commercial release gate

The development identifiers (`CompanyID=64`, Source `PluginID=31001`, Effect `PluginID=31002`) are not a commercial distribution identity. Before any public or commercial package is produced, obtain and apply Audiokinetic-assigned non-conflicting identifiers, regenerate the official projects/XML, rebuild all binaries, restage, and repeat Authoring, SoundBank, Native Host, and integration validation.
