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

- [ ] 已用 `tools/unpack_bootv2.py`（扩展 v3 语义）解出 stock venus boot，记录 header 字段：header_version、kernel/ramdisk/second size、os_version、header extra 字段
- [ ] `shim/build.sh` 修改后本地能编（Linux）或 Windows 版本记录；产物 `out/shim-boot.img`
- [ ] QEMU 冒烟（可选但推荐）：探针在 `-M virt` 输出预期字符串
- [ ] `fastboot boot out/shim-boot.img` 一次，并记录：屏幕现象 / 无响应时的恢复（拔电 5s）
- [ ] 屏幕出现探针文字 = [VERIFIED]；未出现时按以下顺序排查原因：boot 校验失败 / v3 头字段不符 / 帧缓冲地址或分辨率 / RAM 窗口
- [ ] 探针输出包含 `CNTFRQ_EL0` 值与 PSCI 版本号

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
