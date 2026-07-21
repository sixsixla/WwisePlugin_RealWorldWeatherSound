# RealWorld Weather Acoustics v0.1 Validation Report

## Verdict

The v0.1 executable rain/Authoring slice passes its defined acceptance boundary on Wwise `2023.1.19.8928`, Windows x64, Visual Studio 2022/vc170, Release.

This verdict covers the shared Core, deterministic offline rendering, Wwise Source and Authoring binaries, the fixed-slot 2D Preview implementation, isolated build/staging, minimal Authoring installation, project reload, Transport playback, Voice/CPU observation, non-silent output, and Profiler capture. It does not claim that the future game-side runtime transport or engine adapters are complete.

## Implemented boundary

- Rain only; no wind or thunder renderer yet.
- One Listener with position and yaw.
- Up to 8 `SphereProxy` feature slots; `ActiveK=4` enters DSP.
- Metal, Wood, Glass, and Tile response profiles.
- One response profile per feature; no front/back response switch.
- No sky exposure, indoor/outdoor, enclosure, or `receivesRain` fields. Registration itself is the participation whitelist; `ResponseMask` selects modules.
- Stereo output; no Ambisonics claim in this slice.
- Three Authoring presets, 2D circles, one draggable Listener point, and a draggable yaw arrow; no Listener Path.
- Authoring fixed slots and Runtime Source use the same Wwise PropertySet and the same shared Core/DSP.

## Fresh verification evidence

| Claim | Result | Evidence |
| --- | --- | --- |
| PowerShell scripts parse | PASS | `Scripts/Test.ps1` reported `PowerShell syntax validation passed.` |
| Core and offline tests | PASS | CTest: 2/2 passed, 0 failed |
| Deterministic offline render | PASS | `validation_a.wav` and `validation_b.wav` are both 576,044 bytes and SHA-256 `7773F6BBFDA2E2046B4989E9E22CBF42043AD77318960276C30E83DA0FEAD7D8` |
| Saved Wwise project reload | PASS | `WwiseConsole verify --abort-on-load-issues --verbose` loaded all Work Units and completed successfully |
| Installed plug-in discovered and instantiated | PASS | WAAPI created Source class ID `2031682562` in the isolated smoke project |
| Authoring Transport executes the real Source | PASS | Transport state `playing`; 1 total Voice, 1 physical Voice, non-virtual and started |
| Plug-in executes in Wwise profiler | PASS | `RealWorld Weather Acoustics` Source row: 1 instance, about `0.0478 ms` exclusive in the sampled frame |
| Output is non-silent | PASS | Output peak `-34.514984 dB`, above the smoke floor of `-90 dB` |
| Profiler capture saved | PASS | Required smoke assertion passed; `.prof` size 369,119 bytes; SHA-256 `7637E9B7380F54C3B489B0184439A7AC51A4C5AB2D35BBBE52EC777B4AD8E7BF` |
| Wrapper cleanup | PASS | No mandatory cleanup errors; wrapper stopped only the Wwise process it launched |

Primary dynamic evidence:

- `Build/WwiseSmoke/wwise-authoring-smoke-20260721T110307505Z.json`
- `Build/WwiseSmoke/wwise-authoring-smoke-20260721T110307505Z.prof`
- `Build/Core/Fixtures/validation_a.wav`
- `Build/Core/Fixtures/validation_b.wav`
- `WwiseSmoke/RealWorldWeatherAcousticsSmoke/RealWorldWeatherAcousticsSmoke.wproj`

The JSON report records the authored four-material ring, exact project identity, every WAAPI step, Transport state, Voice rows, Source CPU row, Performance Monitor values, assertions, and cleanup state.

## Staged and installed files

The complete source, generated projects, intermediates, binaries, tests, smoke project, and validation outputs remain under:

```text
D:\Tool\WwisePlugin_RealWorldWeatherSound
```

The staged v0.1 package contains four files:

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| Authoring `RealWorldWeatherAcoustics.dll` | 70,656 | `1CF834674E1641569D2136AC7E10561A4E3120ED95B8A3181079BED449E61C30` |
| Authoring `RealWorldWeatherAcoustics.xml` | 27,005 | `0A4ED851FD66BDCCAECA5712457AB7F762938BF46600EA4952FE1E300FB3B311` |
| Runtime `RealWorldWeatherAcousticsSource.lib` | 231,810 | `8875E7FAC86E4A86A46C1AF55D63A7B51DA1297F59AC90259846FC00DD05966C` |
| Runtime `RealWorldWeatherAcousticsSourceFactory.h` | 1,512 | `D7227F57C422DEE6DAB622FE6F59528C0D2685AE2A3B055E20B2ACA0AE8219C6` |

Only the Authoring DLL and XML were copied into Wwise:

```text
E:\WwiseSoft2023\Wwise_2023.1.19.8928\Authoring\x64\Release\bin\Plugins\RealWorldWeatherAcoustics.dll
E:\WwiseSoft2023\Wwise_2023.1.19.8928\Authoring\x64\Release\bin\Plugins\RealWorldWeatherAcoustics.xml
```

Their installed hashes match the staged hashes. No Runtime library, Factory Header, source tree, generated project, PDB, or build directory was copied into the Wwise installation/SDK.

## Reproduction

Run from the product root:

```powershell
$wwise = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928'

& .\Scripts\Configure.ps1 -WwiseRoot $wwise
& .\Scripts\Build.ps1 -WwiseRoot $wwise
& .\Scripts\Test.ps1 -WwiseRoot $wwise
& .\Scripts\Stage.ps1
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise -Apply
& .\Scripts\Smoke-WwiseAuthoring.ps1 `
    -WwiseRoot $wwise `
    -PythonWithWaapi 'D:\Tool\Wwise_mcp\.venv\Scripts\python.exe'
```

The explicit Python path is local smoke-test tooling only and is not a product build/runtime dependency.

## Known gaps and next proof points

- The 2D canvas compiled and linked into the real Authoring DLL, and its property/drag logic has source review coverage. Automated smoke did not open the plug-in editor and visually drag the controls; a short manual UX/screenshot pass remains useful.
- SoundBank serialization and a standalone Native SoundEngine Host have not yet executed the staged Runtime `.lib` and Factory Header end to end.
- Game-side static object registry, C ABI, versioned Scene Snapshot, Custom Game Data/Native Registry choice, Capture/Replay, contribution Monitor, and MCP/Sandbox data plane are P0-B work.
- Unity and Unreal adapters, wind, thunder, Ambisonics, multi-listener, non-Windows platforms, and complete product packaging are not implemented.
- Development IDs are internal only: `CompanyID=64`, `PluginID=31001`. Public/commercial distribution requires Audiokinetic-assigned non-conflicting IDs and a full rebuild/revalidation.
