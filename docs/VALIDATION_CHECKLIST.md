# VALIDATION_CHECKLIST.md — 每阶段可重复验证清单

> 使用规则：每完成一项就打勾 + 注（日期 + 证据文件路径/日志行）；全部完成才允许进入下一阶段。
> 所有命令默认在 `E:\Hackios\ios-sweet` 工作区；真机命令一律经由笔记本（adb/fastboot）。

## Stage 0 — 设备安全 & 侦查

- [ ] adb 可访问 venus（笔记本端 `adb devices` 显示 device，非 unauthorized）
- [ ] 已记录：`ro.product.device` / `ro.product.model` / `ro.build.version.release` / ROM 名
- [ ] 已记录：bootloader 现状（用户已确认解锁；后续用 `fastboot getvar unlocked` 实际取证）
- [ ] 已记录：分区表 `ls /dev/block/by-name/`（截图或文本记入 RESEARCH_LOG）
- [ ] 已记录：stock boot/vendor_boot/dtbo 的分区名和大小
- [ ] 备份方案写成文档（不执行任何写盘；backup 的 shard 校验列在 ai_context §12）
- [ ] 明确列出差恢复路径：类原生 ROM 的 fastboot 官方包 / 第三方 recovery 的镜像来源

## Stage 1 — v3 shim 探针

- [x] 已用 `tools/unpack_boot.py`（自写，v3 头支持）解出 stock venus boot：header_version=**3**（非 v4），kernel 39,623,136B，cmdline 空（os_version 0x200001a9）；vendor_boot v3 + 227 项 cpio + 17 个 SoC 基树（证据 E:\Hackios\Xiaomi11\out\boot_b、vendor_ramdisk_list.txt，2026-10-19）
- [x] `shim/build.sh` --venus 在 Linux 编出 out/shim-boot-venus.img（188-196KB， venus-probe 分支 9a16744）
- [x] QEMU 冒烟：`python3 tools/qemu-venus.py` → venus_probe_ok（fb.txt 含 VENUS PROBE + PSCI method + CNTFRQ），venus-probe 分支 @9a16744
- [x] `fastboot boot shim-boot-venus.img` 多次（12:35 / 12:40 / 12:47 / 13:04，活动槽 _b，从不写分区）；黑屏恢复长按 Power+VolDown 10s 有效
- [x] **屏幕出现探针文字 = [VERIFIED]**（13:04 v4 run：用户观察到蓝底白字 + reset in 40s 倒计时）。黑屏期排查记录：v1-v3 黑屏根因 = splash_region 活机值 0xE2A00000 ≠ 静态 dts 0xEA600000（v4 从 ABL DTB 运行时读取后修正）
- [x] 探针输出包含 CNTFRQ_EL0（真机经 fb 可见；QEMU=0x3B9ACA00/19.2MHz 表）与 PSCI 版本（真机 SYSTEM_OFF 生效 = PSCI 链通）

## Stage 2 — RAM 窗口侦查

- [ ] 探针 v2 输出 live FDT（内存 bank、reserved-memory 节点、AML/ABL 传的 cmdline、initrd 参数）
- [ ] 输出记录在 RESEARCH_LOG（venus-carveouts 节）
- [ ] 据此新建 `mkadt_venus.py`（或 `--soc venus`），生成 `venus.adt` 且 dump 审读通过
- [ ] 复用 `qemu-sweet.py --soc venus` 冒烟（可选）

## Stage 3 — GIC / timer / SMP

- [ ] GICD/GICR 地址、per-PE size 已探定（对比 sweet `shim/shim.c:19-22`）
- [ ] `virtual-timer-intid` 输出值（复用 patch 0017 机制）
- [ ] XNU（SWEET machine config）在 macOS runner 绿
- [ ] XNU 在 venus 屏幕持续输出 kprintf（显示在屏幕任一行）
- [ ] PSCI CPU_ON 4×A55 起码一核入 kernel（kprintf 报告核数）

## Stage 4 — UFS / payload 装载

- [ ] SM8350 UFSHCI MMIO 地址 + CAP 寄存器值已到（RESEARCH_LOG 有 hex 值）
- [ ] 读取 userdata 首块魔法 0x1370（UFS read 场景）
- [ ] payload 拆 vendor_boot / boot 二选一方案文档化
- [ ] 能以轮询驱动读取 userdata 首块（UFS read 链路打通）
- [ ] payload 与 ADT 装载采用 boot / vendor_boot 的方案二选一并文档化
- [ ] `rd=ufsroot0` 内核视图挂上（XNU 检知 root 设备；[VERIFIED] L2/L4 部分点亮）

## Stage 5 — 显示 / 输入 / USB 日志

- [ ] 帧缓冲能看到 1440×3200 输出（分辨率、blitter rate 限帧的 sweetfb_hz 类参数）
- [ ] DSC 脚本或 ABL splash 能用（或改用连续 splash 内存地址）
- [ ] ST FTS 触控事件产生 IOHIDEvent（至少 touch down 检知）
- [ ] USB CDC-ECM 包覆（sweet 方案），主机测 netcat UDP 5140 有数据

## Stage 6 — iOS 用户态

- [ ] `mkiosroot.sh` + rootfs 优化脚本 iP16.7.16 20H392 输入正常（磁盘 16G+）
- [ ] XNU boot args（含 `rd=ufsroot0`）与 mkpayload 打包过
- [ ] launchd 起来进 SpringBoard；屏幕见 iOS 主屏或"Hello"
- [ ] 上游 ai_context §14 交接记录更新

## 通用规则（每阶段共同）

- [ ] 本文件每完成一项即更新
- [ ] 对应 ADR/RESEARCH_LOG 补充
- [ ] 任何破坏性 fastboot 命令（flash/erase/format/解 BL）不执行——无论哪一阶段
- [ ] 实验失败后能 3 分钟内拔电/重启回 fastboot，并记录失败现象
