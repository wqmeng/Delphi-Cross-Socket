# #199 第一阶段继续优化与归因

本报告记录上一轮 `STAGE1_RESULTS.md` 之后的继续工作。当前主选定版为 **J = E 三项 + I2 + K2（仅 Delphi）+ L**，已冻结于 `bin/s1-followup-unwind/selected-j/source`。Windows 三目标 Release/Checked 与累计版 Linux 实际运行均已通过；最终 A/J 关键 12 组合的 31 对复核为 **10 项通过、2 项回退**。两组真实 HTTP 筛查也已结束：默认 RTL 的 Bombardier 吞吐仍回退，FastMM5 全部指标未定，**第一阶段尚未完成**。

旧结果、失败候选和未定项全部保留；各候选单独冻结、同批配对。本文的局部筛查不代替最终累计恢复验收，不能把不同批次的点中位数拼成配对，也不能把各候选百分比相加。

## 版本和本轮范围

- A：#199 之前的 `566187c`，使用原冻结源码。
- C：上一轮 selected C，也是本轮起点。
- D：C 加合法空行正常退出和失败回滚冷路径。
- E：D 加 FPC Header 列表清理优化。
- F：E 加直接复用字符串字面量的名称候选，未采纳。
- G：E 加初始化一次的固定名称表，避免 Delphi 复制字面量。
- H：G 直接将名称表引用赋给最终记录，省去中间 LName 引用；A/H、E/H 各有独立真实 HTTP 筛查，未纳入 J。
- I2：E 加 Byte token 集合与安全 8 字节值校验；保留其失败修订前身 I 的证据。
- K：E 加规范大写起始行文本的精确白名单；三编译器均试验。
- K2：K 的 helper 和两处调用仅对 Delphi 启用，FPC 保留原 GetString＋ToUpper。
- L：E 的 Header.Encode 去掉 Name/Value 中转托管局部，直接 const 借用记录字段。
- J：当前主累计选定版，组合 E、I2、K2、L；不含 F/G/H 的固定共享名称表。
- M：基于 J 的名称四字节扩宽候选，已完成三目标筛查；因小头及 FPC 真实短请求反向，未采纳。

相对本轮起点 C，当前采纳修改位于 `Net.CrossHttpParams.pas` 和 `Net.CrossHttpServer.pas`；专项用例已接入现有 HttpHeaderTests。保留 `TParamEntry` 四字段、Header 从 `TBaseParams` 派生、每项自有 raw、`TUtils.GetString`、原字节协议校验、失败回滚及原始请求字节快照。头体分离、默认 backlog、I/O 线程配置和默认分配器不变。既有其它改动及暂存内容不属于本报告新增提交范围。

## 三个已经证实的额外成本

### 1. 正常成功路径触发 FPC 异常栈展开

原字节解析器遇到合法终止空行，在 `try/finally` 内执行 `Result := ...; Exit`。本机 FPC Win64 的目标文件显示此路径调用 `_FPC_local_unwind`，安装的 `rtl/win64/seh64.inc` 再调用 Windows `RtlUnwindEx`。这不是异常输入才承担的成本，每次正常结束都会发生。

改为先拒绝非法尾随，再 `Break`，由循环后统一设置成功并正常离开 `try`。异常和语法失败仍走原回滚。C 对第一候选的独立筛查中，小头字节解析由约 1.165 µs 降至 0.777 µs。该固定成本说明，上轮不能把所有小头差距都归因于 raw 复制。

### 2. 失败分支的 Delete 使成功路径初始化托管临时记录

FPC 内联 `TList<TParamEntry>.Delete` 后，为 finally 中只在失败时需要的删除操作，给整个 Decode 生成了 32 B 托管临时记录，并在成功调用中初始化/终结。改成不内联的私有 `_RollbackHeader`，仅失败时调用。数据和回滚顺序不变。目标文件对比确认临时量被移出成功路径；独立小头筛查约 0.782 → 0.769 µs。

### 3. FPC 清空列表先复制出整批待删除记录

FPC 的原 `Clear`/析构经过 `DeleteRange`，先创建临时删除数组，再搬运记录、发通知及释放。#199 把记录从 16 B 增至 32 B，并增加一个托管 raw 字段，这条路径的成本随之放大。

仅为 FPC 的 Header 构造真实的私有 TList 子类：无通知时通过受保护字段释放后备数组及全部托管引用；有通知时完整回到原 RTL 路径。清空后容量为零，不跨请求保留数组。析构经虚方法同样覆盖；其它参数列表和 Delphi 继续原实现。独立 E 对 D 筛查，小头约 0.786 → 0.660 µs，10 字段约 2.994 → 2.689 µs。

## FPC 独立 31 对确认

下面是 A 对 E 的**同批配对**，每项 31 对、约 500 ms 窗口、A/A 7 对，默认分配器，亲和掩码 4。耗时比 E/A 小于 1 表示更快；预设恢复预算仍为 0%。

| FPC Win64 操作 | A 中位 µs | E 中位 µs | 配对 E/A | 单侧 95% 上界 | A/A 中位偏差 | 判定 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 4 字段字节解析 | 0.761668 | 0.647353 | 0.851412 | 0.853183 | +0.0343% | 不慢于 A |
| 10 字段字节解析 | 3.046222 | 2.707994 | 0.887995 | 0.891655 | -0.5990% | 不慢于 A |
| 4 字段字符串解析 | 0.691191 | 0.425593 | 0.616389 | 0.617438 | -0.2226% | 不慢于 A |

三项配对耗时分别降低约 14.9%、11.2%、38.4%。这证明上一轮最突出的 FPC 小头失败项已经追回，**不代表所有平台和整机验收已经通过**。证据：`bin/s1f-fpc-confirm/{micro.json,micro-settings.json,validation.json,source-manifest.json}`。

## 真正的压测输入很短

按实际工具抓取请求：Bombardier 为 63 B、2 个字段（User-Agent、Host）；ab 为 107 B、4 个字段（Connection、Host、User-Agent、Accept）。原来的 10 字段 691 B 输入不能代替这两种负载。

在原冻结基准中加入捕获字节，不改变解析/计时实现。A/D 独立 7 对筛查中，Delphi Win64 新建、解析、释放请求对象的点中位分别为：ab 3.017 → 3.173 µs，Bombardier 2.385 → 2.543 µs。方向仍有短请求差距，不能从 10 字段改善推断真实短 GET 已恢复。其它目标的数值及 A/A 稳定性见 `bin/s1f-old-real/micro.json`；其中不稳定项保持未定。

## 内存管理器诊断：释放长尾，而不只是复制字节

普通权限及一次已获批准的沙箱外 WPR 重试均返回 `0xc5585011`，没有采到 trace，也没有修改系统策略。随后用隔离 MM 包装器记录原始分配器调用区间，采用静态线程槽、固定直方图；hook 内不申请内存、不格式化、不写日志、不加全局锁。

该诊断为 Delphi Win64 默认 RTL、A/D 各一次 10 秒真实 Bombardier，不用于正式吞吐验收。每次成功响应归一化：

| 操作 | A 调用次数 | D 调用次数 | A 累计 MM wall µs | D 累计 MM wall µs |
| --- | ---: | ---: | ---: | ---: |
| GetMem | 74.0114 | 76.0119 | 60.668 | 72.944 |
| ReallocMem | 6.0000 | 6.0000 | 34.462 | 35.426 |
| FreeMem | 74.0136 | 76.0142 | 2588.751 | 2694.339 |

净增约两次申请和释放，与真实两字段新增自有 raw 的方向一致。约 96% 的 MM 调用累计 wall 时间在 FreeMem；全部 65 个 I/O 线程均出现大于 5 ms 的 FreeMem，长尾主要集中在 10～20 ms。A 旧版也存在这些长尾。

**这里是各线程调用区间的累计 wall 时间，可重叠，不是进程 wall 时间或 CPU 时间。** RTL 中确有 Sleep(0)/Sleep(10) 等待路径，观察与之相容；没有调用栈，不能把等待/抢占直接定论为分配器锁，更不能宣称新改动造成了全部长尾。分配请求尺寸桶不是实际堆块大小，释放尺寸未知，没有读取私有堆头。

Win64 的请求尺寸需要与 Win32 区分：D13.1 `StrRec` 含 padding 共 16 B，UnicodeString 请求量为 `16 + 2 × (字符数 + 1)`；非空 TBytes 为 `16 + 字节数`。因此 GET=24 B、Host 名称=26 B、User-Agent 名称=38 B、`/`=20 B。Host 不是 22 B，去掉 Host 名称不能声称减少 `(16,24]` 桶；该名称落 `(24,32]`，User-Agent 名称落 `(32,48]`。这些仍是调用方请求字节，不是 RTL 实际分配档。

A/D 的 `(16,24]` 桶约 15→17 次/成功响应，与两处源码差异相容：`fasthttp` 值的 raw 为 8+16=24 B；无 query 的 `/` 当前额外一次 GetString 为 20 B，而旧 A 共享已取出的路径字符串。桶统计没有调用栈，不能把这些解释当成唯一归因。旧 noquery 候选已经被独立整机筛查否决，本轮不重启、不以该推测覆盖其失败证据。方法 GET 的独立子串和 ToUpper 旧 A 已有，K/K2只能称起始行解析内部补偿。

证据：`bin/s1-followup-unwind/diagnostics/mm-ad-01/MM_DIAGNOSTIC_RESULTS.md` 及 CSV/JSON、源/二进制哈希、退出证明。

## 无 hook 的 A/E 整机筛查仍失败

沿用正式示例、Delphi Win64 Release、RTL、128 并发、65 I/O 线程、210 B 响应、默认 backlog、头体分离。固定 A/A 3 对、A/E 5 对；ab 2 万预热后 10 万计时，Bombardier 每次 15 秒，没有中途追加轮数。

| 指标 | A 点中位 | E 点中位 | 配对变化 | 本批判定 |
| --- | ---: | ---: | ---: | --- |
| ab RPS | 29952.44 | 26348.33 | -11.87% | 筛查回退 |
| Bombardier RPS | 28094.07 | 26038.28 | -6.20% | A/A +1.0407% 超门槛，未定 |
| Bombardier P99 ms | 45.895 | 46.872 | +2.13% | 筛查回退 |

两种吞吐及 P99 均为 5/5 对坏向；CPU 指标未定。16 个完整服务会话全部零错误、正确字节数，64 个服务器/客户端进程正常退出。已从原始日志独立复算配对、bootstrap 与计数，源和工具哈希无漂移。

这仍是 5 对筛查，不代替正式整阶段验收。详细区间、A/A 每对波动与日志在 `bin/s1f-real-http/SCREENING_RESULTS.md`。这个失败结果保留，并据此继续下一候选。

## 第四项候选：复用常见字段名常量

仅字节 Decode 对库中已有的全部 44 个 `HEADER_*` 常量按长度筛选，并逐字节作**大小写完全一致**的匹配。命中时直接引用不可变字符串常量；未命中仍按原方式扩宽。无动态缓存或全局锁，不规范化名称大小写，不改变任何值及 raw 的所有权。

该候选减少名称字符串申请。旧 A 也有这项名称成本，因此应称为在 Header 解析内补偿新增 raw 成本，不能声称消除了 raw 复制，也不能只为两个压测字段编写特例。

F 的三目标 270 个语义组合通过，但 Delphi 的申请次数完全没有减少：小头仍为 13 次，真实 Bombardier 请求对象仍为 34 次；FPC 才减少。Delphi `System._UStrAsg` 对负引用计数的字符串字面量执行复制，局部赋值 `_UStrLAsg` 的行为不同。名称最终赋入 record 时不能依赖局部变量的规则。F 部分输入还增加匹配成本，未采纳。

G 因此只在单元初始化时，为 Delphi 的 44 个名称各用公开 `SetString` 创建一次普通托管字符串，运行期仅引用；FPC 保留 literal 规则。没有手改 RTL 引用计数或读取私有字符串头。现在三目标小头的申请次数都从 E 的 13 次降到 9 次。代价是 Delphi 额外持有固定的 44 个名称，并产生共享字符串引用计数活动。精确大小写未命中的名称仍需自行分配，此方案不是所有名称都免分配。

H 进一步返回固定 string 变量槽的地址；未命中时指向本次调用的 LName。控制分类只读该字符串，最终仅赋入 record 一次。这个指针不指向 TList 后备数组，不跨调用保存；字符串 Decode 不变。G/H 独立筛查有正有负，不能说 H 在所有微基准都优于 G；例如 FPC 字符串小头约 0.419 → 0.437 µs，也如实保留。

G 与 H 各通过三目标 270 个基准语义组合，并在 Win64/Win32/FPC Win64 的 Release/Checked 六目标中分别通过每目标 **11348 项**名称测试。覆盖全部 44 个名称、大小写变体、同长度近似、长短边界、重复项、输入缓冲覆写、记录外部修改和写时复制。具体数据分别在 `bin/s1f-name-constants`、`bin/s1f-name-shared`、`bin/s1f-name-direct`。

### A/H 未恢复、E/H 有收益但 CPU 未定的权衡

两个批次各自固定参考自对照 3 对、候选 5 对，Bombardier 15 秒，ab 原预热/计数；均 16 个零错误服务会话、64 个进程正常退出。所有统计从同批原始日志复算，不能跨批相减。

| 同批比较 | 指标 | 配对变化 | A/A偏差 | 判定/边界 |
| --- | --- | ---: | ---: | --- |
| A/H | Bombardier 吞吐 | -2.964675% | +0.916824% | 5/5 H更慢，筛查回退 |
| A/H | ab CPU/成功响应 | +6.990881% | 0% | 5/5 H更差，筛查回退 |
| A/H | ab 吞吐 | -7.913506% | -2.540248% | A/A不稳且区间跨1，未定 |
| A/H | P99 / Bombardier CPU | 中位0% / -0.312891% | 分别保留原记录 | 区间跨1，未定 |
| E/H | Bombardier 吞吐 | +2.561535% | +0.887840% | 5/5 H更快，坏向上界0.980091，符合本批改善 |
| E/H | P99 | -17.538942% | -0.002133% | 5/5 H更低，但A/A范围0.839328～1.212742很宽 |
| E/H | ab 吞吐 | +6.193594% | +1.040494% | A/A略超1%，仍未定 |
| E/H | ab CPU/成功响应 | +3.273810% | -1.25% | 4/5 H更差，A/A不稳且区间跨1，未定 |
| E/H | Bombardier CPU/成功响应 | +2.369302% | -1.671848% | 4/5 H更差，A/A不稳且区间跨1，未定 |

E/H满足事先约定的“至少一项吞吐稳定收益且没有稳定CPU/P99退化，才可考虑H”条件；**未定不等于CPU无退化已获证明**。当前 J 没有纳入 F/G/H 的固定名称表及共享引用计数活动，保留这组收益/复杂度权衡和完整证据，不把未采纳改写为“所有负载无收益”。A/H未恢复到A的失败结论也不改变。

证据：[A/H报告](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-name-http/SCREENING_RESULTS.md)、[E/H报告](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-name-eh-http/SCREENING_RESULTS.md)，各目录均有 summary.json、原始16会话、冻结哈希、独立复核和退出证明。

## I2：字节校验批量读取，保留反向结果

[选定扫描器](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-followup-unwind/selected-j/source/Net/Net.CrossHttpParams.pas:1659) 用 Byte 集合替换名称 token 的重复字符判断；值区间明确证明剩余至少8字节后才 packed 读取 UInt64。两个逐字节掩码判断排除CTL/DEL，遇CR/HTAB等特殊块回到原标量路径。移位/掩码没有跨字节进位或依赖关闭溢出检查，不改变raw、GetString或数据结构。I2修正了FPC在R+下对高位十六进制常量先按负Int64处理的问题；旧I证据保留。

三目标Windows Release/Checked六个组合，专项各 **86603** 检查通过；其中Checked副本显式启用完整布尔求值B+，并由编译命令启用Q+/R+。包含所有字节在不同位置、短尾、非对齐和保护页边界；不能把这六个Windows目标写成Linux/ARM运行证明。[专项证据](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-scan-word-v2/scan-tests)；另有270/270基准语义预检。

E/I2同批为21个组合，每项7对、A/A5对、250ms；结果18 not_slower、2未定、1回退。下表r=I2/E；两侧耗时为各自样本中位，r为逐对比值中位，不能相除替代。

| 目标 | 模式/输入 | E µs | I2 µs | 配对 r | 单侧95%上界 | A/A偏差 | 本批判定 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| win64 | wire/small | 0.869743 | 0.834459 | 0.958215 | 0.968461 | -1.0718% | 未定 |
| win64 | wire/ascii | 2.792168 | 2.447568 | 0.877301 | 0.887161 | -0.1831% | not_slower |
| win64 | wire/many | 8.910818 | 8.251331 | 0.912989 | 0.929789 | +0.0857% | not_slower |
| win64 | wire/cookie | 8.662166 | 4.291320 | 0.496296 | 0.497558 | +0.0755% | not_slower |
| win64 | string/small | 0.547899 | 0.555791 | 1.015002 | 1.027385 | -0.5818% | 回退 |
| win64 | new-request/ab | 3.132362 | 3.056197 | 0.979164 | 0.982080 | -0.2024% | not_slower |
| win64 | new-request/bombardier | 2.520595 | 2.466536 | 0.976612 | 0.979018 | +0.2262% | not_slower |
| win32 | wire/small | 0.706470 | 0.684604 | 0.969466 | 0.972258 | +0.0196% | not_slower |
| win32 | wire/ascii | 2.500242 | 2.190288 | 0.877575 | 0.885775 | -0.2934% | not_slower |
| win32 | wire/many | 8.335660 | 8.138291 | 0.973772 | 1.005808 | -0.1924% | 未定 |
| win32 | wire/cookie | 10.735965 | 7.019049 | 0.653434 | 0.656619 | +0.7110% | not_slower |
| win32 | string/small | 0.432550 | 0.425361 | 0.981668 | 0.989764 | -0.0053% | not_slower |
| win32 | new-request/ab | 2.716875 | 2.715168 | 0.996867 | 0.999929 | +0.5821% | not_slower |
| win32 | new-request/bombardier | 2.236824 | 2.210072 | 0.989802 | 0.991834 | +0.7725% | not_slower |
| fpc64 | wire/small | 0.646659 | 0.582397 | 0.900622 | 0.907633 | +0.1504% | not_slower |
| fpc64 | wire/ascii | 2.693934 | 2.033660 | 0.752379 | 0.758892 | +0.1458% | not_slower |
| fpc64 | wire/many | 7.921789 | 6.504916 | 0.821526 | 0.825852 | +0.4016% | not_slower |
| fpc64 | wire/cookie | 15.923974 | 7.828303 | 0.491605 | 0.493176 | +0.0348% | not_slower |
| fpc64 | string/small | 0.429286 | 0.416975 | 0.970920 | 0.986056 | +0.6820% | not_slower |
| fpc64 | new-request/ab | 6.842235 | 6.802477 | 0.993778 | 0.999320 | -0.3986% | not_slower |
| fpc64 | new-request/bombardier | 4.267187 | 4.200062 | 0.987022 | 0.994660 | +0.0286% | not_slower |

Win64 `string/small` 配对 **+1.500151%**，单侧下界1.002934、上界1.027385且A/A稳定，是本批真实反向，不能用Cookie或字节路径收益抵消。Win64 `wire/small` 虽方向约-4.18%，A/A偏差-1.0718%超门槛，仍未定；Win32 `wire/many` 区间跨1也未定。21项的分配次数和累计申请字节均未变化，收益不能称为减少堆分配。最终J对A仍须复核这些项。

证据：[I2 micro.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-scan-word-v2/micro.json)、[设置](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-scan-word-v2/micro-settings.json)、[语义预检](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-scan-word-v2/validation.json).

## K 与 K2：仅 Delphi 避免规范起始行的额外 ToUpper

保留GetString，只对解码后精确等于9个常见大写方法或HTTP/1.0、HTTP/1.1的文本返回已有Result；其余包括小写、混合大小写、非ASCII、空串仍调用原平台/区域ToUpper。只比较，不赋同值literal、不建共享表、不改变方法接受范围。未采用“所有非小写ASCII恒等”的宽假设。旧A也有方法子串和ToUpper成本，本项属于起始行内部补偿。

K的Windows三目标Release/Checked六个组合各 **723 cases / 4288 checks** 通过，真实Request.ParseHeader对照原GetString＋ToUpper oracle，覆盖BOM、原始0..255、大小写、非ASCII和raw输入覆写。该结果不等于已运行全部有效locale；累计版Linux现已实际运行通过，见末尾复核记录。基准另有258/258语义预检。

E/K的“15项”是三目标×5输入组合，每项仍为7配对、A/A5、250ms，不是15配对。Delphi 10项A/A均稳定且not_slower；FPC一项回退、四项未定：

| 目标 | 模式/输入 | E µs | K µs | 配对 r | 单侧95%上界 | A/A偏差 | 本批判定 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| win64 | new-request/small | 4.574934 | 4.419439 | 0.965630 | 0.966635 | -0.5002% | not_slower |
| win64 | new-request/ascii | 5.925818 | 5.800258 | 0.978216 | 0.984674 | +0.7590% | not_slower |
| win64 | new-request/ab | 3.123010 | 2.972949 | 0.952148 | 0.955189 | -0.5437% | not_slower |
| win64 | new-request/bombardier | 2.515892 | 2.367478 | 0.940752 | 0.956299 | +0.0973% | not_slower |
| win64 | request/original | 5.045423 | 4.946995 | 0.977717 | 0.983655 | -0.7163% | not_slower |
| win32 | new-request/small | 4.074687 | 3.940114 | 0.971816 | 0.986888 | -0.1674% | not_slower |
| win32 | new-request/ascii | 5.308659 | 5.147809 | 0.969700 | 0.974658 | +0.6960% | not_slower |
| win32 | new-request/ab | 2.737447 | 2.595512 | 0.948011 | 0.962838 | -0.2556% | not_slower |
| win32 | new-request/bombardier | 2.229725 | 2.030497 | 0.915729 | 0.918496 | -0.0062% | not_slower |
| win32 | request/original | 4.533777 | 4.390501 | 0.967642 | 0.970838 | -0.0259% | not_slower |
| fpc64 | new-request/small | 8.124473 | 8.147312 | 1.001836 | 1.007310 | -0.0570% | 未定 |
| fpc64 | new-request/ascii | 14.327331 | 14.450580 | 1.006617 | 1.013859 | -0.2136% | 回退 |
| fpc64 | new-request/ab | 6.864977 | 6.860050 | 0.997668 | 1.004439 | +0.2148% | 未定 |
| fpc64 | new-request/bombardier | 4.242609 | 4.213613 | 0.992976 | 1.003912 | +1.0604% | 未定 |
| fpc64 | request/original | 13.517740 | 13.588289 | 1.005219 | 1.015021 | +0.3976% | 未定 |

真实Bombardier报文的new-request配对耗时：Win64下降 **5.924759%**、Win32下降 **8.427110%**。独立计数每项每侧10000操作，Delphi 10项均少2次申请；该报文Win64为 **34→32次、1777→1719 B**，Win32为 **34→32次、1117→1067 B**。FPC该项为 **38→38次、1773→1773 B**，全部5项均无分配减少。

FPC 10字段 `new-request/ascii` 配对 **+0.661689%**，单侧下界1.002246、上界1.013859、A/A稳定。因此采纳的是 **K2**：helper与Method/Version两调用均受`{$IFDEF DELPHI}`保护，FPC保留原两行GetString(...).ToUpper，不承担已证实没有分配收益的白名单扫描。静态条件展开已核对Delphi等于K、FPC等于E；累计J完整回归随后验证该组合。

证据：[K micro.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-startline-case/micro.json)、[专项记录](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-startline-case/case-tests)、[K2完整性记录](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-followup-unwind/startline-case-delphi/candidate-integrity.json).

## L：Header.Encode 去掉中间托管引用

[Encode实现](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-followup-unwind/selected-j/source/Net/Net.CrossHttpParams.pas:1969) 直接通过const参数读取既有只读元素视图的Name/Value，去掉仅用于中转的LName/LValue。借用期间没有容器变更或用户回调；名称/值/UTF16错误顺序、原字符串追加、最终CRLF和发送编码均不变。这不是定长builder，也不改变头体发送机制。

E/L是独立批次，不叠加I2/K2来冒充单项收益。三个目标×6个encode/server-encode输入，共 **18个定向组合**，每项7对、A/A5、250ms，全部A/A稳定并为not_slower；语义预检258/258。各项申请次数和累计申请字节全部相同，收益来自减少中间引用传递，不称减少分配。

| 目标 | 模式/输入 | E µs | L µs | 配对 r | 单侧95%上界 | A/A偏差 | 本批判定 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| win64 | encode/small | 0.345820 | 0.328432 | 0.950078 | 0.960694 | -0.3558% | not_slower |
| win64 | encode/ascii | 1.520798 | 1.481053 | 0.973962 | 0.981198 | +0.0535% | not_slower |
| win64 | encode/many | 4.320303 | 4.155484 | 0.955590 | 0.966543 | -0.4821% | not_slower |
| win64 | server-encode/small | 1.132542 | 1.112202 | 0.979146 | 0.987752 | +0.0334% | not_slower |
| win64 | server-encode/ascii | 2.275009 | 2.237176 | 0.982504 | 0.994383 | -0.5598% | not_slower |
| win64 | server-encode/many | 5.747970 | 5.585071 | 0.975109 | 0.976337 | +0.0964% | not_slower |
| win32 | encode/small | 0.309656 | 0.293040 | 0.942875 | 0.949763 | -0.0929% | not_slower |
| win32 | encode/ascii | 1.295178 | 1.238402 | 0.956018 | 0.960844 | -0.7115% | not_slower |
| win32 | encode/many | 3.475719 | 3.302266 | 0.949166 | 0.952560 | -0.0067% | not_slower |
| win32 | server-encode/small | 1.027230 | 0.999140 | 0.973802 | 0.982662 | -0.1629% | not_slower |
| win32 | server-encode/ascii | 1.903796 | 1.860197 | 0.980308 | 0.984942 | +0.3769% | not_slower |
| win32 | server-encode/many | 4.799973 | 4.610430 | 0.958916 | 0.970722 | -0.0743% | not_slower |
| fpc64 | encode/small | 0.327929 | 0.304263 | 0.928459 | 0.931762 | +0.6559% | not_slower |
| fpc64 | encode/ascii | 1.644978 | 1.623468 | 0.985188 | 0.989541 | +0.1078% | not_slower |
| fpc64 | encode/many | 4.594149 | 4.503963 | 0.980757 | 0.981553 | +0.4364% | not_slower |
| fpc64 | server-encode/small | 3.941652 | 3.915732 | 0.994906 | 0.999982 | +0.1236% | not_slower |
| fpc64 | server-encode/ascii | 5.841949 | 5.799484 | 0.992991 | 0.997592 | +0.7720% | not_slower |
| fpc64 | server-encode/many | 16.894657 | 16.778491 | 0.992584 | 0.995900 | -0.0696% | not_slower |

FPC `server-encode/small` 的单侧上界为 **0.999981684**，虽然满足本批判定，但非常接近1，不描述成宽裕通过。证据：[L micro.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-encode-direct/micro.json)、[设置](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-encode-direct/micro-settings.json)、[语义预检](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-encode-direct/validation.json).

## 已完成的正确性验证

- E：Windows Delphi Win64、Win32、FPC Win64，Release/Checked 各目标均 1890 项检查和 22 个真实 TCP 场景通过。
- E：Linux 交叉编译后在 WSL Ubuntu 22.04 **实际运行**；Delphi 1597 项、FPC 1853 项，另各 22 个 TCP 场景通过。GetString 的平台转换异常差异按原行为检验，没有统一替换。
- E：FPC 同单元隔离生命周期测试，Release/Checked 各 563 项通过。覆盖实际子类构造、基类引用 Clear/Assign(nil)、Decode 清空、析构、容量释放、外部 raw 引用、通知内容/顺序/次数。生产单元未增加测试 API。
- 各微基准先验证语义；旧 A 输出错误的 Unicode 编码不作为有效耗时基线。具体矩阵和候选失败记录全部保留。

尚不对未运行的平台、旧版编译器或长期真实业务负载作通过声明。没有提交或推送；原有暂存改动保留。


## 当前累计选定 J 与剩余工作

J组合E的正常退出/冷回滚/FPC Header清理、I2批量校验、K2仅Delphi起始行白名单和L Encode去临时引用。共享名称表F/G/H不在主；M名称四字节扩宽已验证但未采纳，不计入J收益。

当前主生产源与 `selected-j/source` 的68个文件逐个hash相同；其中：

| 冻结文件 | SHA-256 |
| --- | --- |
| Net.CrossHttpParams.pas | `af31f5fc34c6bce5e6b9947522c5de536e037b4439263bbfc47c58c54d46bb18` |
| Net.CrossHttpServer.pas | `751bd15c43736040d86c7eff90dd2043b1294e9119587a01548a281986c2bd71` |

Windows完整J已逐份核对 `s1f-j-release`、`s1f-j-checked` 三目标的results.json/component.log/build.json。各目标均为组件 **92781** 检查（1890既有＋86603扫描＋4288起始行）和 **22个真实TCP场景**，预期400拒绝场景也是通过项，不能把400当压测错误：

| J目标 | Release组件 / TCP | Checked组件 / TCP |
| --- | --- | --- |
| Delphi Win64 | 92781 / 22 通过 | 92781 / 22 通过 |
| Delphi Win32 | 92781 / 22 通过 | 92781 / 22 通过 |
| FPC Win64 | 92781 / 22 通过 | 92781 / 22 通过 |

证据：[J Release](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-j-release)、[J Checked](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-j-checked). 这是累计J的Windows功能回归，不把各独立候选的微基准组合成已完成的累计性能结论。

| 项目 | 当前状态 | 后续 |
| --- | --- | --- |
| E/I2/K2/L采纳与Windows累计回归 | 已完成当前验证 | 新变更只按影响复验 |
| 最终A/J关键性能复核 | 31对：10通过、2回退 | 全部A/A稳定；有明确失败，未扩成全129组合验收 |
| 累计版Linux复核 | 实际运行通过 | Delphi44650/FPC92266组件检查，另各22个TCP |
| 共享名称表F/G/H | 未纳入J | 保留A/H失败及E/H收益/CPU未定权衡 |
| M名称四字节扩宽 | 已测，未采纳 | 关键小头和FPC真实短请求反向，保持J |
| 第一阶段严格恢复 | 尚未完成 | 不以局部not_slower或功能通过提前完成 |

未提交、未推送，旧暂存及不相关改动保留。

## 最终累计 J 对旧 A：固定 31 对确认

每组合固定 31 对、500 ms 窗口、A/A 7 对，亲和掩码4、默认分配器、seed199及5000次bootstrap。12个组合在运行前固定；正值表示耗时增加。百分比来自逐对比值的中位数，不等于两个点中位数之比。

| 目标/组合 | A中位 µs | J中位 µs | 配对耗时变化 | 单侧95%下界 | 单侧95%上界 | A/A中位偏差 | 判定 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| win64 wire/small | 0.816690 | 0.878566 | +7.5238% | 1.072728 | 1.078642 | +0.2580% | 回退 |
| win64 wire/ascii | 3.299509 | 2.597964 | -21.5188% | 0.781835 | 0.788563 | +0.0716% | 通过 |
| win64 wire/cookie | 22.883297 | 4.395027 | -80.8006% | 0.191411 | 0.192245 | -0.1366% | 通过 |
| win64 new-request/bombardier | 2.387813 | 2.374957 | -0.8985% | 0.988065 | 0.996528 | +0.0459% | 通过 |
| win64 server-encode/ascii | 2.228809 | 2.243157 | +0.2039% | 1.000287 | 1.005273 | +0.0391% | 回退 |
| win32 wire/small | 0.739190 | 0.686207 | -6.8583% | 0.923271 | 0.937000 | -0.2863% | 通过 |
| win32 server-encode/ascii | 1.939391 | 1.841332 | -4.9008% | 0.947111 | 0.951859 | -0.1901% | 通过 |
| win32 new-request/bombardier | 2.174445 | 2.101391 | -3.5052% | 0.961876 | 0.967510 | +0.5089% | 通过 |
| fpc64 wire/small | 0.757131 | 0.571657 | -24.6224% | 0.753144 | 0.755212 | +0.1291% | 通过 |
| fpc64 wire/ascii | 3.034453 | 2.080377 | -31.4262% | 0.684333 | 0.686537 | +0.0999% | 通过 |
| fpc64 string/small | 0.687991 | 0.416390 | -39.4852% | 0.603260 | 0.607225 | +0.0412% | 通过 |
| fpc64 new-request/bombardier | 5.558169 | 4.178997 | -24.8569% | 0.750461 | 0.752465 | -0.0893% | 通过 |

**全部12项A/A稳定，10项通过、2项回退；0%预算没有放宽。** 两项回退是Win64 4字段字节解析和真实服务端ASCII头生成。后者虽只约+0.20%，仍按原规则列为回退。原先最突出的FPC小头及Win32服务端编码失败项已追回，但不能以它们抵销剩余反例，也不把关键12项当成完整129项。

基准先做270个角色/目标/模式的语义核对，J的135个组合全部有效；旧A有26个组合不符合当前Unicode输出合同，相关错误输出不作为有效性能基线。证据：[累计微基准](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-final-j/micro-combined.json)、[预设矩阵](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-final-j/final-micro-settings.json)、[语义预检](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-final-j/validation.json)。

## 累计 J 的 Linux 实际运行及未采纳的 M

J从当前源交叉编译后，在WSL Ubuntu22.04实际运行：Delphi Linux64 **44650项组件检查＋22个TCP**，FPC Linux64 **92266项组件检查＋22个TCP**，全部通过。不同检查数来自各平台原GetString转换结果/异常以及Windows专有保护页用例，使用原函数作为oracle验证，没有统一修改转换行为。证据：[Linux J](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-j-linux)。ARM/aarch64、大端、macOS/BSD及旧编译器未实测。

M另有六目标各14049专项检查和270个语义组合通过，但J/M固定7对筛查中关键小头及FPC真实短请求存在反向，因此不合入。部分长头变快不能替代目标输入达标；CPU32没有启用块扩宽，不把其数值变化归因为该64位算法。原始记录在 `bin/s1f-name-widen`；主仍与selected-j完全一致。

## 最终累计 J 的真实 HTTP 筛查

RTL与FastMM5分开固定执行，各A/A3对、A/J5对；Bombardier每会话15秒，ab先2万预热再测10万，128并发、65个I/O线程、210B响应、63B真实GET、默认backlog、头体分离均保持。RPS变化表示J相对A吞吐变化；P99/CPU为相应指标变化。

| 分配器/指标 | 配对变化 | 坏向比值单侧95%下界 | 上界 | A/A中位偏差 | 判定 |
| --- | ---: | ---: | ---: | ---: | --- |
| rtl ab_rps | -7.3430% | 1.064048 | 1.098540 | -1.3320% | 未定 |
| rtl bomb_rps | -1.4769% | 1.010048 | 1.048033 | -0.0068% | 回退 |
| rtl bomb_p99_us | +0.0022% | 0.999956 | 1.000044 | +0.0000% | 未定 |
| rtl ab_cpu_us_per_response | -1.5576% | 0.959184 | 1.027356 | +0.3086% | 未定 |
| rtl bomb_server_cpu_us_per_response | -0.8261% | 0.988652 | 1.027395 | -0.6311% | 未定 |
| fastmm5 ab_rps | +0.4903% | 0.936892 | 1.007007 | +0.4034% | 未定 |
| fastmm5 bomb_rps | -0.9585% | 0.996609 | 1.024807 | +0.2438% | 未定 |
| fastmm5 bomb_p99_us | +0.0512% | 0.999488 | 1.024041 | -0.0512% | 未定 |
| fastmm5 ab_cpu_us_per_response | +2.2500% | 0.925337 | 1.056225 | +2.2500% | 未定 |
| fastmm5 bomb_server_cpu_us_per_response | +0.9266% | 0.948462 | 1.017824 | -0.2019% | 未定 |

**RTL Bombardier吞吐配对下降1.476908%，5/5对更慢，A/A稳定，区间位于坏侧。** RTL的ab也是5/5更慢，但A/A偏差超门槛，保持未定；P99和CPU亦未定。FastMM5五个指标的区间均跨1，全部未定，不能把接近旧版或一个点中位更好标为恢复。

本批共32个服务会话和128条服务器/客户端进程记录，全部返回0、无强制结束。32份计时ab、32份Bombardier及32个预检响应全部零错误、字节正确；Bombardier共30,300,624个2xx。冻结源、origin源、编译覆盖单元、harness、模板、工具和FastMM原件/副本hash全部通过审计。所有配对、CPU归一及bootstrap都从raw独立复算；最终相关进程与18080监听为空。

证据：[最终HTTP汇总](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-followup-unwind/diagnostics/FINAL_J_HTTP_RESULTS.md)、[RTL分组](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-final-j-http-rtl/SCREENING_RESULTS.md)、[FastMM5分组](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1f-final-j-http-fastmm5/SCREENING_RESULTS.md)。这是固定5对筛查，不能与历史15对A/C结果跨批相减推断改进幅度，也不冒充完整验收。

## 本轮结论与剩余优先级

本轮保留六项实现：FPC成功路径正常退出、失败临时记录隔离、Header无通知清理、Byte集合/8字节值校验、Delphi规范起始行省略重复ToUpper、Encode直接const借用字段。正确性回归与原字节/所有权合同通过，源码已在工作区。名称共享表和四字节名称扩宽没有合入；第二阶段未启动。

1. **Win64短头固定成本仍是明确反例。** 最终4字段纯解析仍多7.5238%；单次申请A为10、J为13。后续应区分每字段raw建立、字符集检测/转换及列表操作的固定成本；原始数据保存合同保持。
2. **出站Win64仍有约0.2039%的严格回退。** 这是31对按零预算得到的结论，量级小也不改门槛；后续须分别计量字段校验、拼接及最终编码，保留异常与UTF-8规则。
3. **真实RTL优先定位分配/释放分布。** Delphi Win64真实两字段请求的新建/解析/释放微基准中，A/J申请次数均为32，整机仍有回退，说明次数相等不足以保证恢复。FreeMem长尾与尺寸桶证据值得继续做调用阶段/释放线程归因；现有证据不足以把全部长尾认定为锁争用。FastMM5未定不能替代默认RTL通过。

当前状态是“本轮优化与验证已完成，第一阶段严格恢复未完成”。没有提交或推送。最后完整性检查确认：主生产68文件与冻结J相同，GetString及稳定两个runner未变，最初55767字节暂存patch逐字节一致，修改的Pascal文件均为UTF-8 BOM/CRLF，git diff --check通过。见 [完整性记录](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-followup-unwind/final-integrity.json)。
