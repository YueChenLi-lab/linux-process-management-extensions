# 本次运行产物

正式数据来自此次新代码的实际执行，原 PPT 的既有数字不用于生成新数据。目录中除教学模拟与基准输出外，还保存了一次**预期失败的超时清理验证**和环境受限的 LeakSanitizer 日志；不能把这些文件混入成功性能汇总。

| 路径 | 来源 |
|---|---|
| tests.log | make clean all test 的真实 stdout/stderr |
| environment.txt | 系统/编译器/亲和性/配额快照；源代码、可执行文件哈希 |
| pcb/trace.csv | C 教学状态机 17 个事件（含 NOOP） |
| pcb/state_trace.png | Python 读取 trace.csv |
| schedule/*.tasks.csv | C 模拟器逐任务指标 |
| schedule/*.timeline.csv | C 模拟器原始片段，含 IDLE |
| schedule/*.summary.csv | C 模拟器各算法均值/切换计数 |
| schedule/comparison.csv、PNG/SVG | 读取上述模拟输出；不是 Linux 测量 |
| schedule/arrivals/ | 第二份到达/空闲工作负载输出 |
| nice/smoke.csv、smoke.*.log | 4 次 smoke、500 万次目标运算；不进入正式汇总 |
| nice/raw.csv、run.*.log | 40 次正式测量，1 亿次目标运算 |
| nice/summary.csv、summary.json | 仅由 raw.csv 汇总；样本标准差 ddof=1 |
| nice/nice_comparison.png、SVG | 四格经历时间与 CPU 时间，全部散点 + 均值/标准差 |
| nice/nice_rounds.png | 保留全部运行的轮次图 |
| nice/timeout_validation* | 巨大负载 + 1 秒超时，预期退出1，FAIL记录不参与汇总 |
| sanitizers.log | detect_leaks=0 下教学模块 ASan+UBSan 通过 |
| sanitizers_lsan_unavailable.log | 默认 LeakSanitizer 环境限制，未完成泄漏检查 |
| provenance.json | 命令/退出状态/来源 SHA-256 |

完整运行命令见 scripts/run_all.sh。benchmark 原始字段中 wait_status 是 waitpid 原始状态字，不是格式化的 shell 退出码；成功时为 0。start/end 为单调时钟秒数，只用于同一运行环境中的时差，不是日历时间。环境是在正式测量后记录，不能声称连续监控了主机状态。所有轮次都保留，没有删掉“异常值”。

首次编译被 -Werror 拒绝一处误导缩进，修复后干净编译/单测通过。最终程序哈希以 provenance.json 和 environment.txt 为准。源代码更新后复跑了必要测试；nice 核心在正式测量后未做算法改动。
