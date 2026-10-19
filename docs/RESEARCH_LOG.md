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
- 【不可自证（无 root）】/proc/device-tree model/compatible 权限拒绝（shell 用户）→ **已补：root（APatch）通道读通 compatible = qcom,lahaina**；GIC/timer/PMIC/触控协议尚待 venus 内核源码或后续 FDT dump。
- 【空间】/data 225G，可空间 138G（备份余地充足）。
- 【记录形态】分区表 129 项细节见子代理会话报告；关键遗留：boot/vendor_boot 字节级大小（已开补充采集任务）。
- 【遗漏风险】venus boot_b 实际可容纳 boot v3 镜像大小上限待字节级确认；`fastboot getvar unlocked` 需用户批准后再做。

## 2026-10-19（第二次）— 分区字节级取证（root 通道已确认）

- 【分区大小 [VERIFIED]】boot_a/b=192MiB、vendor_boot_a/b=96MiB、dtbo_a/b=24MiB、vbmeta=128KiB、devinfo=16MiB（`blockdev --getsize64`，root 通道全成）。
- 【root 通道 [VERIFIED]】APatch su 存在；它的 su 不支持 `-qc` 组合，用 `-c`；已用它读通 `/proc/device-tree/compatible` → `qcom,lahaina-mtp / qcom,lahaina / qcom,mtp`。**后续 S2 RAM/carveout dump（/proc/iomem、device-tree reserved-memory）都有通道了**。
- 【打包空间 [SUPPORTED]】boot_b 192MiB 覆盖 mkbootimg v3/v4 单镜像 ~64MiB 头部上限约 3 倍；vendor_boot 96MiB、dtbo 24MiB 充足——shim v3 打包体积不构成瓶颈。子代理把 768MiB 与单镜像上限区分（前者是 fastboot max-download 载荷限制），结论采纳，标注 [SUPPORTED] 待实跑验证。
- 【CPU 冷备位】kernel_max=31（8 核 lahaina 属正常）；/proc/gcc 不存在（跳过）。
- 【设备安全】全程只读，未 reboot、未写分区、未进 fastboot。

## 2026-10-19（第三次）— CI 基线构建成功 [VERIFIED]

- fork Actions 首次启用（`PUT /actions/permissions` enabled=true），基线 `xnu.yml` @ main（源 commit 处于 fdad0de 同源状态）→ run **39193640899**，conclusion=success，总时长 7m47s（Build 主体 ~7m01s）。
- artifacts：`xnu-kernel` 57MB（已下载至本地 `E:\Hackios\ios-sweet\xnu\out`，其中 `kernel.development.sweet` 17,206,064B + `SHA256SUMS`）、`xnu-buildtree` 22MB（buildtree 未下载，留本地重放按需）。
- 意义：macos-14 + Xcode 15/iig 构建链对用户 fork 有效性获得实证；venus 分支可复用该 workflow，仅换 MACHINE_CONFIG / 补丁组。
- 交接：https://github.com/Duncan588/ios-sweet/actions/runs/39193640899
- Linux 同步状态：`~/ios-sweet` @ `e29ba8a`（含 docs 5 件 + AI_CONTEXT），后续小 diff（root 通道 RamPresent 行、CI 结论）在下次 S1 提交时一并带过去。

### S1 具体缺口（唯一优先任务分解）
1. `tools/unpack_bootv2.py` 扩展 boot header v3/v4（当前对 v3 除零报错）[已证实缺口]
2. `shim/build.sh` 的 mkbootimg v3 参数 / dtbo 挂接方式（venus 的 dtb 在 dtbo_b，24MiB）
3. 从设备只读 dump：`su -c 'dd if=/dev/block/by-name/dtbo_b of=/sdcard/dtbo_b.img'` + adb pull（root 通道已验证可用；执行前须用户拍板"仅读、放 /sdcard"）

```
## YYYY-MM-DD — 任务名
### 查过什么
### 关键发现（附证据）
### 未解决问题 / 阻塞
```
