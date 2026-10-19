# DECISIONS.md — 架构决策记录（ADR 风格）

> 决策按 ID 递增；状态：Proposed / Accepted / Superseded。
> 复杂技术选型都写这里，避免散落在 NOTES。

## ADR-0001 仓库与工作区布局 — Accepted (2026-10-19)

- **决策**：仓库 = `https://github.com/Duncan588/ios-sweet`（即用户自己的 fork）；本地克隆 `E:\Hackios\ios-sweet`，浅克隆 depth=50；长期不要求本地完整深度（如需 log 可 `git fetch --unshallow`）。
- **原因**：笔记本直连 GitHub 443 不通，Windows 侧可达；文档/分析阶段不需要 Linux 构建环境。
- **替代方案**：①笔记本配代理再克隆（拒绝：额外配置、且非当前必需）；②直接用 GitHub raw 分析（拒绝：无法做全面 grep/补丁细节核对）。
- **影响**：Linux 侧后续构建时再以 bundle 或代理方式同步，不用重复下载。

## ADR-0002 长期文档本地生成、不推送 — Accepted (2026-10-19)

- **决策**：`AI_CONTEXT.md` 与 `docs/*` 6 篇初稿全部在 Windows 本地生成；暂不推送到 Linux 侧，需要输出"文件生成"而不是"逻辑迁移"，避免遗漏。
- **原因**：当前处于只做审计与文档阶段；把 Linux 作为目标一齐搬可能导致重复劳动。
- **替代方案**：直接在 Linux 笔记本做文档（拒绝：直连网络不通）。

## ADR-0003 工具边界 — Accepted (2026-10-19)

- **决策**：只用 adb / fastboot（Android platform-tools）与其他开源工具；禁止 MiFlash、小米助手、Mi Unlock 等一切小米官方工具。
- **原因**：用户明确要求；风险点集中在官方工具里不易审计。
- **影响**：所有备份/恢复动作都通过 `adb pull`/`ls /dev/block/by-name/` + 回调方法实现。

## ADR-0004 设备状态 & 信息来源 — Accepted (2026-10-19)

- **决策**：bootloader **已解锁**；当前运行类原生 ROM；venus 侧操作以 adb 只读为主，任何重大 fastboot 命令（尤其带写入语义的）都需用户明确授权。
- **一致说明**："fastboot boot" ≠ 零风险，只允许探针类镜像、禁止直接写任何分区。

## ADR-0005 移植代码组织 — Proposed

- **决策**：在 sweet 上游 base 上新建 `venus` 分支，之后设备差异主通过：
  - `tools/mkadt.py --soc venus`（或单独 `mkadt_venus.py`）参数化设备常量；
  - shim 里引入 `include/soc/` 目录（`soc_sweet.h` / `soc_venus.h`）声明 GIC/FB/UFS 地址，替换目前分散的硬编码；
  - `xnu/patches` 保持编号的含义不变，venus 专有 patch 用 `8xxx-venus-*.patch` 或同场新文件组（作最终一轮讨论）。
- **原因**：把"sweet 硬编码"上移到具名头文件，兼顾两个 SoC 共存。
- **替代方案**：直接 fork sweet 全套打补丁（拒绝：难审计、AI 代理上下文不友好）。
- **状态**：Proposed，待 S1 实验后完成第一版拆分。

## ADR-0006 内核构建路径保持不变 — Proposed

- **决策**：XNU 本体仍然走 GitHub Actions macOS runner（Xcode 15 + iig ABI 匹配）；Linux 不承担 XNU 构建。
- **原因**：`xnu.yml:37-39` 已明确 iig ABI 独此一条；换本地构建系统改造的成本远大于 CI 循环成本。
- **影响**：S1-S6 每次都要跟 CI 往返；建议**一次提交多个 kernel 常量实验一次性打包**（同 dxb patch 表）。

## ADR-0007 身份伪装策略复用 — Proposed

- **决策**：沿用 chip-id 0x8015 + iPhone10,3/D22AP 身份伪装，用户态采用 iOS 16.7.16 原始二进制（与 sweet 相同策略）。
- **原因**：用户态与 SoC 解耦；sweet 上已验证整条链，最小化变量。
- **风险**：ARMv9（SM8350）与 pmap/特权行为差异需在 S3/S4 实验确认；若用户态对内核 ABI 有隐性假设，S6 起需专项 trace。
- **影响**：venus 用户态异常时，按"逐片符号化 + sweet-crash.py 手段"定位，不轻易改用户态。
