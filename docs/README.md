# Documentation Map

本目录是 RealWorld Weather Acoustics 的文档入口。核心产品与 v0.4 方案由本独立仓库维护；Project_J 或其他游戏工程只实现可选 Host Adapter，不再保存方案副本。

## 当前应读文档

| 文档 | 作用 | 状态/真源规则 |
| --- | --- | --- |
| [`../README.md`](../README.md) | 仓库入口、当前可运行能力与快速操作 | 只做摘要，不复制完整设计 |
| [`V0_4_LISTENER_CENTERED_FOA_PLAN.md`](V0_4_LISTENER_CENTERED_FOA_PLAN.md) | Bake-first 声场、Renderer、动态 Overlay、Surface Patch、执行阶段与停止条件 | **v0.4 唯一权威设计与执行计划；尚未实现** |
| [`PRODUCT_PLAN.md`](PRODUCT_PLAN.md) | 产品范围、版本决策与历史演进 | 记录“为何这样决策”，不替代 v0.4 执行合同 |
| [`V0_3_HYBRID_AUDIO_PLAN.md`](V0_3_HYBRID_AUDIO_PLAN.md) | 当前 v0.3 Hybrid 切片的实现边界 | 已实现状态说明；事实仍以代码和测试为准 |
| [`VALIDATION_REPORT.md`](VALIDATION_REPORT.md) | v0.3 构建、ABI、Native Host、Wwise Smoke 与 Profiler 证据 | 已实现能力的主要证据索引 |
| [`USER_GUIDE_ZH.md`](USER_GUIDE_ZH.md) | 安装后试听、Authoring 操作与调音 | 当前 v0.3 使用指南 |
| [`BUILD_AND_INSTALL.md`](BUILD_AND_INSTALL.md) | 构建、测试、Stage 与安装 | 当前 v0.3 工程流程 |

## 历史文档

| 文档 | 保留原因 |
| --- | --- |
| [`V0_2_FIX_PLAN.md`](V0_2_FIX_PLAN.md) | v0.2 修复与兼容决策历史 |

历史文档不能覆盖当前代码事实或 v0.4 权威计划。出现冲突时按以下优先级处理：

1. v0.3 已实现行为：代码、自动化测试、`VALIDATION_REPORT.md`。
2. v0.4 尚未实现行为：`V0_4_LISTENER_CENTERED_FOA_PLAN.md`。
3. 产品动机与演进：`PRODUCT_PLAN.md`。
4. README、指南和历史计划只提供入口或背景。

## 当前执行入口

v0.4 第一条切片固定为“静态地面雨扩展源 + 单墙/门洞 + 单 Listener 轨迹”，按 Bake、Runtime sampler、论文 Mono→Stereo Renderer 的顺序验证。Bake 未证明有稳定收益前，不提前实现 HOA Renderer 或 `WeatherSurfaceGranulatorSource`。详细交付、仓库落点、A/B 路线和停止条件全部维护在 v0.4 权威计划中。
