# HTTP 与通信链路性能优化：两阶段实施计划

日期：2026-10-28。

状态：第一阶段所选优化已实施并完成本轮验证；严格性能恢复验收未通过，阶段二未启动。未提交、未推送。

## 1. 目标与阶段顺序

用户要求按两个阶段进行：

1. **阶段一：只优化 #199 新增或明确放大的成本，先让性能至少恢复至修改前。** 保留已经确认的数据结构、编码行为、原始字节和协议校验。
2. **阶段二：阶段一独立验收后，再优化其它地方。** 减少旧版已有成本，并按真实负载继续优化整条通信链路。

本计划作为后续实施顺序和验收依据，取代 [全链路分析报告](D:/GitHub/Delphi-Cross-Socket/docs/plans/2026-10-28-communication-chain-performance-review.md) 中原先混合排列的批次。分析报告保留为问题、调用链、源码位置和历史反例的参考。

阶段一未达标时继续定位和优化 #199 范围，不自动启动阶段二，也不把第二阶段的收益合并后宣布 #199 已恢复。

## 2. 固定合同与工作区保护

- 保留 TParamEntry 的 Name、Value、RawValue、HasRawValue，以及两个 Create 构造器。只有字节来源持有自有 raw；空 raw 和无字节来源仍有区别。
- 保留 THttpHeader 从 TBaseParams 派生、两个 Decode 入口、公开接口/属性/异常合同及现有接口 GUID。
- 字节值继续通过 TUtils.GetString 转为字符串，保留 BOM、UTF-8 探测、默认编码回退和各平台既有转换/异常行为。阶段一不改 Utils.Utils。
- HTTP 结构及控制字段按原始字节检查，保持失败回滚、重复字段规则、正文定界及 multipart 失败传播。
- RawRequestHeader 保留自有快照，属性返回防御性副本；输出使用当前 Value，整块无 BOM UTF-8，非法字段或无效 UTF-16 整次失败。
- 保留头体分离、响应 FIFO、每次写独立完成保护、异步缓冲寿命、回调一次及锁外用户通知。
- 沿用此前已确定的默认 SOMAXCONN、eager Cookies/Params/Query 和现有容量/背压策略。不以改线程数、分配器或队列限额抵消第一阶段的回退。
- 当前已暂存的注释、未暂存的基准驱动及未跟踪文件属于现有工作，实施前记录并保护。使用隔离快照或工作树；不覆盖用户改动，不执行丢弃工作区内容的 Git 命令。
- 每项保存独立补丁/快照和结果，不要求为了测量先提交 Git；提交、推送另按用户指令执行。

## 3. 两阶段的明确边界

按成本来源划分，而不是按“文件是否被 #199 修改过”划分。同一个文件内，可能同时存在新增成本和旧成本。

| 内容 | 阶段 | 归因与限制 |
| --- | --- | --- |
| 控制字段 Unicode 比较链 | 一 | 分类函数由 #199 新增；保留原始字节校验 |
| 共用扫描器 ReadChar/模式分支/Name 写入 | 一 | #199 替换扫描实现带来的执行成本 |
| TParamEntry 托管记录读取与传递 | 一 | 记录增大、增加 raw 后放大的搬运成本；只优化读取，不顺便改通用比较或新增索引 |
| 无 query 时重复 GetString | 一 | 旧版直接复用 RawPathAndQuery，#199 改成对相同区间再次解码 |
| 新增 UTF-16 检查、重复检查及 UTF-8 输出路径 | 一 | 在保留完整检查覆盖和输出字节的条件下优化 |
| 字节构造、Header 内部清理中被新记录放大的临时搬运 | 一的预备项 | 必须先有消融/分配证据；范围限定 Header 相关生命周期 |
| UrlDecode 无转义快路 | 二 | UTF-8 往返在旧版已经存在 |
| 默认匿名 comparer 去除/共享 | 二 | 旧版已经每容器构造比较器 |
| Header 定长 builder | 二 | 旧版已有字符串逐项追加；阶段一只处理新增校验/编码成本 |
| 通用元数据提取、默认头查找、永久索引 | 二 | 原有查找成本；永久索引暂不作为首选 |
| 跨请求保留容量、对象池、通用 Clear 策略 | 二 | 涉及旧容器策略及内存保留 |
| 外层 Parser 中转、路由、正文、Session、客户端调度、TCP/TLS/WS | 二 | 其它链路专项，需要独立负载和生命周期证据 |

## 4. 基线与证据组织

### 4.1 三份阶段一版本

| 标记 | 版本 | 用途 |
| --- | --- | --- |
| A | `566187c283ebb3105e57ac9c8dd148a952b014d1` | #199 前的恢复目标；已含此前 GetString 优化 |
| B | 当前 `5e63745` 实现及已有注释，实施前冻结 | #199 当前成本和正确性合同 |
| C | 只包含阶段一优化的最终冻结版 | 分别与 A、B 比较，独立证明恢复 |

阶段一每一步另有 C01、C02…快照。单项比较“上一已保留快照→候选”，同时保留原始 A/B；不能只做相邻比较而失去最终恢复目标。

所有生产依赖、测试源、编译参数、工具版本、二进制、输入和驱动均记录 SHA-256。计时、MM 计数、堆占用、profiler 分别运行。每批使用新 label，保留失败候选和原始输出，不覆盖上一轮证据。

已有数据见 [PERFORMANCE.md](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/PERFORMANCE.md) 和 [PERFORMANCE_RESULTS.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/PERFORMANCE_RESULTS.json)。历史绝对时间用于判断量级；最终达标以同批、同条件重新测得的 A/C 为准。

### 4.2 当前恢复目标的量级

以下为已测 10 字段 ASCII、中位数 μs/次；括号中为当前 B。它们是历史参考值，不是跨批次硬编码阈值。

| 项目 | Delphi Win64 A（B） | Delphi Win32 A（B） | FPC Win64 A（B） |
| --- | ---: | ---: | ---: |
| 字节→字段 | 3.438（5.376） | 3.698（4.365） | 3.008（10.563） |
| 字符串→字段 | 2.972（4.200） | 2.446（3.417） | 2.796（4.128） |
| 复用请求 ParseHeader | 5.729（7.587） | 5.488（6.243） | 16.817（27.339） |
| 新建/解析/释放请求 | 6.665（8.633） | 6.156（7.135） | 17.782（28.233） |
| 响应 ParseHeader | 4.557（6.153） | 4.472（4.995） | 7.755（16.013） |
| 出站生成与 UTF-8 | 1.420（2.554） | 1.312（2.305） | 2.144（4.476） |

旧 Delphi 对原始 #199 报文输出错误，该组合只比较 B→C 和正确性，不把旧错误路径当恢复目标。FPC 旧版在该输入结果正确，可作 A→C 比较。同样，旧 ASCII 输出会破坏的 Unicode 发送组合、其它旧版结果不等价的输入，不套用 A→C；先核对预期结果再确定参考版本。新增非法输入拒绝路径也以 B 的合同为准。

## 5. 阶段一：只追回 #199 回退

### S1-00：冻结范围，补足基准能力

**交付：** A/B 不可变源码、白名单、输入/工具清单、逐项任务表，以及可比较冻结目录的驱动。

实施内容：

1. 检查 git status，记录现有暂存/未暂存内容。确认没有竞争基准或其它会话写同一源码快照。
2. 第一阶段生产修改白名单以 Net.CrossHttpParams、Net.CrossHttpServer、Net.CrossHttpClient、Net.CrossHttpUtils 的相关实现为主；Net.CrossWebSocketClient 如需改动，限于 #199 相关握手调用。每个 hunk 写明归因。
3. 阶段一不改外层 Net.CrossHttpParser、Socket 后端、TLS、路由、Session、通用 UrlDecode、默认 comparer 或全局构建开关。
4. 扩展现有 [run_bench.py](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/run_bench.py)：支持固定源码目录、完整门槛项 A/A、可配置 HTTP A/A 轮数和计时窗口、配对统计及通过/未达标/未定状态。
5. 修正 HeaderBench 中“复用容器容量”的说明；对象复用和容量复用分开统计。补充必要的有/无 query、不同规模输出及 Unicode 代表输入。
6. 验证更新后的驱动能区分 A/A 与已知 A/B 回退。先验证测量可靠性，再开始优化。

**完成条件：** 生产源未发生优化修改，A/B 可重建并通过语义核对，范围与测量能力明确。新增的是验证能力，不是阶段二生产收益。

### S1-01：控制字段一次分类

**位置：** [Net.CrossHttpParams.pas:1484](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1484)、[1609](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1609)。

以私有 ASCII 分类替代最多 12 次 Unicode 比较；先按长度分组，再比较，直接得到普通值、ASCII 控制值、允许引号 obs-text 的控制值。移除命中后再次判定 Expect/TE 的重复比较。

**验证：** 所有控制字段和普通字段、混合大小写、同长度近似名、重复项、BOM、引号/反斜杠/高位字节；三编译器 4/10/40 字段和长 Cookie。确认生成代码，收益按字节入口报告，不能把字符串入口收益归给此项。

**保留条件：** 分类结果及原始字节拒绝行为一致，目标成本下降，其它输入没有重复性回退。

### S1-02：优化新扫描器与 Name 构造

**位置：** [Net.CrossHttpParams.pas:1539](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1539)、[1602](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1602)。

分两个独立候选：先在 SetLength 后取得一次可写指针，消除逐字符唯一性检查；再比较入口一次选择 byte/Char 扫描与保留共用流程但消除逐字符调用。字符串模式可直接复制已经校验的 Name 区间。以三编译器结果选择简单且稳定的写法。

**验证：** 显式长度、nil/负长度、CRLF 各边界、NUL/DEL、冒号/OWS、空值、最后一行无 CRLF、终止空行及其后数据；AClear=True/False、转换异常和失败回滚。byte/Char 两入口使用同一组语义表。

**保留条件：** 无越界宽读、不改入口单位，不仅添加 inline，还要证明相关调用/分支确已减少且计时改善。

### S1-03：减少新记录带来的只读复制和临时传递

**位置：** [Net.CrossHttpParams.pas:1088](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1088)、[1193](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1193)、[1226](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1226)、[1637](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1637)。

先减少同一轮重复 Items 读取；再验证各 RTL 支持的合法内部只读视图/元素访问，避免只读 Name 却复制整个含 RawValue 的记录。检查新字节构造和 Add 之间是否存在可减少的临时记录传递。

**边界：** 保留 TList 和公开记录布局；不强转私有 RTL 布局，不改通用 Unicode 比较，不加永久索引，不改变动态数组共享规则，不把自有 raw 改成借用指针。视图只在不修改容器、无用户回调的区间内使用。

**验证：** 查找、重复项、Remove、Items/Assign/枚举、Sort、raw 共享与字符串覆盖；FPC 生成代码、分配/引用传递，以及复用、新建/释放两种请求路径。

### S1-04：消除 #199 新分段解码中的重复工作

**位置：** [Net.CrossHttpServer.pas:4893](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:4893)、[4942](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:4942)、[Net.CrossHttpClient.pas:3387](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpClient.pas:3387)。

无 query 时让 RawPath 直接复用已转换的 RawPathAndQuery；审查新请求/响应分段实现，对完全相同的区间只做必要转换。路径/query 的结构仍从原始字节确定，参数继续先分段再解码。

**边界：** 不改通用 UrlDecode，不改旧元数据提取策略，不改 RawRequestHeader getter 的复制合同，不引入属性惰性解码。

**验证：** 无 query、空 query、多个问号、%3F/%26/%3D/%252F、中文、非法 UTF-8/BOM 的既有结果或异常；请求展示文本、路径/查询属性、参数及输入覆盖后的快照。补请求与响应的局部性能比较。

### S1-05：减少新增输出校验和编码成本

**位置：** [Net.CrossHttpParams.pas:1631](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1631)、[Net.CrossHttpServer.pas:5782](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:5782)、[Net.CrossHttpClient.pas:2689](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpClient.pas:2689)。

分开验证：

1. 在字段合法性遍历中完成 UTF-16 配对检查，保留严格整块失败；避免再次扫描已验证的整个字段块。
2. 明确首行、独立 Set-Cookie、请求 method/target/query 的检查责任，调用方只省略确实重复的部分。
3. 如编码仍是热点，可在已有扫描中顺带得到“全 ASCII”标记，比较 ASCII 等价的无 BOM UTF-8 输出快路；非 ASCII 继续既有 UTF-8 编码。该项是独立候选，不强制采用。

**重要限制：** 客户端 URL/query 在百分号编码前的 UTF-16 检查不能直接删掉，因为转换可能先替换非法代理，最终头检查无法发现原始错误。Cookie 和首行也不能漏检。全 ASCII 标记覆盖实际输出的全部段，不能只根据字段块选择快路。合并检查使用私有组合校验或保持公开方法原有语义，不改变 IsValidHeaderValue 等公开方法的返回值/异常合同。阶段一保留原字符串追加方式，定长 builder 放阶段二。

**验证：** ASCII/中文/emoji/U+FFFD、孤立高低代理、CRLF/NUL 注入、空头、多字段、Cookie、HEAD/204/304、客户端自动/手动 URL 编码；合法字节一致、失败零写入、回调一次。长 Unicode 输入和小头分别计时，避免快路增加的检查使非 ASCII 退化。

### S1-06：未恢复时的范围内补充定位

只有 S1-01～05 后仍有未达标项时启动。分配计数、采样及消融分别定位 Name/Value/raw 构造、托管记录传递、Header 清理/回滚/释放成本，先解释差额，再选择最小候选。

允许研究“新记录放大了的临时搬运”，例如仅 Header 相关路径减少 FPC 删除临时记录的开销；必须证明它与 #199 回退相关，并验证新建/释放的真实路径。通用 Clear 合同、跨请求保留容量、对象池、旧追加 builder 仍留在阶段二。

**退出规则：** 候选无净收益或引入正确性/内存问题就撤回该候选并记录。若范围内无法达到目标，阶段一保持未完成，说明剩余成本和证据；不擅自把阶段二候选迁入抵消。

### S1-07：累计验收与独立交付

冻结最终 C；审核它只含阶段一允许的修改。按第 6 节完成 A→C、B→C，不用总平均掩盖单项回退。

**交付：** 采用/否决候选表、生产改动清单、三编译器结果、实际 HTTP 结果、正确性与生命周期结果、A/B/C 分配和内存数据、完整原始样本、遗留项及平台覆盖表。

阶段一完整验收后，C 才成为阶段二基线。验收完成不自动产生 Git 提交或推送。

## 6. 阶段一验收门槛

### 6.1 必测覆盖

| 组 | 内容 | 判定 |
| --- | --- | --- |
| 三编译器微基准 | Delphi Win64、Win32、FPC Win64，各自使用固定 Release 参数 | 独立判定，不跨编译器平均 |
| 字段解码 | byte/string；4/10/40 字段、8 KiB Cookie、原始 #199 | 旧版正确的组合 A→C；旧版错误的组合 B→C |
| 完整请求 | 有/无 query，复用对象、新建/解析/释放 | 防止只优化字段集合或复用基准 |
| 完整响应 | Response.ParseHeader；短/多字段/Unicode 代表项 | 防止只修服务端 |
| 输出 | 字段文本、完整头、最终 UTF-8；小/多字段/Unicode | 恢复检查和编码路径，保持整块失败 |
| raw/所有权 | 来源、原字节、空值、共享/覆盖、快照副本、输入复用 | 正确性硬门槛 |
| 生命周期 | multipart 失败、双向 WS 握手、生成失败、取消/迟到回调 | 全部通过 |
| 实际 HTTP | Delphi Win64，默认 RTL/FastMM5，ab/Bombardier 分组 | 128 并发、65 I/O 线程、210 B 响应、默认 backlog、头体分离 |
| 内存 | RTL 申请数/累计字节、5000 对象存活堆增量、释放回落 | 分开报告，不混称峰值/RSS |

三 Windows 目标都必须有真实 loopback 正确性。Linux Delphi/FPC 复用已有 runner 做实际运行回归；不能用交叉编译代替运行。macOS/ARM 无环境时明确列为未覆盖。

本阶段整机 HTTP 性能声明以已有 Delphi Win64 场景为范围。若实施时要声称 FPC/Win32 整机吞吐也恢复，必须先为对应编译器建立等价 A/B/C 服务端基准；其微基准不能代替整机证据。

### 6.2 采样规则

- 单项探索先用 7 对交替 A/B，计时窗口至少沿用原 250 ms，必要时提高；只用于筛选，不作为最终恢复声明。
- 所有最终门槛项做 A/A；微基准固定 CPU 掩码 4、预热，循环数和计时窗口在比较前校准。微基准 A/A 中位偏差目标控制在 1% 内，趋势/离群和逐对分布同时检查。
- 最终候选冻结后建立独立确认批次：微基准每组合固定 31 对、每次约 1 s；实际 HTTP 每工具/分配器固定 15 对，Bombardier 从 10 s 延长到 30 s，ab 保持预热与请求数一致。最终批次不提前停止或只保留有利轮次。
- HTTP A/A 至少 5 对，轮数可配置；若仍有明显漂移或波动，先调查环境/等待/调度并记录，再预先规定新确认批次的条件和样本数。不能把历史约 7% RTL 噪声当作允许下降 7%。
- 窗口、样本数和输出方案在开始该批前记录；失败和未定批次保留。带 profiler/MM hook 的诊断批次与普通计时分开。

上述数量是计划初始配置；实施前可根据 A/A 调整，但需先记录理由、统一重测基线，不能在看见有利结果后改变门槛。

### 6.3 “至少恢复”的判定

退化预算为 **0%**，不默认增加允许慢 3% 或 5% 的宽限。

每对计算：参考版本 R 按第 6.1 节选择，旧版正确时 R=A，旧版错误/不等价时 R=B。耗时/CPU/P99 使用 `r=C/R`，吞吐使用 `r=R/C`，统一 r>1 表示更差；R=B 的通过只证明新合同路径未退化，不宣称恢复到旧错误实现。报告配对比值中位数、分布及按整对重采样的单侧 95% 上界；区间衡量运行间不确定性，不冒充绝对零误差证明。

- **达到目标：** 中位 r≤1，且单侧上界≤1，有足够证据支持恢复。
- **未达标：** 有可重复的实质回退，继续阶段一定位。
- **未定：** 结果接近 1 但区间跨过 1，或 A/A 不稳定；不能直接按通过处理。重新设计独立确认批次，仍未定就保留未定状态。

同一候选的有效确认批次未定时，最多再安排一次事先固定条件与样本数的复测，两批结果均保留；结果矛盾或仍未定时维持未定，不无限重复直至偶然过线。只有有证据说明环境/批次无效，或产生了新的候选，才重新建立相应确认批次。

严格零退化要求可能使近乎相等的实现长期无法获得足够证据；这表示测量结论未定，不能自动把目标改成“差不多”。所有等价性能组合逐项列状态，不能用总体平均抵消某编译器或短头的回退。

实际 HTTP 先要求零失败、零超时、零非预期状态及响应内容/长度正确，再评价成功 RPS；ab/Bombardier、RTL/FastMM5 各自判定。RPS、P99 和 CPU/成功请求分别检查，存在重复性恶化时不得只凭吞吐宣布完整恢复。

新增 raw 的存活内存不要求回到旧版没有 raw 的数值；要求 C 相对 B 没有无解释的恶化、释放回落、无无界保留。A/B/C 的占用均需公开，时间性能恢复和内存功能成本分别说明。

### 6.4 进入阶段二的条件

1. 范围审计通过，C 未混入第二阶段收益。
2. 正确性、raw、失败和生命周期回归全部通过。
3. 三编译器门槛组合和已覆盖实际 HTTP 场景达到恢复目标；未恢复/未定项不能被省略。
4. 内存、分配和释放情况可解释，平台覆盖明确。
5. 阶段一报告、不可变 C 和可复跑证据齐全。

任一关键项不满足，状态保持阶段一未完成。需要调整范围或目标时先更新计划并说明原因，不自行跨阶段。

## 7. 阶段二：优化旧成本及其它链路

### 7.1 基线、顺序和采纳规则

以阶段一交付 C 为直接基线，继续保留 A。第二阶段每项得到 D01、D02…；比较前后局部成本，阶段里程碑再比较 C/D 和 A/D，确认既得 Header 性能保持。

先做低风险、通用热路径的 S2-01～04；再根据阶段一结束时的 profile/负载选择其它项。后续表中各行为独立候选，不能一次打包改完整链路。只有热点证据、正确性和净收益成立才采纳；否决也要记录原因。

### 7.2 通用优化与专项任务

| 编号 | 工作与位置 | 最小实施方案 | 专项验证/限制 |
| --- | --- | --- | --- |
| S2-01 | UrlDecode，[HttpUtils:1822](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpUtils.pas:1822) | 全 ASCII 且无 %/+ 时复用原字符串 | 中文、非法代理和百分号走原算法；普通 GET、query、目录路径，三编译器 |
| S2-02 | 默认 comparer，[Params:1129](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1129) | 普通 TList.Create 或共享不可变 comparer，选简单者 | 核对默认初始化、Sort/Assign/重复项；测新建/释放，不改 eager 创建 |
| S2-03 | Header 输出构造，[Params:1631](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1631) | 校验计长、一次 SetLength、直接填充，复用阶段一校验保证 | 小头/长头/Unicode/溢出，保持首行/Cookie/整块失败；不先假定 StringBuilder 更快 |
| S2-04 | 元数据和默认字段，[Server:4995](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:4995)、[Client:2634](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpClient.pas:2634) | 局部复用值/位置；再评估一次提取已知头 | 保留首项/重复项/CLTE/Connection/Cookie 规则；永久索引不作为首选 |
| S2-05 | 容量与清理，Params 容器 | 有界预留/小容量保留分别实验 | 新建与复用分列；偶发大头后内存回落、异常释放；不改第三方 RTL |
| S2-06 | HeaderStream 中转，[Parser:463](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParser.pas:463) | 当前包有完整头且无旧分片时，同步借用区间解析；分片走原路径 | 每字节拆分、严格 CRLF、头体同包、pipeline、Upgrade 余留、回调重入与寿命 |
| S2-07 | 路由，[Server:3649](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:3649)、[3990](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:3990) | 复用本次 method/segment 规范化结果；再研究正则构造 | 短静态/参数/正则、多层回退、非 ASCII；可变 matcher 不跨线程共享 |
| S2-08 | 压缩，[Server:5592](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:5592) | 便宜条件先退出、MIME 只规范化一次、必要时减少 q-value 临时处理 | q/重复编码/HEAD/已有 Content-Encoding；先测协商，再决定 zlib 池化 |
| S2-09 | 正文/上传，[Server:3054](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:3054)、[Params:2478](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:2478) | 已验证小正文有界预留；multipart 参数单次扫描和一次构造分别做 | 压缩长度、阈值、many-small-parts、大文件、分片、磁盘失败、清理；不扩展 filename 语义 |
| S2-10 | 客户端，[Client:4265](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpClient.pas:4265)、[4992](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpClient.pas:4992) | 一次锁内超时分类、缩短 dock 锁内工作；大池/稀疏超时有证据后再调整 | 关闭前重新认领、取消/完成竞争、上传未发完、额度；默认小池不先上新队列 |
| S2-11 | Session，[Sessions:778](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpSessions.pas:778)、[826](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpSessions.pas:826) | 先测写锁等待和全表扫描；必要时设计有界批次 | 1k/10k/100k、零/稀疏/全过期；Touch、续期、同 ID 替换、钩子和锁外释放 |
| S2-12 | 文件发送，[Server:5461](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:5461) | 分开测元数据/打开/读盘；必要时减少路径查询 | 304、Range、文件替换、慢读者、取消；sendfile/TransmitFile 单独后续设计 |
| S2-13 | TCP 后端，[Iocp:126](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossSocket.Iocp.pas:126)、[Epoll:967](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossSocket.Epoll.pas:967) | 先统计上下文和事件注册；再决定有界回收或减少重复注册 | 完成包/停机 drain 后才能回收；部分写、ET/ONESHOT、并发关闭及丢唤醒 |
| S2-14 | TLS/WS，[OpenSSL:884](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossSslSocket.OpenSSL.pas:884)、[WS Parser:395](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossWebSocketParser.pas:395) | 新证据下研究 TLS 缓冲/通知；WS 独立试复制+mask 合并 | TLS1.2/1.3、WANT、密文顺序；WS 新帧重置 offset、未对齐/尾字节、真实 CSPRNG、已交付数据寿命 |

S2-09 中正文、multipart，S2-10 中超时、池、冷连，S2-14 中 TLS、WS 都拆成独立子项验证，不能因列在同一行而一并改动。

同步 DNS、I/O 线程数和业务执行模型列为诊断/独立设计项：先分阶段测冷连时间、线程等待与慢业务影响；不默认加 DNS 缓存、修改默认线程数或把所有请求自动投递线程池。

### 7.3 历史反例与高风险边界

- TLS 空 BIO 提前返回已有 Delphi 大块接收回退并撤回，见 [BUFFER_BENCHMARK.md:62](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/TlsIoTests/BUFFER_BENCHMARK.md:62)。有新的归因证据才重开，不能仅凭少一次分配直接采用。
- WS 一次构帧分配、只清零帧头、真实随机 mask，基础发送无限额快路、FIFO、handler 快照和稀疏清理等已有优化继续保留。
- Header、request/response 或 I/O 对象池需要最终引用/完成所有权证据；不能按逻辑发送结束就复用仍被外部持有的对象。
- 新公开配置、接口、默认策略或协议行为不作为性能补丁的附带改动；有必要时另列设计和迁移影响。

### 7.4 阶段二验收与交付

每个候选先做相关正确性、局部时间/分配和适用负载比较；影响共享 Header/编码 helper 的候选重跑三编译器阶段一门槛。互不相关的专项不机械重复全部长压测，在里程碑和最终累计版统一验收。

性能目标是相对 C 在预先指定负载上得到超出噪声的改善，并保持已恢复的 Header 路径。A/C/D 同列，区分通用收益和专项收益，不把 TLS、上传或 Session 的数字外推到普通 GET。

交付包括：候选采用/否决/不适用清单；每项证据和独立补丁；最终累计收益；CPU、成功吞吐、P99、分配/存活/峰值内存；Windows/Linux/其它平台覆盖和未验证项。尚有必需工作或关键回归时不标为完成。

## 8. 执行追踪表

| 项目 | 当前状态 | 完成依据 |
| --- | --- | --- |
| 两阶段计划与边界 | 已形成，尚未实施 | 本文及全链路分析报告 |
| S1-00 基线/驱动 | 已完成 | A/B 快照、范围白名单、完整 A/A 能力 |
| S1-01～05 主优化 | 已实施所选项；S1-04未采纳 | 每项正确性、生成代码/成本及三编译器比较 |
| S1-06 剩余回退 | 已实施并完成候选收敛 | 有归因后才选择候选；无须实施时说明 |
| S1-07 累计验收 | 本轮验证完成；严格恢复未通过 | 第 6 节全部门槛及独立阶段报告 |
| 阶段切换 | 未满足 | C 已冻结且阶段一验收完成 |
| S2-01～04 通用优化 | 未启动 | 以 C 为基线，独立验收 |
| S2-05～14 专项 | 未启动 | profile/负载选择及各项回归 |
| 最终累计验收 | 未启动 | A/C/D 对比、候选处置、风险和平台覆盖 |

## 9. 风险与调整规则

主要风险是新增 raw/严格检查有不可消除成本、FPC 托管记录和 RTL 行为不同、小头与长头收益冲突、真实 RTL 服务噪声大，以及生命周期优化引入竞态。处理方式是最小候选、逐项消融、跨编译器验证和失败证据保留。

阶段一恢复是目标，当前没有优化后的数据可证明必然达到。达不到时准确报告剩余差额和原因，保持阶段一状态；不删除已确认功能、不用第二阶段收益掩盖、不通过更换比较条件宣布成功。

实施中需要变更范围、顺序或验收方法时，在本文追加带日期的变更记录，说明证据和影响，再继续相关工作。回退候选采用精确反向补丁或隔离快照，保护用户已有工作。Git 提交消息使用中文，提交范围只含用户授权内容。


## 变更与执行记录

- 2026-10-28：开始阶段一。A/B 各 68 个源文件已冻结于 HttpHeaderTests/bin/s1-199-20261028；现有工作区 21 个文件及暂存/未暂存补丁已备份。新增行为回归在当前 B 的 Windows 三目标各通过 1633 项组件检查和 22 个 TCP 场景。S1-00 驱动的真实输出基准仍在做语义校验；生产优化先在隔离候选中进行，尚未写回主源码。

- 2026-10-28（实施进展）：S1-01/02/03及S1-05已写回并通过当前Windows回归；S1-04无query复用仍为待确认候选，未写回。S1-05新增安全具体工具方法TCrossHttpUtils.EncodeHeader(string):TBytes，ICross接口/IID和TParamEntry结构不变。出站微基准跟随冻结源码的真实生成路径，旧表达式保留作补充，避免继续计入已删除的重复校验；不降低0%退化目标。S1-06继续分析原位构造与GetString终端指针重载。当前未完成累计A/C验收，阶段二未启动。

- 2026-10-28（累计验收采样安排）：已冻结selected C。探索已显示小头可能存在明显未恢复，因此本轮按“完整广度+独立关键确认”收敛验证：A/C全部43组合×三目标先固定7对、A/A5、250ms；另用独立label对wire small/ascii/cookie、request ascii、new-request ascii、response ascii、encode small/ascii、server-encode ascii三目标固定31对、A/A5、1s。A无效组合按固定列表另测B/C。HTTP仍原15对、A/A5、30s，内存A/B/C。此安排不把未测31轮的其余项视为通过，也不降低0%预算；若任一关键项失败，结论是阶段一严格恢复验收未通过，不重复完整31×129矩阵来尝试刷过线。第二阶段不启动。完整全门槛通过验收仍保留为原目标，而非本轮缩小后自动完成。

- 2026-10-28（验证进展）：当前C的Windows三目标Release/Checked各1890组件+22TCP通过；Linux两目标Release/Checked实际运行通过；最终只读代码/范围审查未发现实质问题。无query复用、全ASCII整块视图、两/四码元循环展开均未采纳。选择C的完整性与既有index/用户HTTP驱动哈希保持，尚待累计性能结果。

- 2026-10-28（S1-04一次独立整机复核）：固定C的80服务会话及内存已完成，RTL15对吞吐/P99均反向，不能标第一阶段恢复。基于新的实际分配器负载证据，对未采纳的无query文本复用只做一次独立RTL真实服务筛查：参考为冻结selected C，候选为noquery-selected/source，仅Server相同范围一行逻辑变化；128并发/65线程/210B/头体分离/backlog保持，固定A/A3对及A/B5对、Bomb30s、ab原预热/计数。只用新label，不改主/frozenC/原结果；无净益则否决，不增加其它候选/阈值，不重复刷过线。它仍属S1-04，不进入第二阶段，且无论结果如何都不覆盖独立wire小头等已确认失败。

- 2026-10-28（本轮收尾）：已完成129组合A/C广度、27组合独立31对确认、A不等价26组合B/C及两项唯一31对补充、80个实际HTTP会话、A/B/C存活/释放与独立MM计数。27确认=19通过/4回退/4未定；RTL吞吐/P99和FastMM5 Bomb吞吐仍回退，因此严格0%恢复目标未达成。所有功能/跨平台/正式构建与完整性检查通过。额外noquery独立16会话五项均未证明净收益，按预定整体否决，不复测/不采纳。选定C保留已验证优化，不改写其失败事实，不进入阶段二。最终报告见Net/Tests/FPC/HttpHeaderTests/STAGE1_RESULTS.md。
