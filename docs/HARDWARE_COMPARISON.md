# HARDWARE_COMPARISON — sweet (Redmi Note 10 Pro) vs venus (Xiaomi 11)

> 可信度标记：[V] = VERIFIED（代码/日志/实机），[S] = SUPPORTED（一手/准一手资料，未实机），[H] = HYPOTHESIS，[U] = UNKNOWN，[R] = REJECTED。
> 来源格式：本地路径:行号 或 URL（外部均需记录）。更新记录见 docs/RESEARCH_LOG.md。

## 一、事实矩阵

| 项目 | `sweet`（Redmi Note 10 Pro, SM7150） | 来源 | `venus`（Xiaomi 11, SM8350） | 来源 | 可信 |
|---|---|---|---|---|---|
| SoC | Snapdragon 732G，Kryo 470 = 2×A76 (2.304GHz) + 6×A55 (1.8048GHz)，ARMv8.2，无 PAC | `README.md:5,194`；`NOTES.md:60` | Snapdragon 888，Kryo 685 = 1×X1 @2.84 + 3×A78 @2.42 + 4×A55 @1.80，**ARMv9**，Adreno 660 | https://wiki.lineageos.org/devices/venus/ | [V] / [S] |
| PSCI | smc，CPU_ON 已验证 6 核/8 核 | `README.md:194`；patch 0065/0078 | `arm,psci-1.0` + `method=smc`（上游 dtsi） | sm8350.dtsi @2fff00c8 | [V] / [S] |
| GIC | GICv3（GIC500），vtimer/PPI INTID 19，Group 1 IRQ | `README.md:193`；patch 0017 | GICv3（硬件标注 GIC-600，待验 GICR per-PE size）| sm8350.dtsi + SM8350 内核源注释 | [V] / [S] |
| ARM timer | 19.2 MHz | `README.md:194`；`tools/mkadt.py:30` | 通常 19.2 MHz，仍需在 venus 实机读 `CNTFRQ_EL0` 验证 | 高通平台通用约定（准一手） | [V] / [S] |
| MMU/页 | 16K granule; TCR T0SZ=28, 36-bit VA, IPS 36-bit | `shim/mmu.c:42-44` | 数值待按 SM8350 RAM 量定 | shim 源码别名 | [V] / [H] |
| RAM | low bank 0xb0000000/0x4d000000；`--highmem` 0x1_00000000/0xfcc00000（≈3.95GiB 上 bank 实测可写） | `tools/mkadt.py:13-19`；`NOTES.md:152` | 6/8/12 GB LPDDR5，基址 0x80000000；low bank 固定 carve-outs（hyp/tz/aop/cpucp/removed_mem 等），**真实可用窗口 UNKNOWN** | sm8350.dtsi reserved-memory 节点（布局仅静态源码，未实机） | [V] / **[U]** |
| Boot 镜像 | Android boot **v2**，单 boot，payload+dtb 打包一体；max-dl 768MiB | `shim/build.sh:29-33`；`NOTES.md:180` | Android boot **v3 + vendor_boot（96MB）+ dtbo**，A/B 设备，boot 192MB，pagesize 4096 | https://github.com/LineageOS/android_device_xiaomi_sm8350-common/blob/lineage-23.2/BoardConfigCommon.mk | [V] / [S] |
| AVB | 未做校验（解锁后自定义 v2 生效 boot 直接） | 本仓库 fastboot boot 经验 | AVB 处于启用状态（flags=3 会在解锁后较宽松），仍校验 vbmeta/vendor_boot 链 | PixelExperience venus install wiki | [V] / [S] |
| fastboot 支持 | `fastboot boot` 可用且基本可靠 | `NOTES.md:46` | 支持 `fastboot flash/reboot`；`fastboot boot` 在小米 ABL 上需 venus 实机验证 | PixelExperience wiki + 实测未做 | [V] / [H] |
| UFS | UFSHCI 2.1 @ 0x01d84000， userdata sda34（107 GiB），LUN0；>4G DMA 掠过 disabled（CAP.64AS=0） | `shim/ufs.c:16`；`NOTES.md:176-179,152` | UFS 3.1（或 3.x），128/256 GB；MMIO 地址和 CAP 未知 | https://wiki.itsvixano.me/devices/venus/ | [V] / [S] + [U] |
| 显示 | 1080×2400 DSC video mode @ ABL splash cont_splash 内存 0x9c000000/0x1700000 | `README.md:195`；`mkadt.py:25-27` | 6.81" 1440×3200 Samsung AMOLED DSC，120Hz（panel-k2-38-08-0a dsc-cmd）；splash 帧缓冲地址不得乱借 | https://github.com/Demon000/kernel_xiaomi_sm8350 commit aac7b59 | [V] / [S] |
| 触控 | Goodix GT9896 @ I²C 0x5d，GENI SE7 0xa84000，LOAD LDO11 | `README.md:196`；patch 0060/0062 | Samsung ST FTS（`st_fts_k2.ftb` 固件），I²C； GENI 基建可复用 | Lineage sm8350-common device.mk | [V] / [S] |
| PMIC | PM6150 / PM6150L；SPMI PON/GPIO2 按 SID4；RPMh LDO11 vote 触控 | `README.md:196-197`；patch 0062 | PMK8350 + PM8350/c/b 家族；PON 与 RPMh 路径需按 sm8350 dtsi 对照 | https://git.yoctoproject.org/linux-yocto-dev/plain/arch/arm64/boot/dts/qcom/pmk8350.dtsi | [V] / [S] |
| USB | DWC3 @0x0a600000，QUSB2 PHY @0x088e2000，Qualcomm wrapper VBUS QSCRATCH 0xa6f8800 | patch 0072-0074（常量） | SM8350 也用 DWC3，但地址/PHY（QUSB2 v5.0 + QMP v5.00）/wrapper offset 不同，需查 venus.dtsi | sm8350.dtsi | [V] / [S] + [U] |
| ECID/chip-id 模拟 | chip-id 0x8015（伪装 A11）, iPhone10,3/D22AP 身份 | `mkadt.py:104-105,215-224` | 同样策略可行（SM8350 兼容信息写入 `mkadt venus` 分支） | [H]（复用策略） | [H] |
| 串口 | 无调试 UART（接为 SPI），帧缓冲 + 4MiB userdata ring + USB CDC-ECM UDP 日志 | `README.md:48`；`NOTES.md:13` | 同样无调试 UART；EDL 9008 与 XBL/UEFI log ring 保留 | dtsi reserved_xbl_uefi_log + sweet 方案 | [V] / [S] |
| 上游 Linux 主线 | postmarketOS sm7150 主线 6.6.1 已有 Goodix/interconnect/DispCC | `NOTES.md` | LineageOS 23.2 (Android 16) 采用 5.4 vendor kernel；postmarketOS 主线 venus 无官方页 | lineage wiki | [S] |

## 二、最大差异 / 风险 Top5（详细缓解策略见 docs/XIAOMI_11_PORTING_ROADMAP.md）

| # | 差异 | 后果 | 缓解 |
|---|---|---|---|
| 1 | **boot v3 + vendor_boot 拆分 + AVB** | shim 现有 v2 打包完全无法在 venus boot；vendor_boot 的 ramdisk 语义不同 | S1 探针重写 build.sh；必要时拆 payload → vendor_boot 或 boot ramdisk 段 |
| 2 | **ARMv9 + X1/A78/A55 异质拓扑** | sweet 的 ARMv8.2 补丁假设（8 核同构、cache line 6、Kryo470）不再成立；MPIDR/时钟拓扑必须重建 | mkadt 重建 CPU 表；建议先 `cpus=4` 仅 A55 小核做 S3 验证 |
| 3 | **RAM carve-out 布局 UNKNOWN** | phys_base/mem_size 一错，MMU init 即重启，无串口难 debug | S2 借 S1 探针 dump live FDT 并在屏幕/UART 输出 |
| 4 | **UFS 3.1 + 可选 IOMMU 路由不同** | sweet_ufs.c 需 SoC 分支 | 复用读写框架，MMIO 常量用 venus.dtsi 实测 |
| 5 | **1440p@120Hz AMOLED + Samsung FTS** | CPU 渲染 9MB/帧 大幅压力；触控新驱动 | 引入 sweetfb_hz 限帧；FTS 新写 I²C 协议驱动 |
