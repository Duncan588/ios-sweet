# 方案报告:venus(SM8350,小米11)6 个副核 redistributor "GICR miss" 攻坚方案(2026-10-20,venus-probe 子代理)

> 本报告为**纯研究/方案**,未改任何代码。所有结论区分【观测】(真机/源码/主线证据)与【假设】(待 boot 验证)。
> 产物:本文档。下一步动作全部在 §6/§7,由主会话或下一子代理执行。

---

## 1. "GICR miss" 的准确定义(源码级)

打点在 `ios-sweet/shim/shim.c:442`(`venus_gicr_prepare_cpus`,VENUS_PROBE 才编译):

```c
static uintptr_t venus_find_gicr_aff(uint32_t aff)   /* shim.c:422 */
{
    for (uintptr_t off = 0; off < GICR_SIZE; off += GICR_PE_SIZE) {   /* 0x100000 内步进 0x20000,共 8 帧 */
        uint64_t typer = rd64(GICR_BASE + off + GICR_TYPER);
        if ((uint32_t)(typer >> 32) == aff)      /* GICR_TYPER[63:32] == MPIDR 亲和值 */
            return GICR_BASE + off;
        if (typer & (1 << 4))                    /* Last 位 → 提前终止 */
            break;
    }
    return 0;
}
```

- **miss = 在 8 个帧起步 offset(0x17A60000 + n×0x20000, n=0..7)上逐帧读 `GICR_TYPER`,其高 32 位亲和值(Affinity Value = Aff0[39:32]/Aff1[47:40]/Aff2[55:48]/Aff3[63:56])没有一帧等于目标 MPIDR**(shim 表:`0x0,0x1,0x2,0x3,0x101,0x102,0x103,0x200`)。
- miss 后**直接 continue:完全不执行 WAKER 唤醒、IGROUPR0、ISENABLER0 配置**(shim.c:441-444)。
- **不是睡眠"打不开",也不是 abort**:boot#17 照片中 `GICR miss ×6` 全部打印后 `GICRP prep=8` 照常出现、payload 正常进入内核 ⇒ 8 帧的 TYPER **读都不会触发异常**,只是那 6 帧**读回的值不匹配**。曾担心的"睡眠唤醒打不开/读 abort"已被 boot#17 排除。
- 观测拼图(boot#15/17 真值):`GICR+` 仅 aff=0x0、0x200 成功(且 en 回读 = bits 1+27+30,说明 0x200 那帧不但 TYPER 可读、寄存器写/读都真)。

### 关键推理(演绎,现状观测完全自洽)

副核在 shim 阶段全部处于 PSCI-OFF(整机 fastboot boot 后仅 boot 核在线)。假设【**H1:PE 处于 power-off 时,其 redistributor 帧寄存器读取返回零(RAZ 级无效值)**】:

| 期望 aff | 遍历期间实际读到的 TYPER 亲和 | 结果 |
|---|---|---|
| 0x0(boot,silver0,在线) | 0x0(真实值) | **hit** ✓ |
| 0x1/0x2/0x3(silver1-3,离线) | ≈0(RAZ)→ 与 0x1/0x2/0x3 都不等 | miss ×3 ✓ |
| 0x101/0x102/0x103(gold1-3,离线) | ≈0(RAZ) | miss ×3 ✓ |
| 0x200(prime,离线) | 0x200(有效!见下) | **hit** ✓ |

- H1 与全部观测一致;prime 帧**例外可读**(0x200 命中且 en 回读非零)⇒ prime(X1)的 RD 帧电源域与 silver/gold 不同(可能是独立 always-on/RDP 分组,或 X1 域未被当次全程 gate)。
- `Last` 位异常提前 break 的变体(H1b:睡眠帧返回 0xFFFFFFFF)被排除——那会使包括 0x200 在内其后所有帧 miss,而 0x200 命中了。
- 另一个独立候选【**H2:affinity 表错位**】(如 gold 实为 0x100-0x102 而非 0x101-0x103)**不能单独成立**:silver1-3 的 MPIDR=0x1..0x3 已被 CPU_ON 成功且副核回报 self-MPIDR 证实(boot#15),若帧有真实 affinity,walk 应命中。H2 只能作为**叠加因素**存在(见 §5 残留疑点)。

---

## 2. 布局权威验证(工具逐源核实,非记忆)

| 来源 | GICD | GICR | 结论 |
|---|---|---|---|
| **实机 ABL DTB** `E:\Hackios\Xiaomi11\out\vboot_dtb_04.dts:1388-1397` | `<0x17A00000 0x10000>` | `<0x17A60000 0x100000>`,`#redistributor-regions=<0x1>`,`redistributor-stride=<0x0 0x20000>` | 单 region、8 帧、stride 0x20000 ✓ |
| **Linux mainline** `arch/arm64/boot/dts/qcom/sm8350.dtsi`(intc 节点) | `<0x0 0x17a00000 0x0 0x10000>` | `<0x0 0x17a60000 0x0 0x100000>` 注释 `/* GICR * 8 */`,同 regions=1/stride=0x20000 | 与 ABL/DT 完全一致 |
| **AOSP common** sm8350.dtsi | 同上 | 同上 | 一致 |
| **mainline sm8450.dtsi**(对照) | `0x17100000/0x10000` | `0x17180000/0x200000`,**stride 0x40000**,内含 ITS 子节点(`gic_its@17140000`) | SM8450 是另一代(256KB/PE,GIC-700 族);venus(128KB/PE)与之**无可比布局问题** |

**结论【观测】:**
1. **SM8350 不存在 GICR "attached/分离/不连续" 布局的公开事实**——mainline 与 ABL DTB 两源一致:单 redistributor region、8 帧、0x20000/帧。shim 的遍历地址、跨度、stride 全部正确(boot#17 red-screen 判读也早已核过 vboot_dtb_04.dts:1388)。
2. miss 不是"找错了地址",而是"帧里的 TYPER 不吐期望的亲和值"。
3. GICD/GICR 常量(TIMER 27/30、19.2MHz、MPIDR 表)与 soc_venus.h 一致,无需改常量。

脚注:mainline sm8350.dtsi 的 **cpu 节点 reg = 0x0/0x100/…/0x700(单 cluster cpu-map)是主线占位伪值**,不是真 MPIDR(`RESEARCH_LOG` 第十次已判);真 MPIDR 以 shim CPU_ON 实测为准。

---

## 3. 与姊妹 fork(topaz/sweet shim)GIC 段 diff

逐行对照 `ios-topaz/shim/shim.c`(同 `gold_gicr_prepare`,commit 66ae4bf):

| 项 | topaz(sweet,SM7150 真值) | venus(sweet/SM8350) | 影响 |
|---|---|---|---|
| GICR_BASE | `0x0f300000` | `0x17a60000` | 无 |
| INTID | vt=19/pt=18 | vt=27/pt=30(soc_venus.h) | 无(boot#4 已证 27 让 SWEET GIC 行通过) |
| MPIDR 表 | `cpu_mpidr(i) = (i>>2)<<8|(i&3)`(0..3, 0x100..0x103) | 显式数组 `{0,1,2,3,0x101,0x102,0x103,0x200}` | gold 段编号来源不同,**是 H2 残留疑点** |
| `GICR_IPRIORITYR` 写 | 保留(**word 内字 0xA0A0A0A0** + wr8 循环)且**成功无 abort** | shim 侧 skip(2026-10-19 wr8 外部 abort,ESR 0x96000010) | 见 §4 |
| cross-PE walker | 同款,findByAffinity | 同款 | topaz 上副核帧 cross-PE TYPER 可读;venus 上 6 帧不可读 ⇒ **板卡集成差异在 venus 引入**,非代码回归 |

---

## 4. IPRIORITYR/GICR_SGI+0x400 外部 abort —— 与 miss 的关系【判定:不直接相关,但可能同根】

【观测】
- abort 现场(2026-10-19):`wr8(r+GICR_SGI+0x400+id, 0xa0)`(**8-bit 字节访问**)→ ESR 0x96000010 同步外部 abort;同页/同帧的 IGROUPR0(+0x080) **word** 写成功、ISENABLER0(+0x100)/IPRIORITYR+16(+0x410) **word** 读成功。
- **内核侧(bt87/0087 同源代码)`pe_fiq.c:176` 写 priority 用 `gicr_write32(GICR_IPRIORITYR0 + i, 0xA0A0A0A0)`(步进 i+=4 的 word 写)**,在 boot#4/boot#17 的 cpus=1 内核上顺利通过(GIC init → IOKit 一路跑到 Firehose)。内核全路径无任何 byte GICR 访问。
- topaz shim 同一访问宽度的 wr8 系列成功(sweet/SM7150 板可 byte)。

【判定】aborted 的是**访问宽度**而非"0x400 页本身":强假设【**B1:SM8350 的 GIC 集成不允许对 GICR 寄存器的 sub-word(byte)访问,而不支持宽度返回 SLVERR → 同步外部 abort**】(Arm GIC-500 TRM 明示"不支持的访问宽度返回 SLVERR";GICv3 对部分寄存器的字节访问为 IMPLEMENTATION DEFINED)。与 miss 不构成因果(miss 全程只做 rd64/rd32;abort 出现在清醒 boot RD 的字节写),但**同属"venus GIC 集成纪律":一律 32-bit word 访问**,对后面 cpus>1 的任何 kernel/shim 补丁都是硬约束。

---

## 5. 内核侧期待(bt87 = 0087 前置 buildtree,~/research/bt87)

【观测,ssh 实读】
- `pexpert/arm/pe_fiq.c::pe_init_fiq()`:**每 CPU 自服务** —— `find_gicr_pe_base()`(同款 TYPER 亲和扫描,由该核**在自己清醒时**执行)→ `GICR_WAKER` 只有 `ChildrenAsleep` 置位才清 `ProcessorSleep` 并等其回落 → `gicr_write32(GICR_IGROUPR0, 0xFFFFFFFF)` → **word 写 priority** → `ISENABLER0 = (1<<sweet_vtimer_intid)|(1<<SWEET_IPI_SGI)` → ICC_SRE/BPR/PMR/EOImode/IGRPEN1,GICD_CTLR ARE_NS|Grp1A|Grp1;`SWEET.h` 里 `GICR_PE_SIZE = 0x20000`、`GICR_TYPER_AFFINITY_VALUE_SHIFT=32`,与 DT/DTB/GIC-600 完全一致。
- 调用链:`osfmk/arm64/start.s`(SWEET secondary 入口 ERET 到 EL1)→ `LowResetVectorBase` → `start_cpu` → `arm_init_cpu()`(`arm/arm_init.c:676`,注释"SMP boot (non-boot CPUs only)")→ `PE_init_cpu()` → `pe_init_fiq()`。即 **xnu 在每次二审副核上线时,都会由该核自己配置自己的 redistributor**。
- ⇒ **当前 0087 内核对"副核 RD 未被 shim 预唤醒"有内生补偿**:副核一进内核即自 walk(此时该 PE 在线,TYPER 必可读)+ 自 wake + 自 enable。shim 的 cross-PE prepare(GICR+ / miss)在内核 per-core 自服务覆盖下,对 cpus>1 的功能影响被压缩到**副核 pe_init_fiq 执行前的窗口**(该窗口内核尚未开中断、不会把自己挂起,风险≈可忽略)。

【判定】**miss ×6 是 shim 侧 cross-PE 预配的天花板,不是内核路径的硬墙**。cpus>1 现在最可能的下一堵墙不是它。残留小风险:
- R1:若 xnu/SweetCPU 在副核 pe_init_fiq 完成前向其路由 SPI(1-of-N)——sweet 早期 boot 无外部 SPI 依赖(boot#17 cpus=1 已穿 zone/VM/IOMD/RTM/Firehose),实测覆盖。
- R2:pre-park 二次唤醒场景(shim 的 test_smp → PSCI CPU_OFF 后寄存器是否保留)—— topaz 证据(66ae4bf landed 同款预唤醒)成立;venus 未测。

---

## 6. 根因假设排序 + 验证探针设计(不改代码,下一 boot 执行)

### 假设排序(P0 首选)

1. **H1(RAZ)**:silver1-3/gold1-3 六帧在 PE power-off 时 TYPER 读为 0(RAZ 级);prime 帧例外可读(独立电源域)。—— 与全部观测一致,无反例。
2. **H2(表错位,叠加)**:gold 真 affinity ≠ 0x101-0x103(如 0x100-0x102,即 Aff0 从 0 起计,SM7150/SDM845 惯例;0x103 需要 4 个 gold 核,SM8350 只有 3 个)。**单独**解释不了 silver miss,但若与 H1 同时成立则影响 golden 编号/CPU_ON 细节。
3. **H1b(垃圾值/Last 位)**:已被 0x200 命中位置排除(§1)。
4. **H3(第二 RD region/attached)**:被 mainline+ABL DTB 双源否定(§2)。

### 探针(venus shim 下一增量,如 `venus-probe` 分支 00x,单 boot 完成,全部只读 + word 访问,符合 HOLD 不毁屏铁律)

**探针 A(决定性,8 帧全 dump)** —— 在 `venus_gicr_prepare_cpus` 里,对每帧 n=0..7(`base = 0x17A60000 + n*0x20000`)打印一行:

| 读点 | 地址 | 期望/判读 |
|---|---|---|
| `GICR_TYPER`(64b)| `base+0x08` | 高32位 = 亲和、bit4=Last、bits[23:8]=ProcessorNumber;**全 0 ⇒ H1 RAZ 实锤**;有值但不等期望 ⇒ H2;0xFFFFFFFF ⇒ H1b 复活 |
| `GICR_IIDR`(32b)| `base+0x04` | implementer/product ID(区分 GIC-600 vs 700) |
| `GICR_WAKER`(32b)| `base+0x14` | 每帧当前睡眠态(reset 0x6=PS+CA;RAZ 帧=0) |
| `GICR_PIDR2`(32b)| `base+0xFFE8` | PIDR2.archrev=0x3GICv3;若帧 RAZ 应=0 |

输出格式紧凑:`GICR7T aff=xxxxxx last=%d pn=%d waker=%x iidr=%x` ×8 行,由现有 console 打到 boot 屏,拍照取证(照片通道唯一,venus 铁律)。

**探针 B(GICD 身份)**:`rd32(0x17A00000+0x08)`(GICD_IIDR)、`rd32(0x17A00000+0xFFE8)`(GICD_PIDR2 archrev)—— 把"是不是 GIC-600"从"?"坐实,还能对照 0x200 帧(命中帧)的差异。

**探针 C(可选、独立 boot#19):priority word 写回归**——在 boot CPU 自己的帧(`r0 = find_gicr()`)上执行 `wr32(r0+0x400, 0xA0A0A0A0); rd32(r0+0x400)` 验证 word 写+回读成功(预期成功,呼应内核路径)。byte 写测试(预期 SLVERR)单独一轮、boot arg 门控、允许蓝屏(HOLD),以最终钉死 B1。

**探针 D(随探针 A 免费获得)**:副核在线自配证据 —— 把 `test_smp()`(shim.c:218,已编译未调用)临时挂回 probe 屏:CPU_ON {0x1..0x3,0x101..0x103,0x200} → 每副核 `secondary_main` 里 `find_gicr()`(自帧 walk)并回报 `sec_probe[0..3]`。目的:证明"PE 在线时自帧 TYPER 必可读"(内核 per-core 补偿成立的前提)。若某副核在线时连自己的 TYPER 都读不出真实亲和值——则 H1 需要更激进修正(如 RD 需 RSC/PMC 先供电,超出 mmio 范围),这将是重大改判。

### 判读表(boot#18 拍屏后)

| 观测 | 判定 | 下一步 |
|---|---|---|
| 6 帧 TYPER 全 0(waker=0/pidr=0) | **H1 成立**,prime 帧例外可读同理坐实 | → §7 修法 2(venus-by-design),miss 降级为诊断噪音;cpus=2 直接试 |
| 6 帧有真实亲和值,且 gold=0x100/0x101/0x102 | **H2 成立**(表错位) | → §7 修法 1(改 venus_mpidrs 一行 + wake 遍历按 ProcessorNumber),CPU_ON 表同步改(0x103 CPU_ON 应失败——回查 boot#15 照片的 mpidr= 行即可旁证) |
| 0x200 帧 TYPER 完整、其余 RAZ | H1 + prime 电源域特殊性 | 同 H1 处置;prime 帧预处理保留 |
| 副核自帧 TYPER 也 RAZ(探针 D) | H1 改判:RD 供电不在 Mmio 域,需 PSCI/RSC 通道 | 中止方案 1/2,转 RSC/qcom-fw 通道研究(新立项) |

---

## 7. 修复方案 diff 草图(按判读结果二选一,不变更内核语义)

### 修法 1(若 H2:affinity 表错位;最小 2 处,shim 侧)

`ios-sweet/shim/soc_venus.h`:
```c
- static const uint32_t venus_mpidrs[VENUS_NR_CPUS] = {
-     0x000, 0x001, 0x002, 0x003, 0x101, 0x102, 0x103, 0x200
+ static const uint32_t venus_mpidrs[VENUS_NR_CPUS] = {   /* 以探针 A 实测 TYPER 为准 */
+     0x000, 0x001, 0x002, 0x003, 0x100, 0x101, 0x102, 0x200
};
```
`shim.c::venus_find_gicr_aff`:匹配条件追加 `ProcessorNumber`(TYPER bits[23:8],ATF `gicv3_rdistif_base_addrs_probe` 的判据)作 secondary key,哪怕 affinity 字段被固件部分 flush 也能对帧定址:
```c
+ uint32_t pn = (uint32_t)((typer >> 8) & 0xffff);
  if ((uint32_t)(typer >> 32) == aff || pn == aff_from_pn_table) return GICR_BASE + off;
```
CPU_ON 表(`shim.c:228 mpidr_of`)同改。CI 流程照官方 README(xnu-cc.py + `git apply --check` 双绿)。

### 修法 2(若 H1:RAZ;推荐默认,venus-by-design)

不动内核、不动 shim 语义,只把 prepare 改为"**只 prep 可读帧,其余交给 xnu per-core 自服务**":
- `shim.c::venus_gicr_prepare_cpus` 保持 miss→continue 现状(它已经是"能做多少做多少",且 0x0/0x200 已覆盖 boot CPU+可读 prime 帧);
- 加探针 A 诊断输出(可留在后续 commit,不带语义);
- **进行 0087 内核 cpus=2 试射**:`mkpayload.py` 官方 17 参数 cmdline,`cpus=2`(不改其他)→ `build.sh --venus 2 out/payload-venu0.bin /home/monster/vboot_dtb_04.dtb` → `tools/venus_boot_probe.sh`;
- 观察点:`SWEET CPU: requested=2` → `startAll`(0084 打点)→ `start() cpu: 1` → `cpu=1 entered initCPU` → `processor_start returned`(0086/0087 打点)→ SweetCPU beats。副核鲁棒性由 arm_init_cpu→pe_init_fiq 兜底(§5)。
- 若 cpus=2 在**非 GICR 环节**又 wedge:按 boot#17 既有方法论(QEMU 复现 zone/VM/IOMD/RTM/Firehose 后第一墙 + P0086/P0087 风格插桩),**不与 GICR miss 混合归因**。

### 修法 3(fallback,若 cpus=2 出现"极早期 timer-on-secondary 不到期"(topaz 66ae4bf 曾命中的同一类))

shim 侧 per-核自配绑定:让 shim 在 loader 之前对 7 个副核做"**CPU_ON(副核) → 副核自配自身 RD → PSCI CPU_OFF park**"(`secondary_main` 现成逻辑,扩展为全 7 核 + 计数器)。这等效于把 cross-PE wake 改为"借道在线副核自己的通道",不依赖 asleep 帧可读性:
```c
/* sketch: venus_pre_park_cpus() —— test_smp() 同款循环,附加自配 */
for (i = 1; i < NR_CPUS; ++i)
    smc(PSCI_CPU_ON, venus_mpidrs[i], (uintptr_t)secondary_entry, i);
delay_ms(500);          /* 各核 secondary_main: find_gicr(self) → wake 自帧 + IGROUPR0/ISENABLER → PSCI CPU_OFF */
```
仅在探针 A/D 全部证据齐后才启用(0x103 的 CPU_ON 是否有效要先定,H2 残留)。

---

## 8. 引用

**开源/官方(全部本轮工具实取)**
- Linux mainline sm8350.dtsi GIC 节点:https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sm8350.dtsi （intc ~L3654: `#redistributor-regions=<1>`, `stride 0x20000`, `0x17a60000 0x100000 /* GICR * 8 */`)
- AOSP common 同文件:https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/arch/arm64/boot/dts/qcom/sm8350.dtsi
- Konrad Dybcio 2021 "sm8350: Add redistributor stride to GICv3" 补丁(布局历史):https://lore-kernel.gnuweeb.org/lkml/20211114012755.112226-3-konrad.dybcio@somainline.org/
- Linux mainline sm8450.dtsi(对照,17100000/17180000/stride 0x40000 + ITS):https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sm8450.dtsi
- Arm GIC-600 TRM r1p6 redistributor 寄存器表(TYPER/WAKER/PIDR 偏移与 reset 值):https://developer.arm.com/documentation/100336/0106/programmer-s-model/redistributor-registers-for-control-and-physical-lpis-summary
- Arm "Learn the Architecture — Configuring the Arm GIC"(wake 协议:清 ProcessorSleep→poll ChildrenAsleep;GIC-600 ARE 恒 1):https://developer.arm.com/documentation/198123/0302/Configuring-the-Arm-GIC
- ATF `gicv3_main.c` / `gicv3_helpers.c`(TYPER aff/ProcessorNumber 帧探查的权威实现):https://github.com/ARM-software/arm-trusted-firmware/blob/master/drivers/arm/gic/v3/gicv3_main.c
- Arm GIC-500 TRM(不支持宽度 → SLVERR,对应 venus byte-write abort 机制):https://support.arm.com/documentation/ddi0516/e/programmers-model/about-the-gic-500-programmers-model
- Linux irq-gic-v3.c `gic_enable_redist`(per-CPU 自 wake 形态参考,MSM8996 quirk 注 residues):https://github.com/Xtended-Devices/kernel_oneplus_sm8350/blob/xt/drivers/irqchip/irq-gic-v3.c

**本机观察(实机/实码)**
- ABL DTB:`E:\Hackios\Xiaomi11\out\vboot_dtb_04.dts`(L1388-1397,与 ABL/主线双一致)
- shim:`E:\Hackios\ios-sweet\shim\shim.c`(miss 打点 L442,walker L422,skip 注释 L376/464)、`soc_venus.h`、`venus_probe.c`
- topaz:`E:\Hackios\ios-topaz\shim\shim.c`(gold_gicr_prepare L186-220, wr8 L211 在 sweet 板成功)
- 内核(bt87,0087 前置):`pe_fiq.c`(word priority 写 L176,wake 流程)、`arm_init.c`(arm_init_cpu→PE_init_cpu)、`start.s`(sweet secondary EL1 入口)、`SWEET.h`(GICR_PE_SIZE=0x20000)
- 历史:`docs/RESEARCH_LOG.md` 第十~十六次(ADT/CPU/CNTP/IMEM 纪录)与 NOTES.md boot#0..#17(证据链条 h)
