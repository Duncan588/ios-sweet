# Working on ios-sweet

## OFFICIAL-COMMAND 铁律（2026-10-19 用户指示：优先参考官方，不自造）

后续每一步构建/发射/日志/patch 都按官方 README 的命令来，只换 venus 特定值（硬件真值/文件名），不改命令语义：

```sh
# ADT（venus 用 tools/mkadt_venus.py，内部复用 mkadt.build，同官方代码路径）
python3 tools/mkadt_venus.py shim/out/venus-adt.bin

# payload（官方同款 17 参数 cmdline；venus: cpus=1，不带 sweetfb_noconsole/sweetfb_hz）
python3 tools/mkpayload.py <kernel> shim/out/venus-adt.bin shim/out/payload-venu0.bin \
    --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1'

# 打包（venus 需 v2 头 + 活机 dtb；官方 sweet 是 shim/build.sh ../shim/out/payload.bin）
shim/build.sh --venus 2 out/payload-venu0.bin /home/monster/vboot_dtb_04.dtb

# 发射（官方 fastboot boot 同义）
tools/venus_boot_probe.sh shim/out/shim-boot-venus.img

# 内核构建（官方流程）
gh workflow run xnu.yml -R Duncan588/ios-sweet
gh run download RUN_ID -R Duncan588/ios-sweet -n xnu-kernel -D xnu/out

# kernel log（S4 UFS 可写后立即切换官方方案，停用拍屏）
fastboot boot recovery/recovery.img; tools/sweet-log-pull.sh logs/boot.txt; adb reboot bootloader
```

- 内核 patch：官方 patch 一字不动；venus 新增量编号续 0082 之后，官方风格。
- iOS root：`tools/mkiosroot.sh` + `sweet-root-tweaks.py`，与官方完全同款（S6 用）。
- d22ap.adt 提取：`tools/remotezip.py` + `ipsw img4 im4p extract`（官方命令，已执行过）。

---

- Read `NOTES.md` before starting; it contains the project goal, hardware facts,
  current blocker, local artifact locations and the next concrete action.
- The user switches agents frequently. Update the active handoff in `NOTES.md`
  after each milestone and before stopping. Record commits, CI run IDs, exact
  build/boot results and unfinished work. Distinguish observations from guesses.
- Commit completed changes. Use `gh` for GitHub Actions kernel builds and logs.
  Local single-file cross-compiles are useful before spending a CI run.
- Device bring-up uses temporary `fastboot boot` images. Preserve the Android
  installation and partition backups. The user approved (2026-10-15) using
  internal UFS for iOS and said Android may be deleted: the iOS root goes
  into the existing `userdata` partition (no GPT changes). Keep bootloaders,
  recovery, modem, persist and the backups intact, and confirm each
  destructive step with the user before running it.
- Keep device dumps, serial numbers and downloaded Apple artifacts out of git.
