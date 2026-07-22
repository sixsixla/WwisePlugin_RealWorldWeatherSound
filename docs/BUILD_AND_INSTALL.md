# Build, validation, staging, and Authoring installation

## Supported environment

v0.2 intentionally targets one ABI/toolchain combination:

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

# Recreate the four-file minimal staging tree and SHA-256 record.
& .\Scripts\Stage.ps1

# Dry-run only; no Wwise files are modified.
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise
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
  stage-record.json
```

`Artifacts/manifest.json` declares the only allowed staged files. `Stage.ps1` validates every source before clearing the two generated staging subtrees, copies only those entries, rejects unexpected staged files, and records file sizes and SHA-256 hashes. Generated artifacts are ignored by Git.

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
4. Backs up existing same-name DLL/XML files under `Artifacts\InstallBackup\<UTC timestamp>`.
5. Copies only `RealWorldWeatherAcoustics.dll` and `RealWorldWeatherAcoustics.xml`.
6. Verifies installed SHA-256 hashes against the staged files.

There is no implicit Runtime SDK installation. The staged `.lib` and Factory Header are inputs to an engine/integration packaging workflow, not files to scatter into the Wwise SDK.

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
4. Creates a Source Plug-in instance, authors the four-material ring, and saves the disposable project.
5. Opens the Authoring UI and verifies the real GUI controls: the Wwise-populated `GeometryEnabled` checkbox, `Add Feature`, `Delete Feature`, and the preview canvas.
6. Verifies five Undo gates: Geometry `true -> false -> true`, Add, Feature center drag, radius-handle drag, and Delete button. It also verifies Delete key deletion.
7. Starts Profiler capture and Transport playback, then asserts a started non-virtual voice or matching Source CPU evidence plus a finite output peak above the configured silence floor.
8. Saves structured JSON, stdout/stderr logs, and a Wwise `.prof` capture under `Build\WwiseSmoke`.
9. Stops only the exact Wwise process started by the wrapper unless `-KeepWwiseOpen` is specified, checks that the fixture project is unchanged, and removes the disposable copy unless retention was requested.

This proves the Authoring binary can be discovered, instantiated, edited through the v0.2 GUI smoke path, serialized into generated Wwise SoundBanks, executed, and observed inside the target Wwise build. It does not yet prove that a generated bank loads and executes in an independent Native SoundEngine Host or game-side integration; those are the next runtime milestone.

The final v0.2 evidence file is:

```text
Build\WwiseSmoke\wwise-authoring-smoke-20260722T035623735Z.json
Build\WwiseSmoke\wwise-authoring-smoke-20260722T035623735Z.prof
```

That run recorded wrapper/client `success = true`, all GUI assertions true, fixture unchanged, disposable copy matching the fixture, disposable project removed, `inspectorTextMatchesStablePreset = true` for 13 visible Inspector edit texts (`60`, `0.75`, `24681357`, `20`, `4 / 8`, `14`, `35`, `0.65`, and Feature1 `0`, `6`, `2.5`, `3`, `10`), Add complete defaults true, 6 Undo gates including canvas Delete key Undo, actual Wwise SoundBank generation and Authoring parameter serialization, `transportState = playing`, 1 physical voice, `OutputPeak = -28.6425437927246 dB`, Source CPU `0.1396999955177307 ms`, and a `368867` byte `.prof`.

The SoundBank serialization gate generated Geometry true/false banks, each `457` bytes. The expected `273`-byte parameter block matched exactly once in each bank at offset `77`; `GeometryEnabled` was at block offset `16` and bank offset `93`, with true byte `1` and false byte `0`. The two parameter blocks differed only at offset `16`, generation logs contained only `Message` severities, and Geometry was restored to `true`.

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

The development identifiers (`CompanyID=64`, `PluginID=31001`) are not a commercial distribution identity. Before any public or commercial package is produced, obtain and apply Audiokinetic-assigned non-conflicting identifiers, regenerate the official projects/XML, rebuild all binaries, restage, and repeat Authoring discovery/creation/Transport playback validation.
