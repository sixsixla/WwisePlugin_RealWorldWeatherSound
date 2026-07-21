# Build, validation, staging, and Authoring installation

## Supported environment

v1 intentionally targets one ABI/toolchain combination:

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
3. Starts the isolated project under `WwiseSmoke`, creates a Source Plug-in instance, authors the four-material ring, and saves it.
4. Starts Profiler capture and Transport playback, then asserts a started non-virtual voice or matching Source CPU evidence plus a finite output peak above the configured silence floor.
5. Saves structured JSON, stdout/stderr logs, and a Wwise `.prof` capture under `Build\WwiseSmoke`.
6. Stops only the exact Wwise process started by the wrapper unless `-KeepWwiseOpen` is specified.

This proves the Authoring binary can be discovered, instantiated, executed, and observed inside the target Wwise build. It does not yet prove SoundBank serialization or a game-side Native SoundEngine integration; those are the next runtime milestone.

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
