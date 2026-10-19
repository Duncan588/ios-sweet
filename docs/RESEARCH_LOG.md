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

## 2026-10-19（预估）— 真机探针 runbook 就绪

- 【交付】`tools/venus_boot_probe.sh`（笔记本侧运行；前置检查 adb/fastboot、40s 超时轮询、getvar unlocked/current-slot/max-download-size 各一行、`fastboot boot <img>` 前后时间戳、全输出存 `logs/venus-probe-<ts>.log`、完成后**不自动重启**并打印"观察窗口开始"；脚本内无任何 flash/erase/format）。`tools/venus_recover_note.md` 为 Windows+笔记本双端黑屏恢复速查（等 30s → Power+音量下 10s → fastboot devices 验证；完全无响应则拔线 5s 重插；**绝不使用小米官方工具**）。
- 【执行要点】连接方式：Windows 侧 `ssh notebook`，adb/fastboot 仅在笔记本；venus 活动槽 _b；`fastboot boot` 临时启动不写分区；BL 已解锁（verifiedbootstate=orange），ADR-0008 已自动授权本实验；root 为 APatch（`su -c`，不认 `-qc`）。镜像路径做参数传入。
- 【状态】脚本与速查已提交 venus-probe 分支；本地 Windows 无 bash，未执行 `bash -n`，仅目视核对（标注：未执行），首跑须笔记本端验证。恢复动作按速查文档执行并补记回本日志。
---

## 2026-10-19（第四次）— venus dump 解析（boot v3/v4 / dtbo / RAM carve / FB）[VERIFIED-物证]

- 【输入 [SHA-256 已核]】`E:\Hackios\Xiaomi11\` 下 dump：boot_b=98CB9CCA36AA49C18100A9FEE369E8FC4294CD29EB3E44DE3C93AFEBAA8839B4、vendor_boot_b=F0999813278EC3DB24A7DBA13597E599399B50CB1AA5A36F2B929D63FE8FAE2A、dtbo_b=EEBAFBA086EF48318B7500B95515EDC27CF57E129E8E64E3671C42CE93947018。全只读处理，产物在 Xiaomi11/out 与 Xiaomi11/dtb（不进 git）。
- 【boot 头 [CONFIRMED]】venus boot_b = **header v3**（非 v4）：kernel 39,623,136B、ramdisk 1,489,270B、cmdline 空、os_version 0x200001a9、无 base/offset/tags 字段（v3 语义）。build.sh `--venus` 默认 VENUS_HV=3 **正确，未改**。kernel 已解出到 Xiaomi11/out/boot_b/kernel。
- 【vendor_boot 头 [CONFIRMED]】VNDRBOOT **v3**：page_size=4096，kernel 槽（32KB 字段口径）实际是 gzip 流，解压为 20,133,880B 标准 cpio 新奇异值：小米把"kernel"槽用了 vendor RAMDISK，非 GKI 标准布局，已按字节验证（解压流以 070701 开头）。
- 【vendor cmdline（页0）】`androidboot.console=ttyMSM0 androidboot.hardware=qcom androidboot.memcg=1 androidboot.usbcontroller=a600000.dwc3 cgroup.memory=nokmem,nosocket console=ttyMSM0,115200n8 loop.max_part=7 msm_rtb.filter=0x237 service_locator.enable=1 swiotlb=0 pcie_ports=compat iptable_raw.raw_before_defrag=1 ip6table_raw.raw_before_defrag=1`
- 【vendor RAMDISK 清单】标准 GKI 通用 ramdisk（227 项）：init、init.recovery.qcom.rc、first_stage_ramdisk/fstab.default、fsck.f2fs/e2fsck/dump.f2fs/linker64 等；**ramdisk 内无任何 dtb/dtbo/\*_init.s**（完整清单 Xiaomi11/out/vendor_ramdisk_list.txt；本 cpio 全文 Xiaomi11/out/vendor_boot_blob0.bin）。
- 【vendor_boot 内嵌 DTB】dtb 区 17 个 SoC 级基树：qcom,lahaina (v1/v2/v2.1)、lahainap x3、shima、yupik/yupikp(yupik-iot/yupikp-iot) 全家（Xiaomi11/dtb/vboot_dtb_00..16.dtb）。main=dtb_04/05/06=Lahaina。Board 档（mtp/venus 面板）来自 dtbo 覆盖，非 vendor_boot 本体。
- 【dtbo】dt_table v0，magic **0xD7B7AB1E**（BE），header 0x20、entry 32B x73、全 entry id=0 rev=0，全部为标准 FDT overlay（fragment@N）。entry **46 = "Venus based on ... SM8350"（qcom,board-id=65544）**。逐 entry 解包 Xiaomi11/dtb/dtbo_*.dtb。
- 【FB base【SOLVED】】Lahaina SoC DTB `/reserved-memory/splash_region` **reg = 0xEA600000 / size 0x2300000**，label "cont_splash_region"（venus entry46 未覆盖 splash 区域）。`VENUS_FB_BASE = 0xEA600000`（原占位 0x9d000000 已弃）；dfps 数据区 0xEC900000/0x100000 紧随其后。
- 【FB 几何【SOLVED，高置信】】dtbo entry46 真机面板 `xiaomi 38 08 0a cmd mode dsc`（与 adb cmdline `qcom,mdss_dsi_k2_38_08_0a_mp_dsc_cmd` 互证）：FHD **W=0x438(1080) H=0x960(2400)**、QHD **W=0x5A0(1440) H=0xC80(3200)**。默认 FHD -> `VENUS_FB_W=1080 / H=2400 / STRIDE=4320`（bpp DT 未给，a8r8g8b8 假设保留，首绘异常再换）；用户若开 QHD+，FB 应切 1440x3200。
- 【GIC [CONFIRMED=shim.c 无需改]】`/soc/interrupt-controller@17a00000`：compatible arm,gic-v3，**reg = <0x17A00000 0x10000  0x17A60000 0x100000>**，#redistributor-regions=1，redistributor-stride=**0x20000**。与 shim.c 现值完全一致。
- 【timer【四 PPI 齐解；shim.c 冲突】】SoC DTB `/timer`（arm,armv8-timer）interrupts（DT 顺序=secure phys、ns phys、virtual、hypervisor）：
  1. raw 13 -> INTID **29**（secure physical）
  2. raw 14 -> INTID **30**（non-secure physical，cntp_ctl_el0 非安全物理定时器）
  3. raw 11 -> INTID **27**（**virtual timer，cntv_ctl_el0 → VENUS_VTIMER_INTID = 27**）
  4. raw 12 -> INTID **28**（hypervisor）
  clock-frequency 0x124F800 = **19.2 MHz**。
  警告：shim.c:36-37 硬编码 VTIMER 19 / PTIMER 18（732G 外部资料口径），lahaina DT 不支持 —— 实机 fastboot boot 时 vtimer=19 收不到 IRQ。本次合规只改 soc_venus.h；shim.c 需 venus 侧把 VTIMER->27、PTIMER(cntp)->30（venus_probe 编译选项可 -D 覆盖），宏表已写入 soc_venus.h。
- 【ufshc】`/soc/ufshc@1d84000`：reg = <0x1D84000 0x3000  0x1D88000 0x8000  0x1D90000 0x9000>（ufs_mem/ufs_ice/ufs_ice_hwkm），IRQ <0 0x109 4>（SPI 265 -> INTID 297）。与 sweet 同址结论维持。
- 【reserved-memory 全表 [EXPORTED]】SoC DTB 完整表：hyp 0x80000000/0x600000、xbl_aop 0x80600000/0x260000、cmd-db 0x80860000/0x20000、xbl_uefi_log 0x80880000/0x14000、smem 0x80900000/0x200000、cpucp 0x80B00000/0x100000、cdsp_secure_heap 0x80C00000/0x4600000、pil_camera 0x85200000/0x500000、pil_video 0x85700000/0x500000、pil_cvp 0x85C00000/0x500000、pil_adsp 0x86100000/0x3900000、pil_slpi 0x89A00000/0x2F00000、pil_cdsp 0x8C900000/0x1E00000、ipa_fw 0x8E700000/0x10000、ipa_gsi 0x8E710000/0xA000、gpu 0x8E71A000/0x2000、spss 0x8E800000/0x100000、modem 0x8F000000/0x10000000、removed 0xD8800000、**splash 0xEA600000/0x2300000**、demura x2 (0,0)、dfps 0xEC900000/0x100000。/memory@ 在 SoC 基树为空（bank 表 bootloader 注入），S1 FDT dump 可复核。证据：Xiaomi11/out/vboot_dtb_04/05/06.dts、dtb_nodes.txt、dtbo_entry46_venus.dts。
- 【HARDWARE_COMPARISON 冲突标注】其中的 GIC/timer（INTID 19/18、splash 0x9c000000/0x1700000）**属 sweet(sm7150)/外部732G 资料**，对 lahaina(venus) 不适用；拷贝常量务必以本条 + soc_venus.h 为准。
- 【soc_venus.h 新值 [COMMITTED]】VENUS_FB_BASE=0xEA600000（原 0x9d000000 占位）、VENUS_FB_W/H/STRIDE=1080/2400/4320、VENUS_VTIMER_INTID=27（附 30/28/29 表）、GIC 校验注记。build.sh 未改（VENUS_HV=3 即正确默认）。
- 【遗留】1) 面板 bpp/pixel-format DT 未给出（a8r8g8b8 为假设，实测异常先换 BGRX）。2) QHD 下 FB 需 1440x3200（shim 当前 FHD 优先）。3) boot_b 1,489,270B ramdisk 未解（GKI generic，探针打包用注入 ramdisk，无必要）。4) qemu-venus.py 冒烟走 virt 机（vtimer=27 同值），无需变更。

## 2026-10-19 ─ venus probe v3（多基址横扫 + 10s 自证 PSCI 关机链）

- 【改动【COMMITTED 本节同 commit】】\shim/venus_probe.c\ 升 v3：(1) banner 头行改为 \VENUS PROBE v3\；(2) FB 绘画循环扫 3 个候选基址 {0xEA600000(DT splash_region), 0xEA800000(+32MiB), 0x9C000000(sm7150 历史)}，首区域 clear(BLUE)+全信息 banner，其余区域只重置行号后重画 banner（标注 base 值，不清屏，供 ABL 残留内容对照）；(3) banner 完成后打 \probe v3 done, system_off next\ → CNTFRQ×10 忙等 ≈10s → PSCI SYSTEM_OFF(0x84000008)，返回非 0 立即 SYSTEM_RESET(0x84000009)，conduit 仍读 DTB /psci method 自适应（QEMU=hvc/真机预期=smc）；v3 语义=关机/重启/回 ABL，不再 wfe 挂屏；(4) \enus_probe_entry\ 收尾 return 1 修掉 venus_probe.c:86 non-void 警告且保证不 fallthrough 到 loader（shim.c:388 hook 语义核对：探针路径 return 已处理）。配套 \shim/console.\{c,h\}\ 新增 con_reset_row()（仅重置 row=0，不动绘画逻辑；超出仅改 venus_probe.c 的原始指示，因行号静态在 console.c 无法从外部复位）。
- 【假设 (a) websearch 校准】主线 Linux Mi 11 Ultra (star, 毒瘤 sm8350) DT（wuxilin 2023 linux-arm-msm 列表 patch 3/3）chosen 就有 simple-framebuffer 前屏 reg=0xEA600000 尺寸 1440*3200*4 stride=1440*4 a8r8g8b8，注释『initialized by bootloader』→ 反向证据：SM8350 小米 ABL 交给主线内核时面板已点亮且 splash 基址/几何/格式与我们 QHD 编译档一致。即「ABL 对第三方 boot 完全不点屏」最弱；(a) 修正为更可能「ABL fastboot boot 路径会清/不续写 splash、或 bpp/扫描格式与我们假设不同」，v3 多基址横扫即为分辨 (b) relocate vs 格式错误设计。
- 【实验流程约定】Windows: \git bundle create E:\\Hackios\\vp6.bundle d95da3f..venus-probe\ → scp 到 notebook:~/ → 笔记本 \git -C ~/ios-sweet-vp4 fetch ~/vp6.bundle HEAD:vp6 && git merge --ff-only vp6\ → \cd ~/ios-sweet-vp4/shim && bash build.sh --venus 3 qhd\ → \ash tools/venus_boot_probe.sh shim/out/shim-boot-venus.img\（fastboot devices 无输出即停，等用户再重启手机）。
- 【用户观察口径（下一次 boot）】~10s 后自动关机=v3 全链成功 / 自动重启=SYSTEM_OFF 被拒转 RESET / 屏幕闪出蓝色屏+文字=某候选基址命中（记录哪个 base 行）。

- 【S2 mkadt_venus（commit 02593d3, venus-probe）】`tools/mkadt_venus.py` 复用 mkadt.py：/memory reg=三 bank、/chosen dram-base/size=首 bank(0x80000000/0x37500000)、disp0/fbcon 1440x3200、身份 D22AP+0x8015（ADR-0007）、**random-seed 固定为 bytes(range(64))**（跨机 byte-identical）；产物 `shim/out/venus-adt.bin` 10056B，sha256 Win/Linux c3dceb02c0616e89c25d1d2428bdb65eb2d0d125dde6a38245b48819fa1c3e1d（notebook `python3 tools/mkadt_venus.py` 复算一致）。已知：/product 节点缺失——两台机都没有 research/ipsw-x/d22ap.adt（Apple 工件，git 外），出表征即补。下一站：把 venus ADT+kernel 走 qemu-sweet.py loader 路径冒烟。

## 2026-10-19（第五次）─ venus probe v4（FB 基址运行时从 DTB 读取 ─ 活机 splash_region 证据）

- 【根因 [CONFIRMED-活机]】三次黑屏的原因是我们的 shim 画到了错误地址：静态 vendor_boot SoC DTB 的 `/reserved-memory/splash_region reg = 0xEA600000` 已被活机数据否定。活机 Linux 运行时 DT（内核 fdt，fdt_idx=4/dtbo_idx=46，model "Venus based on ... SM8350"，QHD 1440x3200 cmd 面板）导出为 `fdt_live.bin`，用 `Xiaomi11/tools/fdt_min.py` 解析得 **splash_region = base 0xE2A00000 size 0x2300000**。台账保留两点：(1) 静态 SoC DTB 值 0xEA600000 是 bootloader 未覆盖时的残留档；(2) ABL fastboot boot 时会给 shim 传自己的 DTB（x0），解析需自适应其值（v4 即为此设计）。工具说明：`dtb_nodes.py` 是早期手写解析器，对 NOP tag/嵌套跳动容忍不足（活机 DT 中有 FDT_NOP），已被 `fdt_min.py`（Xiaomi11/tools/，临时产物，不入 git）取代。
- 【RAM banks [CONFIRMED-活机]】三条：`0x80000000/0x37500000`、`0xC0000000/0x140000000`、`0x200000000/0x180000000`（即 941MB + 5GB + 6GB ≈ 12GB）。
- 【改动 [COMMITTED 本节 commit]】v4：(1) `shim/fdt.{c,h}` 新增 `fdt_get_reg(dtb, path, &base, &size)`——公共路径 walk 抽成 `fdt_walk_prop`，reg 支持 16B（2+2 cells，BE 双 u64，活机布局）与 8B（1+1 cells）两种编码；(2) `shim/venus_probe.c`：entry 首步即 `fdt_get_reg(dtb, "/reserved-memory/splash_region", ...)`，成功→fb_bases[0]=读到的 base，失败→回落 `VENUS_FB_BASE`；候选数组改为运行时填充 {base, base+0x02000000, 0x9C000000}（横扫保留）；banner 后新增 `FB base=... size=...` 与 `FB-SRC`（dtb 读到 or 回落）决策行；banner 改 "VENUS PROBE v4"；(3) `shim/soc_venus.h`：`VENUS_FB_BASE 默认改 0xE2A00000`（注释写明静态 dts 0xEA600000 已被活机数据否定，保留 VENUS_FB_BASE_STATIC_DTS 文档值）；QHD 就地生效（venus_probe.c `#define VENUS_PANEL_QHD 1`，ocv/未定义双路径安全，build.sh --venus 不带 qhd 参数即 QHD）。
- 【QEMU 冒烟口径调整】新 shim 从 DTB 读基址而 QEMU virt 生成 DTB 无 /reserved-memory/splash_region → 必走回落 0xE2A00000；qemu-venus.py 的 fb_base 正则仍解析 soc_venus.h 的 VENUS_FB_BASE 字面值，恰好与新回落值一致（其 assert 只约束 VENUS_FB_STRIDE 形状，无碍）。QEMU 4G RAM 覆盖 0xE2A00000..0xE4D00000，快照/解码可正常判 `venus_probe_ok`；若 fb.txt 空则按 v4 降级判据：result.json+qemu.log 无 panic 即通过。
- 【验证链】真机：`bash tools/venus_boot_probe.sh shim/out/shim-boot-venus.img`（venus_boot_probe.sh 不变，SYSTEM_OFF 尾段+10s 观察窗沿用 v3，不做二次 boot）。观察口径：屏幕应出现 "VENUS PROBE v4" + FB base/size/来源三行；若 fb 地址行 FB-SRC 显示回落且仍黑屏 → 下一步怀疑 dtbo 覆盖后的最终 DTB 或 ABL 传参路径。
## 2026-10-19（第六次）─ S2.5 首次把 SWEETXNU payload 灌入 venus 真机（kernel.development.sweet + venus ADT）
- 【载荷 [DONE]】kernel = CI run 39193640899 的 xnu/out/kernel.development.sweet（17,206,064B，sha256 de315bfaf63fc53e71028881321c74b71276ee657b31038a41cab2afa0f7dccf，与 xnu/out/SHA256SUMS 逐字节一致）；ADT = shim/out/venus-adt.bin（c3dceb02…a1c3e1d，双机一致）。tools/mkpayload.py 未改动（其现有 VMADDR/layout 逻辑对 venus ADT 直接适用）；无 --ramdisk（真机先跑无 root 的最小 shell，root fork 属 S3）。cmdline: debug=0xc serial=3 keepsyms=1 -noprogress（fbcon reg 0xE2A00000 已在 ADT 配好）。产物 shim/out/payload-venu0.bin：16,610,928B，sha256 65f3a115e53687ca5ec8fe2326ffb107a878114a1ebd28935b7cc2706348f21b，fixups=0（CI 内核未含 LC_DYLD_CHAINED_FIXUPS，属预期），virt_base 0xfffffe0006000000，entry 0xfffffe000748b488，kernel_end 0xfffffe00084c8000。
- 【镜像 [DONE]】笔记本（~/ios-sweet-vp4 先 ff 到 b82f729；同步方式：bundle 前必须先查对端 HEAD——02593d3..b82f729 的 bundle 因笔记本缺 02593d3 基点而 fetch 失败，改 9a16744..b82f729 成功）：shim/build.sh --venus 3（v3 头，dtb 不打包告警属预期，ABL 传真 DTB）→ shim/out/shim-boot-venus.img 16,805,888B sha256 80bea8afd0c54df20c0ed6ba473354d24cb54547594c51654beb37c0ae22b1b1；kernel 188,416B；unpack_boot.py 反验 ramdisk 与 payload 字节一致（UNPACK-CMP-OK）。
- 【QEMU 取舍 [SKIPPED]】按计划跳过 loader 路径 QEMU 冒烟：两者的动机是验证 shim 家件跳进 xnu，但 sweet 的 qemu-sweet.py 无 root fork 时 xnu 会在最早期 panic（即使如此也只覆盖到与真机等价或更少的信息），probe 路径（qemu-venus.py）不识别 SWEETXNU payload；shim 家件与 fastboot boot 通路已被 S1 v4 真机（蓝底白字实践）验证。因此判定直接真机的信息增益更大。
- 【真机 boot [DONE→FAILED-UNKNOWN] 2026-10-19 13:19:36-13:19:37】venus_boot_probe.sh 正常走完：adb 在线 e528afbe → reboot bootloader 9s → fastboot unlocked=yes 变量未回读（getvar unlocked 出现 waiting 字样，current-slot=b，max-download 805306368）→ Sending 16412KB OKAY 0.933s、Booting OKAY 0.199s → 观察窗开始。+35s 起 fastboot/adb 双离线（多次复检均空）。无法从 Windows/ssh 侧确证屏幕内容；判定为未知失败（黑屏可能性大，需用户长按电源恢复并回收目击信息）。仅此一次，未做第二次 boot（按约）。
- 【失败候选排序（未验证，按概率直觉）】【S2.5/S3 归属待定】(1) xnu 在 fbcon 起来之前就 panic——ADT /product 节点缺失（两机均无 research/ipsw-x/d22ap.adt，Apple 工件）：S1 前例显示 xnu console 有 sweet-fbcon 兜底不至于完全无字，但不能排除极早期 fault（tlb/cache 行为在 SM8350 已知差异是候选）；(2) shim 对 16.6MB payload 的多段拷贝在真机内存时序上有坑（S1 只拷贝过 188KB 的 probe 段）；(3) fastboot boot 大 ramdisk 传参后 shim 未正确拿到 boot_args 某字段。下一轮建议：换最小 cmdline 复跑一次（用户授权后）or 在 shim 里给 SWEETXNU 路径加一行 banner（加载成功即将跳 xnu），把失败定位在 xnu 前/后。

## 2026-10-19（第七次）─ S5: initrd 载体 payload 区间扫描（修复 milestone 5 "no SWEETXNU payload"）
- 【根因 [CONFIRMED-真机照片]】用户实拍 milestone 5 屏：INITRD lo/hi 打印为有效值 + "no SWEETXNU payload" + **UFS 分区清单完整输出 → UFS 驱动在 venus 上只读活体工作 PASS（S4 第一项提前验收，ufs.c 未动也不许动）**。根因 = 小米 ABL 传给 shim 的 initrd 是「vendor ramdisk cpio 拼接 SWEETXNU payload」，magic 不在首字节，loader 旧逻辑只查 rd_start 头 8 字节。
- 【改动 [COMMITTED 本次]】shim/loader.c：payload 定位改为区间扫描——rd_start..rd_end 内 4 字节步进 linear search "SWEETXNU"（8 字节 head 比较，复用 magic_ok）；命中打印 `INITRD payload found at +0x<off> (size 0x<n>)`；所有 payload 相对 offset（segs[].file_off / adt_off / 头部对齐检查 rd_start&7 改 (rd_start+scanned)&7）与基准改为 rd_start+scanned；未命中打印 `no SWEETXNU payload (scanned all of initrd)` 后走原 reset 路径，m5 勘探画面留用。性能：~17MB 4-byte stride 数 ms，MMU off 1:1 无 TLB 处理（不改 cache 逻辑，sweet 同读法）。已并核两侧 magic 字面量：mkpayload 产物首 64B hex `5357454554584e55010000000a000000`（版本 1、10 段）与 loader.c "SWEETXNU" 一致。
- 【验证 [BUILT 真机 boot 见 NOTES handoff】笔记本 build.sh --venus 3（payload 不重打）；真机 boot 通过 tools/venus_boot_probe.sh，观察窗口 45-60s，不二次 boot。
- 【观察对照表】A = `INITRD payload found` + `JUMP` + JUMPXNU 齐 → XNU 已跳，之后白字（含 panic 文本）都是 xnu 打的（0xE2A00000 同 FB）＝下一步抓 panic；B = 只有 found 无 JUMP → loader 死在跳转前一步；C = 仍 no payload → ABL cpio 拼接格式理解有误，下一步 dump initrd 首 64MB 到屏 hex（或 cpio TRAILER 解析）。

## 2026-10-19（第八次）─ venus 蓝屏 SYNC 根因 = reset() 写 sweet IMEM 地址 + dump 被二次覆盖
- 【根因[CONFIRMED-反汇编]】shim.elf VMA=0 基、Image 装载于 0xA0080000：ELR 0xA0081938 = `reset.constprop.0+0x74` 的 `str w1,[x0]`（x0=0x146aa65c、w1=0x77665500），LR 0x18e4 = 同函数 con_str("RESET","in 40s") 返回点。实拍蓝屏是**第二次异常**：第一次异常（真凶，ELR 在 loader 阶段，值未及记录）→ exc_handler 画蓝 → reset(40) → 40s 后写 0x146aa65c → venus 上 data abort（ESR 0x96000010 DFSC=0x10 sync external，FAR=X0=0x146aa65c，X1=w 寄存器立即数形态）→ 第二次 exc_handler 重画蓝覆盖第一张 → 每约 40s 循环。红屏 3-5s 后消失 = INITRD 行打印后进入 35MB 字节步进扫描即撞第一异常（DUMP/LOADER 行从未到达）。
- 【修复[COMMITTED]】shim/shim.c：①VENUS_PROBE 下 IMEM 写改为 venus 真地址 `0x146bf65c`（venus ABL DTB qcom,msm-imem@146bf000+restart_reason@65c，见 E:\Hackios\Xiaomi11\out\vboot_dtb_04..06.dts；sweet 的 0x146aa65c 原值保留）；② exc_handler 在 VENUS_PROBE 下 dump 后 HOLD（WFE），不再进 reset() —— 下一 boot 蓝屏即第一现场；③ reset() 写 IMEM 前先打 RSTREASON 行。shim/loader.c：payload 头 deref 前加固（version==1、nsegs 1..16、mem_size/kernel_end∈(0,0xC0000000]、phys/virt/entry 非 0、adt_off/命中本址 8B 对齐），拒 vendor cpio 假 SWEETXNU。sweet 默认构建不变。
- 【观察对照表[下一 boot]】A: 蓝屏一次后 HOLD 常驻（新行 HOLD/“exception screen stays”）→ 第一现场到手，报屏上 ESR/ELR/FAR（预期落在 loader_boot 扫描段）；B: INITRD “payload found at +0x…” + JUMP/JUMPXNU → 扫描活，继续看 xnu；C: “bad payload header” → 假命中被拒，需扩 dump 定位真 magic；D: 蓝屏仍每 ~40s 换新值（有 RESET 行出现）→ 修错方向，按屏上 ELR/FAR 重启调查。

## 2026-10-19（第九次）─ v2 payload 直通 + loader 两处误判修复 + **XNU 首亮（venus 上 XNU 取得控制权）**
- 【上游对照[VERIFIED-README]】sweet 上游用 boot v2 头（mirror PixelOS），payload 是唯一 ramdisk，ABL 直传 initrd=payload 首字节——无 vendor cpio 拼接。venus 之前用 v3 触发 GKI vendor 拼接＝35MB 混流＝magic 找不到的根因。
- 【改动[COMMITTED]】shim/build.sh：--venus 接受 2；v2 打包需 --dtb（mkbootimg v2 强制），用 E:\\Hackios\\Xiaomi11\\dtb\\vboot_dtb_04.dtb（dtb_idx=4 活机对应）。
- 【boot(14:18)+实拍】v2 镜像 INITRD=lo AF027000 hi AFFFE670=0xFD7670≈payload 0xFD57F0；payload found at +0x00000000＝v2 策略成立；随后 ad payload header。
- 【误判#1[CONFIRMED+修复 680b729]】8165861 的头部加固把 kernel_end 当物理大小（>0xC0000000 拒），实测 payload +0x30 kernel_end=0xfffffe00084c8000 是 VA ⇒ 拒掉真 payload。修复为：virt_base/entry/phys 非 0，mem_size<=0x100000000，kernel_end∈[virt_base, virt_base+0x20000000)。
- 【boot(14:39)+实拍】PHYS/VIRT/ENTRY 打印＝头通过；随后 
amdisk overlaps kernel memory。
- 【误判#2[CONFIRMED+修复 02b70b1]】重叠检查用 mem_size=0x37500000（RAM bank 尺寸，交给 XNU 的 memSize）而非内核跨度；initrd 0xAF027000 与内核目标 0x80000000..kernel_end(+ADT+2*16K) 并不重叠。改为按 kernel_end 跨度判定。
- 【boot(14:49)+实拍＝里程碑】**XNU panic 页**：panic(cpu 0 caller 0xfffffe0007ad6490): Undefined kernel instruction: pc=0xfffffe000749… instr=d51be228 @sleh.c:951＋handshake structure not initialized＋panic.apple.com 提示＝loader/复制/ADT/boot_args/入口跳转全被 XNU 接受，S3 首亮达成。
- 【panic 解码[VERIFIED-本地位域+OSS sleh.c]】d51be228=MSR (register) 写 **CNTP_CTL_EL0**（S3_3_C14_C2_1，RT=x8）→ UNDEFINED trap，sleh.c handle_uncategorized panic。俩假说：(H-EL2) shim/XNU 运行在 EL2，(op0=3,op1=3) 在 EL2 上统一 UNDEFINED；(H-PTIMER) venus/SM8350 把物理 timer 关掉或不解码。sleh.c 全文已存本地 greps；handshake structure not initialized 字符串不在 sleh.c（另一来源，待定位）。
- 【已知不挡路】test_gic 的 GICR priority 写炸（6e29ec1 已 skip）；红屏 DUMP 部分被异常页覆盖的问题已由 loader 后直接 HOLD 解决。
- 【agent NOTE】第一次 XNU-panic 分析子代理（ses_ead0c374…）completed 无输出：未发现其留下的 commit/status 改动（git 状态干净，xnu/patches 未动），如报告缺失需重做其任务。

## 第十次：payload 官方对齐 rebuild（2026-10-19，venus-probe）

- 目的：把 venus 的 payload/cmdline/构建组装对齐官方 dipa-studio/ios-sweet 标准流程；venus 此前 cmdline 只有 4 个参数，远少于官方 phone 镜像整串。偏离清单（只列不改）写入 NOTES.md 小节 **official-alignment A**（10 条，含 RAM 放置、CPU 节点未适配、loader 扫描语义等，各自注明保留理由）。
- 参数裁决：**cpus=1** 保守起步（官方 "cpus=6 starts the six A55 cores; cpus=1 safe fallback"；venus big.LITTLE 拓扑与 sweet 不同且 ADT CPU 节点未适配 SM8350，SMP 留待后续换 cpus=N）；**rd=ufsroot0 ufsrw=1 rootrw=1 带上**（用户还没刷 iOS root，带上有妨无害——无 root 时 panic 语义变 root not found，也是情报）；**sweetfb_noconsole=1 / sweetfb_hz=30 不带**（venus fbcon 是当前唯一输出通道，不能静音 console）。
- 内核不动不换：xnu/out/kernel.development.sweet sha256 复核 de315bfa…f7dccf（CI run 39193640899），与第九次一致，避免与 XNU 侧修复（CNTP_CTL_EL0 panic）冲突。ADT 沿用 venus-adt.bin（c3dceb02…a1c3e1d）。
- 产物：shim/out/payload-venu0.bin 16,610,928B sha256 **26e7a2ea312df7f76e19e9bcfe05a1200a34c7c4a466d2b78013466eaac58cff**（fixups=0、virt_base/entry 与第九次逐字段一致——只有 cmdline 变化）。
- 发射：bundle→scp→ff 笔记本 ~/ios-sweet-vp4 → build.sh --venus 2（v2 头 + /home/monster/vboot_dtb_04.dtb，沿用 19b 成功通路）→ tools/venus_boot_probe.sh 真机（本会话预算 2 次 fastboot boot）。结果补记于本节末尾与 NOTES.md 置顶 UPDATE。
