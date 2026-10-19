# RESEARCH_LOG — ios-sweet → Xiaomi 11 (venus) 调查记录

> 规则：每条调查记录时间倒序追加；来源（URL+版本 或 本地路径:行号）必须可复现。
> 本文件与上游 `NOTES.md` 职责区分：NOTES.md 记录 sweet 上游每次会话完整工程日志（不动）；本文件只记 venus 移植调查与跨设备结论。

## 2026-10-19 — 首轮审计（五路并行子代理）

### 环境
- Windows 主机可达 github.com；Linux 笔记本直连 443 超时（[REJECTED] 两次尝试），故仓库在 Windows 侧克隆。
- 手机连笔记本；笔记本端 `adb devices` 显示 `unauthorized`，等待手机上确认 USB 调试授权。
- 命令经济：本日 terminal 调用 ≤5 条（含 ssh/git 各一次）。

### 结论 1 — 仓库现状 [VERIFIED]
- 基线：main @ `fdad0de`（xnu 0081：ue0 上报 Ethernet link），上一个 `2683b6a`（notes v15, 0078-0080）。
- 目录：`shim/`（裸金属引导 C+asm）、`xnu/patches/`（81 patch + 3 stage .sh）、`tools/`（18 个脚本）、`.github/workflows/xnu.yml`（唯一 CI）。
- 原项目自述已到 iOS 主屏 sweet；无 venus 专有代码。证据：`README.md:5-8`、`NOTES.md:21`。

### 结论 2 — sweet 真实启动链 [VERIFIED]
```
Xiaomi/Qualcomm ABL（解锁 BL，boot v2）
  → fastboot boot shim/out/shim-boot.img（临时启动）
  → start.S（arm64 Linux Image 头，flags 0xa）
  → shim_main（shim.c:373）
  → loader_boot：payload "SWEETXNU" magic 校验
       拷 XNU 段到 phys_base+(vmaddr-virt_base)
       复制 ADT；构造 boot_args v2（长度 736）
       x0=boot_args 物理地址 → dsb+ic iallu → 裸跳（MMU off）
  → XNU（16K 页）SweetPlatformExpert → HFS+ root（0047/0026）
  → launchd → SpringBoard（甜化身份 iPhone10,3）
```
- 详细行号：`shim/start.S:5-128`、`shim/loader.c:68-145`、`shim/mmu.c:42-44,91-105`、`tools/mkpayload.py:11-22,158-169`、`tools/mkadt.py:13-34,104-147,215-224`。
- 证据类别：C 源码 + README/NOTES 描述 + QEMU 与实机日志多次复现。

### 结论 3 — CI 构建约束 [VERIFIED]
- macos-14，Xcode 15（iig ABI 与 xnu-8796 匹配；Xcode 16 会产 IORPC.kernelContent 不兼容 — `xnu.yml:37-39`）。
- `dxb` pin @ `663906528d1cd5ea8fde7de6f16de73db9511549`（`xnu.yml:29-30`）。
- artifact：`xnu-kernel`、`xnu-buildtree`，`if: always()`；download 不等于构建成功（`if-no-files-found: warn`）。
- SWEET 补丁表（官方）：`README.md:165-188`（0004 machine config、0016 platform expert、0017 device-tree timer intid、0065 PSCI SMP、0072-0077 USB、0042/0044/0046/0047/0049 显示与 HID，等）。

### 结论 4 — venus 硬件事实（外部一手/准一手）
- SoC=SM8350 / Kryo 685（X1×1+3×A78+4×A55）/ Adreno 660：LineageOS venus wiki + Google Android Common sm8350.dtsi @2fff00c8。
- RAM：6/8/12GB LPDDR5；上游 reserved-memory 有 hyp/tz/aop/cpucp/removed_mem 等；真实可用窗口未在本仓库实测。
- Boot：**v3 + vendor_boot（96MB）+ A/B**、boot 192MB、AVB flags=3（解锁后）：Lineage venus BoardConfig + PixelExperience install wiki。
- UFS：3.1；MMIO 未当作事实证据；DWC3/PHY 位置须 venus.dtsi 查。
- Display：1440×3200 120Hz AMOLED（DSC，panel-k2）。
- Touch：Samsung ST FTS（st_fts_k2.ftb）。
- PMIC：PMK8350 + PM8350/c/b。
- Risk 最高 Top5 与缓解方案写入 docs/HARDWARE_COMPARISON.md。

### 结论 5 — 关键工程结论
- `tools/mkadt.py` 是唯一设备常量集中点，移植"main 改点" （~12 处硬编码：PHYS_BASE/MEM_SIZE/SOC_BASE/GICD/GICR/FB/UFS/TIMEBASE/VTIMER_INTID/CPU 拓扑/chip-id/D22AP 身份/boot-manifest-hash）。
- `shim/mmu.c`/`shim/console.h`/`shim/ufs.c`/`shim/shim.c` 每文件同样有 sweet 硬编码常量组；`mmu.c:86-87` 限"镜像跨 ≤2 个 32MB block"，XNU kernel 变大要复核。
- ARMv9 引发甜蜜阻力：0011 `physmap-alignment-padding`、0004 Kryo470 cache-line、0078 8 核表等 patch 都不能原样通吃，需**venus 分支**。

### 首轮实机对话事实（用户口述，[S] 待核证）
- bootloader **已解锁**；当前运行类原生（non-MIUI）ROM。
- 小米官方工具（MiFlash/小米助手等）一律禁用；只允许 adb/fastboot（Android platform-tools）与开源工具。
- 手机通过笔记本宿主侧 adb 操作（Windows 侧 adb 看不到设备）。

### 未解决问题
- venus 实机 RAM carve-outs / ABL 传参：需 S1 探针输出 FDT。
- venus `fastboot boot` 对自定义 boot 的真实支持状态。
- UFS 3.1 的 MMIO、CAP.64AS、PHY 分组。
- ST FTS 触摸固件加载时序。

---

## 2026-10-19 — venus 实机 adb 只读事实（子代理采集，Stage-0 一部分）

- 【设备】M2011K2C（小米 11 中国版 8+256），device=venus；LOS 23.2-20261007-NIGHTLY-venus（Android 16）；内核 5.4.302-qgki；【活动槽 _b】cmdline `slot_suffix=_b`。
- 【boot 格式 v3 + A/B 自证】by-name 含 boot/vendor_boot/dtbo/vbmeta/vbmeta_system 双槽全套。
- 【UFS 同址利好】cmdline `androidboot.bootdevice=1d84000.ufshc` —— UFSHCI MMIO 与 sweet 的 0x01d84000 **完全同址**，`shim/ufs.c:16` 常量初值直接可用（3.1 版本差异仍待验）。
- 【AVB 状态】`verifiedbootstate=orange`（解锁态，与 ADR-0004 一致），bootchain 内 secureboot=1 不阻 fastboot boot；vbmeta 双槽存在。
- 【面板自证部分】cmdline `msm_drm.dsi_display0=qcom,mdss_dsi_k2_38_08_0a_mp_dsc_cmd`（DSC cmd 模式，与外部资料 panel-k2 互证；分辨率数值未直出）。
- 【不可自证（无 root）】/proc/device-tree model/compatible 权限拒绝；GIC/timer/PMIC/触控协议待 root 或 venus 内核源码取证。
- 【空间】/data 225G，可空间 138G（备份余地充足）。
- 【记录形态】分区表 129 项细节见子代理会话报告；关键遗留：boot/vendor_boot 字节级大小（已开补充采集任务）。
- 【遗漏风险】venus boot_b 实际可容纳 boot v3 镜像大小上限待字节级确认；`fastboot getvar unlocked` 需用户批准后再做。

```
## YYYY-MM-DD — 任务名
### 查过什么
### 关键发现（附证据）
### 未解决问题 / 阻塞
```
