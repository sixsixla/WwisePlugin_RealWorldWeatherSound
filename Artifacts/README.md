# Staged artifacts

`manifest.json` 是唯一的最小暂存清单。`Scripts/Stage.ps1` 会验证全部源文件存在且非空，清理并重建 `Authoring/` 与 `Runtime/`，然后生成带 SHA-256 的 `stage-record.json`。

暂存内容固定为：

- Authoring：`RealWorldWeatherAcoustics.dll` 与 `RealWorldWeatherAcoustics.xml`
- Runtime：`RealWorldWeatherAcousticsSource.lib` 与 `RealWorldWeatherAcousticsSourceFactory.h`

生成的二进制、Header 副本、安装备份和 `stage-record.json` 不纳入源码管理。Runtime 产物由宿主引擎集成或产品打包流程消费；Authoring 安装脚本只安装清单声明的 DLL/XML。
