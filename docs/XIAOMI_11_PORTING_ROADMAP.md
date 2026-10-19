# Xiaomi 11 (venus) 移植技术路线图

> 阶段每完成一次，更新本文件并同步 AI_CONTEXT.md §9 与 docs/RESEARCH_LOG.md。
> 时间估计均为范围值，标注了依据与不确定性；不把"代码写完"当成功标准。

## 1. 愿景与限制

- 愿景：在 Xiaomi 11 (`venus`, SM8350) 上启动适配后的 XNU xnu-8796 + iOS 16.7.16 用户态，最大化复用 ios-sweet 已验证的 sweet 架构。
- 现实限制：无串口（与 sweet 同）、无 GPU、安全栈原样 stub、AMOLED 1440p@120Hz CPU 渲染性能未知、可能存在 TZ/XPU 级内存准入限制。
- 明确不做：日常可用性承诺；解锁 bootloader 流程本身（已解锁）；Wi-Fi/BT/蜂窝/音频/相机/SEP 的近期路线。

## 2. 当前基线与成功定义

| 层级 | 定义 | 状态 |
|---|---|---|
| L0 内核可构建 | xnu CI 成功出 kernel.development.sweet | [VERIFIED, sweet]（xnu.yml + NOTES 多次 run） |
| L1 探针可见 | shim 在帧缓冲打出第一行字 | [VERIFIED, sweet] / [UNKNOWN, venus] |
| L2 内核早期初始化 | XNU 接管 MMU/中断/timer 并输出 kprintf | [VERIFIED, sweet] / [UNKNOWN, venus] |
| L3 flash-或-boot 可重复 | 同一构建可重复进入 L2（可回退） | 同上 |
| L4 根文件系统加载 | HFS+ root 挂载成功 | [VERIFIED, sweet] |
| L5 用户态启动 | launchd → SpringBoard 主屏 | [VERIFIED, sweet] |

**成功定义即逐层点亮，且每层都可回退。**

## 3. 阶段依赖图

```
S0 安全前提 ──► S1 v3 探针 ──► S2 RAM 侦查 ──► S3 GIC/timer/SMP ──► S4 UFS+payload ──► S5 显示/输入 ──► S6 iOS rootfs → launchd
     │              │              │                                        │
     └── backup ────┴── AVB 侦查 ──┴── 每阶段失败必须可拔电回 fastboot ◄─────┘
```

## 4. 阶段表

| ID | 目标 | 关键任务 | 依赖 | 状态 | 主要风险 | 验收标准 |
|---|---|---|---|---|---|---|
| **S0** | 设备安全 & 侦查前提 | ① adb 只读收集 venus ROM 事实（`getprop`、/proc/device-tree compatible、内核版本、分区表 `ls /dev/block/by-name`、AVB 状态）；② 记录类原生 ROM 的恢复方式（哪些 recovery 镜像可用）；③ 制定 stock boot/vendor_boot 的备份方案（只描述，未执行） | — | **P0 进行中（部分被 adb auth 阻塞）** | 备份缺漏导致无法恢复 | 事实清单入库 RESEARCH_LOG，恢复路径有书面方案 |
| **S1** | venus `fastboot boot` 一个 v3 shim 探针 | 改 `shim/build.sh` → mkbootimg `--header_version 3`；最小探针：帧缓冲画一行字 + 读 SM8350 cntfrq；不加载 XNU、不碰分区 | S0 | P0 待启动 | boot v3 语义差异；venus ABL 或屏幕参数未知；`fastboot boot` 本身可能不工作或需 vendor_boot | 屏幕出现探针文字；失败可拔电回 fastboot |
| **S2** | RAM / carve-out 侦查 | 基于 S1 探针输出 live FDT `/memory`、`reserved-memory`、AML 传的 initrd 参数；确定 XNU phys_base/mem_size | S1 | P0 | TZ/cpucp carve-out 与 sweet 完全不同 | 得到 venus 专属 phys_base/mem_size 并写进 mkadt.venus |
| **S3** | GIC / timer / SMP | 复用 0017（DT virtual-timer-intid）机制；SM8350 GICR per-PE 大小；PSCI CPU_ON 由 shim 二次核 | S2 | P1 | GIC-600 vs GIC500 细节；X1+A78+A55 MPIDR 表 | 4×A55 起 CPU；kprintf 到帧缓冲 |
| **S4** | UFS + payload 装载 | SM8350 UFS 3.1 `ufshc` MMIO 地址与 CAP.64AS 复查（0034 限制）；vendor_boot 或 boot 里放 SWEETXNU payload+ADT | S3 | P1 | DMA >4G 布局；UFSHCI v3.1 寄存器差异 | XNU 完成 `open`/`mount` 的 UFS read-ready 判定 |
| **S5** | 显示 + 输入 + USB 日志 | ABL splash frame buffer 配置（1440×3200，可能需降刷新率）；DSC/ST FTS 触控新驱动；USB CDC-ECM 继承 | S4 | P2 | 1440p 高分辨率帧率压力很大；Samsung FTS 固件协议未知 | 能在 venus 屏幕上画帧；触控按下产生 IOHIDEvent |
| **S6** | iOS 用户态 | 复用 `mkiosroot.sh` + `sweet-root-tweaks.py`；bootargs 换 `sweetfb_*` 尺寸参数；补SweetPlatformExpert 为兼容 venus | S5 | P2 | chip-id 0x8015（A11 伪身份）在 ARMv9 上用户态ABI 兼容性 | launchd 起来、SpringBoard 主屏 |

## 5. P0/P1/P2 分层

- **P0（阻塞推进）**：S1 v3 探针可行性；S2 RAM 窗口；设备恢复路径与备份。
- **P1（启动链必需）**：S3 中断/timer/SMP；S4 UFS/payload。
- **P2（体验）**：S5 显示/输入；S6 用户态。

每阶段 MVE（一次一个变量）：
- S1：只动 boot header 版本（v2→v3），payload 从简。
- S2：探针输出 FDT，不改其他。
- S3：先 1 核，再 4 核，全 8 核。
- S4：只加 UFS 读取 / 不开写（ufsrw=0）。

## 6. 风险登记表

| 风险 | 概率 | 影响 | 缓解 | 触发条件 |
|---|---|---|---|---|
| venus ABL `fastboot boot` 不支持或校验失败 | 中 | 高（阻塞 S1） | 先 S0 查 ROM（类原生可能溢出为 PixelLike）实际 fastboot 版本与命令支持 | S1 第一次 boot |
| boot v3 vendor_boot 拆分理解错误 | 中 | 中（镜像起不来） | 对 stock venus `boot.img` 做 `tools/unpack_bootv2.py`（需扩到 v3） | 第一次打包 |
| SM8350 RAM carve-out 大于 sweet | 高 | 中（须重算寄存器） | S2 实机探针 | S3 前 |
| UFS 3.1 与 2.1 驱动差异 | 中 | 中 | 保留 UFSHCI 通用层加 SoC 分支 | S4 |
| RPMh 缺 vote 导致静默复位 | 中 | 高（无串口难 debug） | 与 sweet 一样靠帧缓冲+持久日志；最早跑出持久 ring | 任一时段 |
| 1440p@120Hz CPU 渲染性能太低 | 高 | 低（体验） | 引入 sweetfb_hz 类似限制 | S5 |

## 7. 首个可执行任务（MVE）

**S1-venus-v3-shim-probe**：
1. 从 venus 类原生 ROM 只读拉一份当前 boot 分区镜像（`adb shell cp /dev/block/by-name/boot /sdcard/` + `adb pull`），用 `tools/unpack_bootv2.py`（需扩 v3）核对真实 header 参数。
2. 在 `E:\Hackios\ios-sweet` 新建 `venus-probe` 分支（不动 main）。
3. 修改 `shim/build.sh`：mkbootimg `--header_version 3`；探针代码：console 开头一行字 + `CNTFRQ_EL0` 打印 + PSCI 版本查询。
4. 在手机侧 `fastboot boot out/shim-boot.img`（= 临时启动，所有 flash 命令禁用）。
5. 观察屏幕 / 后续持久日志。

## 8. 日志采集与回退

- 每次真机实验前后记录：SHA（kernel/payload/image）、命令、屏幕现象、recovery log 末尾 4MiB ring（venus 需修 sweet-log-pull.sh 名字）。
- 回退：`fastboot` 拔电红键；`fastboot reboot` 恢复类原生 ROM；**未备份前严禁一切 flash**。
- 失败分类：立即重启 → RAM/carve-out；黑屏但有 fb → 显示寄存器；完全无输出 → shim/MMU 格式问题。
