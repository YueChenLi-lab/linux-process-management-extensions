# 实验方法与验证口径

本项目对应上海电力大学 2024 级数据科学与大数据技术操作系统课程的进程管理扩展。核心代码为 C，Python 只读取实验日志、汇总数据并画图。没有修改 Linux 0.11 或当前宿主内核。

## 1. 与课程指导书模型的对应

| 指导书 | 本实现 | 差异及理由 |
|---|---|---|
| PCB name/state/ntime/rtime/link | 同名字段，静态 PCB 注册表 | 注册表让检查器能发现队列外丢失的 PCB；最多 128 个，完成后保留记录 |
| ready / pfend | ready.head / ready.tail | ready 队首运行态 R，其余为 w；运行 PCB 不额外移到第三条队列 |
| stop / stepend | blocked.head / blocked.tail | 阻塞 PCB 均为 b，按 FIFO 唤醒 |
| stop()/wake() | CLI 的 block / wake | 避免与系统函数重名，空队列返回 NOOP |
| 每次运行一个时间单位 | step | 只推进队首，完成后标记 F 并移出活动队列 |
| 撤销 PCB | finish | 教学 CLI 直接把队首剩余工作置为完成，保留注册记录供不变量检查；与 step 自然结束区分 |

每次成功事件（含 NOOP）检查：所有未结束 PCB 恰好出现一次；F 不在活动队列且 link 为空；链表无环；头尾空性一致；尾结点是真正末节点且 link 为空；blocked 均为 b；ready 最多一个 R 且在队首。运行态留在 ready 中符合本教学模型。检查器不访问未知指针；发现破坏时写 FAIL，避免遍历损坏链表，stderr 报第一个违规步骤并非零退出。非法 CLI 输入被拒绝，不记为合法事件。

## 2. 三项实验的控制变量

| 实验 | 改变变量 | 保持一致 | 记录/验证 |
|---|---|---|---|
| PCB | create/step/block/wake/finish 序列 | 单线程、确定性、FIFO 队列 | 每事件 CSV；独立参考模型；主动破坏状态与链表 |
| 调度 | FCFS / 非抢占 SJF / RR | 同一 CSV、到达时刻、运行量、单 CPU、零调度开销 | 完整时间线、逐任务指标、理论值断言 |
| nice 无额外竞争 | 目标 nice=0 / 10 | 1 亿次运算、目标单进程、实际同一 CPU、同编译选项 | 各 10 轮，wall/CPU 时钟、实际 nice/CPU |
| nice 固定竞争 | 目标 nice=0 / 10 | 上述条件；额外竞争进程 nice=0 | 同步门、竞争生命周期、开始偏差、父进程回收 |

### 调度规则与指标

- 输入顺序是最终 tie-break：FCFS 按 `(arrival, input_order)`；SJF 在已到达任务中按 `(burst, arrival, input_order)` 选择。SJF 不抢占。
- RR 的到达任务按 `(arrival, input_order)` 入队。片段结束时，当前时刻新到达任务先入队，未完成的当前任务再放队尾。
- 初始和中间空闲都写为 `IDLE` 片段，模拟时钟直接跳至下次到达。上下文切换仅计**时间连续且 ID 不同的两个非空闲执行片段**，进入/离开 IDLE、首次派发、同任务跨时间片均不计。
- `turnaround = completion - arrival`；`waiting = turnaround - burst`；`response = first_start - arrival`。平均值为本次所有任务的算术平均，单位为模拟 tick。
- 任务仅含单段 CPU burst，没有 I/O；上下文切换计数不带耗时；SJF 假定事先知道运行量。这些假设限制了算法排名的适用范围。
- `A(0,5),B(0,2),C(0,3),q=2` 的完整时间线、均值、RR 逐任务完成/等待/响应均在 C 中断言。另测到达不同、空闲、相同 burst、q=1、q 大于最大 burst、单任务、非抢占以及片段边界新到达。
- 输入仅接受 ASCII 字母数字下划线 ID，禁用保留 IDLE，最多 128 任务；arrival、总 burst、quantum 不超过 1e9，burst 必须正数；最多 200000 片段。资源边界超出时非零退出。

### nice 设计和时钟口径

1. 父进程先取 `sched_getaffinity` 的实际掩码，自动选择第一个允许的逻辑 CPU，或验证 `--cpu` 指定值。目标、竞争者都通过 `sched_setaffinity` 绑定同一 CPU；结果记录实际 `sched_getcpu`。
2. 目标是固定 100000000 次 unsigned 64-bit xorshift。输出校验和使计算不能被优化删除；四组迭代数、输入和代码路径完全一致。竞争者使用相同运算与输入，每 65536 次检查停止管道，在目标退出后停止。**竞争者固定的是代码、nice=0、CPU 和 chunk 大小，运行总量由目标生命周期决定，不是固定总迭代数。** 原始表保留其实际总量和起止时刻。
3. 两个进程在设置亲和性与 nice 后向父进程报 ready，再等待同一管道门。父进程释放门后，内核仍不可能让同一 CPU 的两进程物理上同时执行。记录 `competitor_start - target_start`，分析时验证竞争者在目标完成前启动、完成后退出。数据采集时父进程在其他允许 CPU 上可运行，不更改父进程 nice 或亲和性。
4. 每轮四条件随机排列，LCG/Fisher–Yates、seed=20261029，10 轮。种子和实际 run、round、condition_order 全保留。此随机化用于减少顺序漂移，不用于统计推断；没有预热剔除、挑选有利轮次或异常值删除。
5. `elapsed_s` 为目标子进程工作区间 `CLOCK_MONOTONIC` 差值，不包含 fork、设置 nice、等启动门或父进程 wait 的时间。它包含工作期间的调度等待和被抢占时间。
6. `cpu_s` 为目标 `CLOCK_PROCESS_CPUTIME_ID` 差值，包含计算与极少量测时开销。并非硬件计数器。先读 CPU 起点再读 wall 起点，结束时顺序相反，可能产生微小计时包络差异。
7. 记录的是 waitpid 的原始状态字；成功为 0。每次 trial 默认 30 秒上限；出错或收到 SIGINT/SIGTERM 时清理本次创建的全部子进程并非零退出。不能处理 SIGKILL 或宿主强制终止的清理。
8. 只允许普通 SCHED_OTHER 且父进程初始 nice=0；不尝试提权降低 nice。设 nice 只作用于新子进程。权限、亲和性或策略不满足时打印 NOT_RUN；测量中失败保留 FAIL 行，不产出“成功汇总”。

## 3. 多轮统计与结论边界

四格分别计算样本数、均值、中位数、样本标准差（ddof=1）、极值与平均 `CPU/wall`。误差线是标准差，散点为全部原始运行；不是置信区间，没有做显著性检验。两张 nice 图分别展示四格比较和轮次漂移。

无额外竞争并不保证系统完全空闲；CPU 频率、宿主其他任务、中断、虚拟化、cgroup 配额和调度组仍可能影响结果。亲和性只限制逻辑 CPU，不能保证物理核心独占。环境快照见 results/environment.txt。目标负载约 0.19 秒 CPU 时间，短测量有启动瞬态；结论仅限此次机器、所用内核与该 CPU 密集型工作量。

预期是观察竞争是否改变 nice 作用强度，而非预设某个固定倍率。不能将本实验的两进程 CPU 密集模型与原汇报的五子进程混合负载合并统计，也不能用此结果证明真实 Linux 使用 FCFS/SJF/RR 模拟器。

## 4. 实际验证与失败案例

- C 的 CHECK 宏在失败时输出源码行并退出 1，不依赖可被 NDEBUG 关闭的 assert。
- PCB：空操作、单结点出入队/完成、末节点删除；10 个种子各 5000 次参考模型对照。7 次有意破坏：环、错误尾、第二运行态、双重归属、阻塞状态混入、丢失节点、F 留在活动队列。
- CLI：重复 ID、负 arrival、零/负 burst、额外字段、非整数/溢出数值、非法 quantum、缺文件、未知算法/事件、重复 PCB、非法运行量等均要求非零退出；demo 与场景文件输出逐字一致。
- AddressSanitizer/UndefinedBehaviorSanitizer 只用于教学模块的内存与未定义行为检查，单独构建，不用于时间测量。
- nice：先 4 次 smoke，成功后正式 40 次。分析器拒绝失败轮次、不完整四格、重复格、实际 CPU/nice 不符、目标工作量/校验和不一致、竞争生命周期不覆盖完成等情况。
- CI 只 `make test`，不运行计时基准。提交中包含工作流不代表云端 CI 已运行。

## 5. 参考来源

- Linux 内核 CFS 设计：https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html 。用于理解权重与虚拟运行时间，当前内核公平调度的实现细节不能由此基准逆推出。
- Linux man-pages sched(7)：https://man7.org/linux/man-pages/man7/sched.7.html 。普通策略、nice、调度组等口径。
- setpriority(2)：https://man7.org/linux/man-pages/man2/setpriority.2.html 。返回值/errno 处理、nice 权限。
- sched_setaffinity(2)：https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html 。实际允许 CPU 掩码及亲和性约束。
- 原始 PPT：由用户上传，保留未编辑副本和 SHA-256。原报告数字只作为背景，创新方案中的理论时间线用于程序校验。
