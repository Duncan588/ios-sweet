# venus `fastboot boot` 黑屏恢复速查（人工步骤）

适用：`tools/venus_boot_probe.sh` 跑完 `fastboot boot` 后设备黑屏、无 adb、无 fastboot 响应。
设备：Xiaomi Mi 11（venus / M2011K2C），BL 已解锁。临时启动不写分区——只要不乱用工具，
拔电重启后必回 Android（活动槽 _b）。

**铁律：绝不使用小米官方工具（MiFlash / 小米助手 / 深度测试任何刷机工具）。
恢复手段只有 fastboot 与物理断电。**

## 双端速查

### 笔记本端（有 adb/fastboot，设备插在笔记本上）
1. [自动] 脚本本身不重启设备；先看 `logs/venus-probe-<ts>.log` 的 START/END 时间戳。
2. [人工] 黑屏等待 **30 秒**（镜像自解压/early console 无输出属于可能现象）。
3. [人工] 长按 **音量下(−112) 无效** 时按引导组合：**Power + 音量下 10 秒** 强制重启。
4. [人工] 重启后立即按住 **音量下** 进 fastboot（或等它自己回到系统）。
5. [终端] `fastboot devices` 验证设备回来了；回系统则用 `adb devices` 验证。
6. [终端] 回到系统/fastboot 后，把现象（有无 logo、震动、背光）补记进
   `docs/RESEARCH_LOG.md` 的探针小节。

### Windows 端（E:\Hackios 侧，adb 看不到设备——仅作记录与库操作）
1. [终端] `ssh notebook` 登笔记本跑上述命令（adb/fastboot 都在笔记本）。
2. [终端] 日志在 repo `logs/venus-probe-<ts>.log`（已被 .gitignore 排除，不进 git）。
3. [人工] 若笔记本端 `fastboot devices` 也空 → 走下面的「完全无响应」。

## 完全无响应（fastboot 也看不到）
1. [人工] **拔 USB 线 5 秒再重插**（最干净的"断电"，决不拆机、决不接官方工具）。
2. [人工] 重插后 2~3 秒内按住 **音量下** 强制进 fastboot；或按 Power+音量下 10s 重启。
3. [终端] 再验 `fastboot devices`。venus 进 fastboot 后电池不充电，长时间不用时先退出。

## 记录义务
每次恢复动作（等了多久、按了什么、现象）随即补进 `docs/RESEARCH_LOG.md`，标 [人工]。
