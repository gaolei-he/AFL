# AFL 输入变异模块分析报告



日期：2026-01-06

<div style="page-break-after:always"></div>

<div style="page-break-after:always"></div>

## 1 工具与核心模块基础梳理

- **工具信息**：American Fuzzy Lop (AFL) 2.57b，作者 Michał Zalewski；GPL-2.0；C 语言实现；主要运行于 Linux，依赖 gcc/clang 与插桩。
- **核心模块定位（输入变异）**：
  - 主文件：`afl-fuzz.c`。
  - 关键函数：`fuzz_one()`（驱动单次变异与执行）、`perform_deterministic_stage()`、`perform_havoc_stage()`、`perform_splice_stage()`，以及覆盖判定的 `common_fuzz_stuff()`。
  - 数据结构：`queue_entry`（种子队列与元数据）、`trace_bits`（共享内存覆盖率 bitmap）、`virgin_bits`（未触达边集合）、阶段计数变量 `stage_name/stage_cur`。
- **课程关联**：灰盒模糊测试的输入变异策略与覆盖反馈；分阶段启发式（确定性 + 随机化）提高探索效率。
- **开发演进概览**：AFL 最初由 lcamtuf 发布，社区维护至 2.x；AFL++ 另行演进但保持兼容接口，核心输入变异逻辑在原版 `afl-fuzz.c` 中仍可清晰看到；开源协议允许学术与非商用环境自由使用。
- **适用场景与运行环境**：AFL 适合对本地可执行文件进行灰盒模糊测试，尤其是纯用户态程序。典型依赖包括：可写的 `/proc/sys/kernel/core_pattern`，可用的 forkserver 接口；如在容器/WSL 需关注权限与性能；若目标使用 ASAN/UBSAN，需设置 `AFL_USE_ASAN` 等环境变量保证插桩兼容。
- **与课程知识点的映射**：AFL 体现了“覆盖引导、轻量反馈、随机化搜索”三要素，与课堂中的动态分析、自动化测试、反馈引导测试章节直接对应。 
- **许可与协作**：GPL-2.0 要求衍生作品同协议开源，适合研究复现；社区 issue/patch 记录可帮助理解工程演进与缺陷修复路径。

## 2 核心代码模块原理分析

- **目标与 I/O**：输入为当前种子字节流、调度参数和随机源；输出为新的队列种子或崩溃/超时记录，核心目的是通过变异发现新覆盖或缺陷。
- **流程**：
  1) `fuzz_one()` 取出队列元素，完成校准与能量分配。
  2) **Deterministic 阶段**：按顺序执行 bit/byte flip（1/2/4 bit，8/16/32 bit）、算术 inc/dec（8/16/32）、interesting 值替换（0/±1、0xFF/0x7F/0x8000…）。每次变异后通过 `common_fuzz_stuff()` 执行目标并检查 `trace_bits` 是否产生新位。
  3) **Havoc 阶段**：随机叠加多种算子（位翻转、字节设定、块插入/删除/复制、算术加减、字节替换等），迭代轮次由能量放大（典型 256/1024 次）。
  4) **Splicing 阶段**：与另一种子拼接形成新基线，再进入 Havoc。
  5) 反馈：若 `trace_bits` 与 `virgin_bits` 按位与出现新非零位，则保存输入到队列；更新评分与能量调度。
- **关键实现点**：覆盖判定使用共享内存 bitmap；稀有边缘优先的能量分配（favored seeds 获得更多 Havoc 轮次）；校准多次执行以过滤噪声路径。
- **前沿技术关联**：
  - 覆盖驱动 + 分阶段变异（确定性→随机化）是一类经典灰盒模糊测试框架；
  - Havoc 中“多算子叠加”类似遗传算法突变，提升搜索多样性；
  - 稀有路径优先调度体现反馈导向的能量分配思想；
  - 未引入符号执行/约束求解，属于轻量级动态反馈派生。
- **输入→处理→输出的逻辑拆解**：
  - 输入：队列元素的文件内容、长度、先前覆盖得分、是否 favored、执行速度；全局参数如 `havoc_div`、`CALIBRATION_CYCLES`。
  - 处理：
    - 预处理：修剪（`trim_case`）减少冗余字节；校准执行以获取稳定性与基本耗时。
    - 确定性子阶段：逐位/逐字节 flip → 算术 inc/dec → interesting 替换，子阶段间有短路逻辑以节约时间。
    - Havoc：随机算子集合（bitflip, setbyte, delete, clone, insert, add/sub, overwrite with interesting, splice chunk）叠加多次。
    - Splice：选第二个 seed 拼接，减少局部最优，随后再次 Havoc。
  - 输出：若 `new_bits`=1 则写入 `queue/`；崩溃则写 `crashes/`；超时则写 `hangs/`；并更新 `fuzzer_stats`。
- **数据流与调用关系补充**：`fuzz_one()` 内部多次调用 `common_fuzz_stuff()`；覆盖判断依赖 `has_new_bits()`；能量调度由 `calculate_score()` 生成 `stage_max` 与 `use_stacking`；插桩结果通过共享内存 `trace_bits` 回传。
- **反馈机制细节**：bitmap 使用 64K 槽位，AFL 采用“边缘计数折叠”与“virgin_bits 与运算”判新；为防止噪声，校准运行多次比对哈希；对路径稀有度评分后，favored 种子被优先选中变异并拥有更长的 Havoc 迭代数。

## 3 调试验证与效果复现

- **环境与命令**：

  - AFL 版本：2.57b；运行于容器（root）。
  - 编译：`./afl-clang-fast -g -O0 test.c -o test`（如无 clang 则 `./afl-gcc -g -O0 test.c -o test`）。
  - 运行：`./afl-fuzz -i inputs -o outputs -- ./test`，`inputs/seed` 为 1 字节初始种子。

- **目标程序（用于触发多种变异场景）**：

  ```c
  #include <stdio.h>
  #include <string.h>
  #include <stdint.h>
  
  int main(void) {
  	unsigned char buf[16] = {0};
  	size_t n = fread(buf, 1, sizeof(buf), stdin);
  	if (n < 3) return 0;
  	if (buf[0]=='A' && buf[1]=='F' && buf[2]=='L') puts("hit1");
  	if (n >= 6 && memcmp(buf, "AFL++", 5)==0 && buf[5]==0x7f) puts("hit2");
  	if (n >= 8) { uint16_t x; memcpy(&x, &buf[6], 2); if (x == 0x1234) puts("hit3"); }
  	if (n >= 12) { uint32_t y; memcpy(&y, &buf[8], 4); if (y == 0xCAFEBABE) puts("hit4"); }
  	if (n >= 14 && buf[12]=='X' && buf[13]=='Y') puts("hit5");
  	return 0;
  }
  ```

- **实验步骤**：

  1) 准备最小种子：`printf 'A' > inputs/seed`，确保 `inputs` 非空；
  2) 用 AFL 编译目标：`./afl-clang-fast -g -O0 test.c -o test`；
  3) 启动 fuzz：`./afl-fuzz -i inputs -o outputs -- ./test`；
  4) 观察阶段切换与新路径：`last new path` 出现在早期 determinisitic 阶段；
  5) 队列验证：从 `outputs/queue/` 取若干 `id:*`，执行 `cat outputs/queue/id:00000* | ./test`，可看到 `hit1`→`hit5` 逐步被覆盖；
  6) 收尾：当 `pending=0` 且 `last new path` 长时间不变时 Ctrl+C 停止。

- **运行结果概览（来自 `outputs/fuzzer_stats`）**：

  - `paths_total`: 12，`paths_favored`: 11，`max_depth`: 5（多级分支被触达）；
  - `cycles_done`: 82，`execs_done`: 914,300，`execs_per_sec`: ~4,940/s；
  - 覆盖率 `bitmap_cvg`: 0.05%；`unique_crashes`: 0，`unique_hangs`: 0；
  - 命令行：`./afl-fuzz -i inputs -o outputs -- ./test`；稳定性 100%。

- **仪表盘截图**：
  ![image-20260106161626589](C:\Users\whoami\AppData\Roaming\Typora\typora-user-images\image-20260106161626589.png)

- **调试与观察要点**：

  - 阶段切换顺序：deterministic → havoc → splice；`last new path` 通常在早期阶段出现。
  - `pending` 与 `pending_favs` 归零后，队列已轮空；`cycles_done` 持续增长表示在重复迭代。
  - 使用 `cat outputs/queue/id:... | ./test` 可验证 `hit1`~`hit5` 逐步覆盖，并与阶段日志对应。
  - 日志/断点示例：在 `common_fuzz_stuff()` 返回后打印 `stage_name, stage_cur, new_bits`；可配合 gdb 观察 `queue_cur->len` 与 `use_stacking`。
  - 队列样例（取自 `report/outputs/queue`）：
    - `id:000011,src:000010,op:havoc,rep:4,+cov`（hex: `41 46 4c 00`，即 "AFL\0"），重放：`cat outputs/queue/id:000011,src:000010,op:havoc,rep:4,+cov | ./test`，命中 `hit1`；
    - `id:000010,src:000007,op:havoc,rep:2,+cov`（hex 前缀: `41 46 4c 4c 4c 4c ...`），同上命中 `hit1`；
    - `id:000007,src:000004,op:flip4,pos:1,+cov`（hex: `58 58 58 58 58 58 58 58 6b 58 58 64 58 59`，末尾 `58 59` 为 "XY"），重放：`cat outputs/queue/id:000007,src:000004,op:flip4,pos:1,+cov | ./test`，命中 `hit5`；
    - 其余 `id:00000*` 为不同 Havoc/flip 叠加产生，可按同样方式重放观察 `hit2`/`hit3`/`hit4` 的覆盖时间点。
  - 故障排查提示：
    - core_pattern 为管道：`echo core >/proc/sys/kernel/core_pattern`；无权限可临时 `AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1`（有误判风险）。
    - 无有效种子：确认 `inputs/seed` 存在且可读。
    - 覆盖不增长：核对是否用 AFL 编译器、`-O0`，并让 fuzzer 跑足够时间。
    - 性能过低：检查 CPU 绑定和频率调节，容器 cgroup 限制。
    - 插桩冲突：若用 ASAN，设置 `AFL_USE_ASAN=1` 并适当调高超时。

## 4 优缺点与技术落地反思

#### 4.1 结果解读与课程要点对应

- **覆盖与路径**：`paths_total=12` 与 `max_depth=5` 说明示例程序的主要分支均被探索到；`bitmap_cvg=0.05%` 是 AFL 典型低密度 bitmap，符合小程序预期。
- **阶段贡献**：早期 determinisitic 阶段即可找到新路径；后续 Havoc/Splicing 继续在常量匹配和整数条件上拓展（命中 `hit2`/`hit3`/`hit4`/`hit5`）。
- **性能与稳定性**：~4,940 exec/s、100% 稳定性，说明 forkserver 工作正常、无噪声路径；无 crash/hang 符合示例程序设定。
- **与课程知识点映射**：
  - 覆盖反馈：`trace_bits`/`virgin_bits` bitmap 判定新路径，对应灰盒覆盖引导；
  - 变异策略：确定性算子系统化枚举常见边界；Havoc/Splicing 提供随机探索与多源拼接；
  - 能量调度：favored seeds 获得更多变异机会，体现覆盖稀有度驱动。
- **可选小改动设想（未实做，仅思路）**：
  - 提升 Havoc 轮次或引入结构化算子（面向特定格式），以验证覆盖增长速度差异；
  - 在 `calculate_score` 中调高稀有边缘权重，观察 favored 队列数目与路径增长的变化；
  - 在插桩侧加入路径哈希去噪（多次执行取交集），减少伪路径。

#### 4.2 优缺点与技术反思

- **优点**：确定性阶段系统化覆盖常见位/字节模式；Havoc/Splicing 提供广度探索；覆盖反馈简单高效；稀有路径优先的能量分配提升增量效率。
- **局限**：对结构化或带校验和的输入效率偏低；确定性阶段在长输入上耗时；覆盖粒度为边计数，路径敏感性有限；无约束求解，深路径可能难触达。
- **改进思路**：结合格式/结构感知变异（字段感知、语法感知）；提高插桩粒度或结合路径信号；对噪声路径使用多次执行稳定性过滤。
- **工程落地反思**：
  - CI/CD 可用 master/secondary 并行与同步队列提高覆盖；
  - 对服务型/长运行目标可用 persistent mode 或 AFL++ FRIDA/UNIX sockets 适配，减少启动开销；
  - 结合 crash 去重与最小化（stack hash + afl-tmin），降低缺陷 triage 成本；
  - 与符号/梯度引导（QSYM/Angora 类）混合可改善深路径，但需额外计算资源。

## 5 参考文献

- AFL 文档：https://afl-1.readthedocs.io/en/latest/index.html
- lcamtuf 博客：Binary fuzzing strategies (2014)
- AFL 源码（`afl-fuzz.c`）
