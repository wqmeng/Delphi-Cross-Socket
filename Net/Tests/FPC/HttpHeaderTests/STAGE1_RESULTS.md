# #199 第一阶段优化结果

日期：2026-10-28。状态：已实施所选优化，严格性能恢复验收未通过，阶段二未启动。生产选定 C 已冻结，固定验收和额外候选筛查已结束，未提交、未推送。

第一阶段已实施选定优化并通过当前已运行的正确性回归，但“性能至少恢复至 #199 前”的严格验收未通过。独立确认的 27 个 A/C 组合中，19 个满足本批判定、4 个稳定回退、4 个因 A/A 不稳定而未定。不能用完整请求或长 Cookie 的改善抵消小头、特定编译器或真实发送路径的回退。C 不能作为已经验收完成的阶段二基线。

本文依据[两阶段计划](D:/GitHub/Delphi-Cross-Socket/docs/plans/2026-10-28-http-performance-two-stage-plan.md)和本次冻结源码、原始 JSON；本轮结果没有降低 0% 退化预算。未达到目标的状态如实保留。

## 1. 版本、范围与测量口径

| 标记 | 源码 | 用途 |
| --- | --- | --- |
| A | `566187c283ebb3105e57ac9c8dd148a952b014d1`；`bin/s1-199-20261028/before/source` | #199 前的恢复目标 |
| B | 实施前工作区 #199 版本；`bin/s1-199-20261028/after/source` | 已确认编码、原始字节和严格检查合同 |
| C | `bin/s1-199-20261028/selected-c/source` | 仅第一阶段所选优化的冻结候选 |

冻结目录均位于 [HttpHeaderTests/bin](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin)。每份保留 68 个生产源及 manifest；单批另保存源码、harness、fixture、编译参数、生成路径及 SHA-256。最终性能只使用各自同批的比较，不将历史绝对值拼成恢复结论。

第一阶段生产改动限于 `Net.CrossHttpParams.pas`、`Net.CrossHttpUtils.pas`、`Net.CrossHttpServer.pas`、`Net.CrossHttpClient.pas`。保留 `TParamEntry` 布局和公开构造器、`THttpHeader: TBaseParams`、ICross 接口/IID、`TUtils.GetString` 的 BOM/UTF-8/默认编码与异常语义、自有 raw、空 raw 与无来源的区别、原始字节控制校验、失败回滚、头体分离、默认 `SOMAXCONN`、eager 容器及原 Add 容量增长策略。

编译日志确认 Delphi 工具链来自 `D13.1`，Win64 compiler version 37.0；FPC 为 `3.3.1-21438-g20e18660ec-dirty [2026/10/22]`。Release 微基准 Delphi 使用 `O+`、`C-/I-/Q-/R-`，FPC 使用 `-O3 -CX -Xs -XX`；Checked 回归在对应配置启用 Delphi `C+/I+/Q+/R+`、FPC `-Cr -Co -Ci -Sa`。每个目标完整命令以各批 `build.json` 为准，未将 Checked 耗时混入 Release 性能。

微基准使用各编译器默认分配器，固定 CPU affinity mask=4。广度批次为 43 个组合×3 目标，各固定 7 对、A/A 5 对、250 ms；独立关键确认另建 label，各固定 31 对、A/A 5 对、1000 ms。Bootstrap seed=199、5000 次按完整配对重采样。时间比值 `r=C/R`，R 为 A 或合同参考 B；`r>1` 为更慢。达到目标要求配对中位 `r≤1`、单侧 95% 上界 `≤1`，且 A/A 稳定。A/A 中位偏差超出 1% 的组合保留未定，即使其点估计改善。

表中的 A/C 或 B/C 耗时是各自样本中位数，单位 µs/次；`r` 是逐对比值的中位数，不能由表内两个中位数相除来替代。单侧上下界衡量运行间不确定性，不表示绝对零误差。合成 Header 编码与实际 `_CreateHeader`/`_CreateRequestHeader` 路径分开；本轮驱动根据冻结源码选用实际 ASCII/full-UTF8/prefix-UTF8/safe-UTF8 路径，旧手工额外检查仅保留为补充 `encode-legacy`，没有计入默认 all。

## 2. 实施项与候选处置

| 计划项 | 选定处理 | 位置/证据与边界 |
| --- | --- | --- |
| S1-01 控制字段分类 | 采用私有 ASCII 分类，减少重复 Unicode 比较 | 保留 raw 校验及控制字段类别；不改变普通字段解码 |
| S1-02 新扫描器与 Name | 采用 byte/Char 分型扫描、显式边界/OWS、独立 Name 写入 | [Params:1690](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1690)；不引入越界宽读 |
| S1-03 托管记录读取 | 采用公开只读元素视图：D11+ `PList`，FPC `List`，旧 Delphi 保留公开 List 入口 | [Params:968](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:968)；不强转私有 RTL 布局；视图期间无容器变更/用户回调 |
| S1-04 无 query 重复转换 | 未采纳，额外独立筛查后否决 | 局部候选出现 Win64 反向；独立 RTL 筛查五指标均未定，按预定规则否决，不追加复测；选定 C 保留原解析路径 |
| S1-05 输出校验与编码 | 采用字段合法性/UTF-16 配对融合，新增安全 `EncodeHeader(string): TBytes`，全 ASCII 使用等价无 BOM UTF-8，非 ASCII 走标准 UTF-8 | [Params:1874](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1874)、[Utils:2096](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpUtils.pas:2096)、[Utils:2137](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpUtils.pas:2137)、[Server:5791](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpServer.pas:5791)、[Client:2732](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpClient.pas:2732)；完整块检查及 URL 编码前 UTF-16 检查保留 |
| S1-06 字节构造/搬运 | 采用私有 var 初始化及现有 GetString 指针入口；新槽直接持有 raw；仅现有容量内先增 Count，容量不足仍走原 Add | [Params:1114](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1114)、[Params:1813](D:/GitHub/Delphi-Cross-Socket/Net/Net.CrossHttpParams.pas:1813)；公开构造器强提交、异常清理和原增长策略保留 |
| S1-07 累计验收 | 正确性已过；性能严格恢复未过 | 本文逐项列示，不进入 S2 |

保留的否决证据：

- 手写值字符范围检查在 FPC Cookie/string 约有 2%～3% 反向，未采纳。
- 仅经 Delphi List getter 的借用版存在目标差异，改为公开 `PList` 适配后才采用。
- `P^.Create` 原位实例构造在 FPC 语义预检失败，未计时、未写回。精确原因是构造器写槽后，FPC 自动终结被丢弃的托管 record 结果；不能解释为“没有写入槽”。合法 var 初始化过程替代了该尝试。
- 整块 ASCII 视图在 8 KiB Cookie 约 6%～14% 回退，整体否决；未写回 C。
- 两/四码元 ASCII 输出展开跨编译器收益冲突，未采纳。NameReset 未采纳。
- 未实施 UrlDecode 快路、共享 comparer、定长 Header builder、容量保留/对象池、永久索引、Parser/路由/Socket/TLS/Session 改动；这些仍属阶段二。

探索失败及候选目录在 `bin/s1-01-*`～`s1-06-*` 保留。基准语义 Oracle 已改成独立 trusted fixture 拆行并 Add 构造预期；不再让 expected Decode 与被测解析共享实现。

## 3. 正确性、构建与平台覆盖

| 目标 | 配置 | Header 组件 | 真实 loopback TCP | 结论 |
| --- | --- | ---: | ---: | --- |
| Delphi Win64 | Release / Checked，各运行一次 | 各 1890 | 各 22 | 通过 |
| Delphi Win32 | Release / Checked，各运行一次 | 各 1890 | 各 22 | 通过 |
| FPC Win64 | Release / Checked，各运行一次 | 各 1890 | 各 22 | 通过 |
| Delphi Linux64 | Release / Checked，WSL 实际执行 | 各 1597 | 各 22 | 通过 |
| FPC Linux64 | Release / Checked，WSL 实际执行 | 各 1853 | 各 22 | 通过 |

Windows 最终组件证据为 `bin/s1-06-capacity-release`、`bin/s1-06-capacity-checked`。Linux FPC 由现有 Windows 交叉工具链编译，再由 WSL 实际执行；没有用交叉编译成功代替运行证据。各平台检查数不同，表中不合并伪装为相同覆盖。

相邻验证详见 [FINAL_VALIDATION.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-final-validation-20261028/FINAL_VALIDATION.json)：25 个相邻测试组合均通过，包括 Windows 三目标 HttpFailure 143/143、MultipartStream 21/21、HttpRequestLifetime 6/6；Windows/Linux 五目标 HEAD 14、Response 22、Interim 9；四目标 WebSocket 61 组件、11056 合法帧、11 TCP、55 握手，以及 Win32 55 握手；Delphi/FPC Win64 Chunk 各 100/100。

正式 `HttpServer`、`HttpClient`、`WebSocketServer` 三工程 Win64 Release 全量 Build 均返回 0，产物隔离输出，demo 资源保持不变。已运行覆盖 raw 所有权/快照、防御性复制、空值/重复字段、编码异常、非法 UTF-16、控制字段、失败回滚、multipart、发送失败与请求生命周期等硬门槛；最终独立只读代码/范围审查未发现实质问题。

未覆盖 macOS、ARM、旧 Delphi 版本适配分支的实际编译/运行、TLS 完整矩阵及长时间压力。本阶段实际整机性能范围仅为 Delphi Win64 固定 HTTP 场景；Win32/FPC 微基准不能代替其整机吞吐证明。

## 4. 独立关键 A/C 确认：31 对逐项结果

证据：[s1-ac-confirm-20261028/micro.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-ac-confirm-20261028/micro.json)。small=4 字段/110 B；ascii=10 字段，fixture 文件 856 B，实际解析请求头 691 B（不含 165 B 正文）；cookie=8320 B（约 8 KiB Cookie）。`wire` 为字节→字段，`request` 复用请求解析，`new-request` 新建/解析/释放，`response` 响应解析，`encode` 根据冻结源码选择的输出表达式，`server-encode` 调用真实服务端头生成路径。

### win64

| 组合 | A µs | C µs | 配对 r | 单侧95%下界 | 单侧95%上界 | A/A偏差 | 状态 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| wire/small | 0.775888 | 0.862416 | 1.108483 | 1.104840 | 1.122134 | +0.2488% | 未达标 |
| wire/ascii | 3.217069 | 2.803879 | 0.864979 | 0.863832 | 0.868931 | +0.0586% | 达到本批目标 |
| wire/cookie | 22.208213 | 8.448820 | 0.380164 | 0.379498 | 0.380984 | -0.0336% | 达到本批目标 |
| request/ascii | 5.368021 | 4.817908 | 0.897859 | 0.896273 | 0.900545 | +0.5414% | 达到本批目标 |
| new-request/ascii | 6.239934 | 5.820061 | 0.931971 | 0.928807 | 0.934728 | -0.6400% | 达到本批目标 |
| response/ascii | 4.149546 | 3.529548 | 0.849117 | 0.847132 | 0.855370 | +0.3413% | 达到本批目标 |
| encode/small | 0.367183 | 0.328428 | 0.894070 | 0.892320 | 0.895548 | -0.0022% | 达到本批目标 |
| encode/ascii | 1.401682 | 1.348240 | 0.962226 | 0.958867 | 0.965702 | +0.7168% | 达到本批目标 |
| server-encode/ascii | 2.133645 | 2.056188 | 0.962175 | 0.960542 | 0.967655 | -0.7242% | 达到本批目标 |

### win32

| 组合 | A µs | C µs | 配对 r | 单侧95%下界 | 单侧95%上界 | A/A偏差 | 状态 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| wire/small | 0.711031 | 0.689624 | 0.970713 | 0.966559 | 0.977233 | -1.4455% | 未定 |
| wire/ascii | 3.451358 | 2.387164 | 0.691189 | 0.675621 | 0.693628 | -2.8496% | 未定 |
| wire/cookie | 30.272686 | 10.360954 | 0.340771 | 0.340009 | 0.343532 | +2.4160% | 未定 |
| request/ascii | 5.425742 | 4.266908 | 0.786753 | 0.783145 | 0.788721 | +0.7901% | 达到本批目标 |
| new-request/ascii | 6.240561 | 5.096639 | 0.815113 | 0.812724 | 0.817429 | -0.2569% | 达到本批目标 |
| response/ascii | 4.336999 | 2.992872 | 0.691180 | 0.678460 | 0.696660 | -0.2606% | 达到本批目标 |
| encode/small | 0.344949 | 0.292974 | 0.849577 | 0.847611 | 0.851262 | +0.7410% | 达到本批目标 |
| encode/ascii | 1.260031 | 1.220352 | 0.969433 | 0.968084 | 0.971537 | -0.2790% | 达到本批目标 |
| server-encode/ascii | 1.846973 | 1.861317 | 1.008433 | 1.003817 | 1.013173 | +0.7978% | 未达标 |

### fpc64

| 组合 | A µs | C µs | 配对 r | 单侧95%下界 | 单侧95%上界 | A/A偏差 | 状态 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| wire/small | 0.730528 | 1.151470 | 1.576414 | 1.574090 | 1.580729 | -0.0590% | 未达标 |
| wire/ascii | 2.927455 | 3.274674 | 1.120508 | 1.117537 | 1.123088 | +0.0613% | 未达标 |
| wire/cookie | 20.165104 | 15.946549 | 0.790019 | 0.787641 | 0.792575 | -0.1872% | 达到本批目标 |
| request/ascii | 16.511984 | 13.864379 | 0.839975 | 0.838569 | 0.842250 | -0.0849% | 达到本批目标 |
| new-request/ascii | 17.282745 | 14.731288 | 0.852526 | 0.850746 | 0.853573 | +0.4093% | 达到本批目标 |
| response/ascii | 7.492555 | 7.003752 | 0.934669 | 0.931684 | 0.936841 | -0.3226% | 达到本批目标 |
| encode/small | 0.572348 | 0.322932 | 0.563838 | 0.561338 | 0.566060 | +1.3374% | 未定 |
| encode/ascii | 2.116002 | 1.631594 | 0.771638 | 0.769064 | 0.772037 | -0.1964% | 达到本批目标 |
| server-encode/ascii | 6.934996 | 5.702368 | 0.823536 | 0.821022 | 0.824995 | -0.3679% | 达到本批目标 |

四个稳定回退分别为 Delphi Win64 wire/small（配对耗时 +10.8483%）、Delphi Win32 server-encode/ascii（+0.8433%）、FPC Win64 wire/small（+57.6414%）和 wire/ascii（+12.0508%）。四个未定组合为 Win32 wire/small、wire/ascii、wire/cookie 和 FPC encode/small，均因本批 A/A 不稳定；不能直接归为通过。

完整请求、响应与长 Cookie 在多个目标上明显改善，其中独立确认通过的 19 项只说明这些组合已获得支持恢复的证据。严格第一阶段验收仍失败，不计算一个总平均将失败项抵消。

## 5. 全部广度探索：43 组合×3 目标

证据：[s1-ac-wide-20261028/micro.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-ac-wide-20261028/micro.json)。该批固定 7 对/250 ms，用于覆盖与定位，不能替代未运行组合的 31 对最终确认。以下每格为“配对 r；状态”，A 结果不等价时不提供可用于恢复的速度比较。

| 组合 | Delphi Win64 | Delphi Win32 | FPC Win64 |
| --- | --- | --- | --- |
| wire/small | 1.041645；未达标 | 0.999314；未定 | 1.581321；未达标 |
| wire/ascii | 0.861946；未定 | 0.706514；达到本批目标 | 1.132036；未达标 |
| wire/many | 0.881745；达到本批目标 | 0.782529；未定 | 1.014945；未达标 |
| wire/cookie | 0.373698；达到本批目标 | 0.338672；未定 | 0.793868；达到本批目标 |
| wire/original | A结果不等价 | A结果不等价 | 1.121572；未定 |
| string/ascii | 0.680887；达到本批目标 | 0.597664；达到本批目标 | 0.840155；达到本批目标 |
| request/small | 1.099222；未定 | 1.072701；未达标 | 0.889460；未定 |
| request/ascii | 0.889031；达到本批目标 | 0.801959；达到本批目标 | 0.837837；达到本批目标 |
| request/original | A结果不等价 | A结果不等价 | 0.837904；达到本批目标 |
| new-request/ascii | 0.913261；达到本批目标 | 0.820109；未定 | 0.860766；未定 |
| response/ascii | 0.841018；未定 | 0.733411；未定 | 0.921302；达到本批目标 |
| encode/ascii | 0.963783；达到本批目标 | 0.976375；达到本批目标 | 0.769831；达到本批目标 |
| string/small | 0.779188；达到本批目标 | 0.783345；未定 | 1.333288；未达标 |
| string/many | 0.646977；达到本批目标 | 0.624914；达到本批目标 | 0.722967；达到本批目标 |
| string/cookie | 0.472367；达到本批目标 | 0.362230；达到本批目标 | 0.540971；达到本批目标 |
| string/unicode | 0.877850；达到本批目标 | 0.837450；未定 | 1.270411；未定 |
| new-request/small | 1.094354；未达标 | 1.040976；未达标 | 0.906893；达到本批目标 |
| new-request/many | 0.905169；达到本批目标 | 0.833366；达到本批目标 | 0.817974；达到本批目标 |
| new-request/cookie | 0.543146；达到本批目标 | 0.473054；达到本批目标 | 0.832980；未定 |
| new-request/unicode | A结果不等价 | A结果不等价 | 0.898697；达到本批目标 |
| response/small | 1.065775；未定 | 1.002681；未定 | 0.959823；达到本批目标 |
| response/many | 0.866137；达到本批目标 | 0.787440；达到本批目标 | 0.882324；达到本批目标 |
| response/cookie | 0.339687；达到本批目标 | 0.304921；达到本批目标 | 0.773670；达到本批目标 |
| response/unicode | A结果不等价 | A结果不等价 | 0.933449；未定 |
| encode/small | 0.898742；未定 | 0.848161；达到本批目标 | 0.564269；未定 |
| encode/many | 0.988436；未定 | 1.001901；未定 | 0.721121；达到本批目标 |
| encode/cookie | 0.961860；达到本批目标 | 1.000184；未定 | 1.043233；未达标 |
| encode/unicode | A结果不等价 | A结果不等价 | A结果不等价 |
| server-encode/small | 0.908336；达到本批目标 | 0.998279；未定 | 0.815020；达到本批目标 |
| server-encode/many | 0.960671；达到本批目标 | 1.017426；未达标 | 0.794158；达到本批目标 |
| server-encode/cookie | 0.962534；达到本批目标 | 0.998476；未定 | 0.997068；未定 |
| server-encode/unicode | A结果不等价 | A结果不等价 | A结果不等价 |
| request/noquery | 1.062510；未达标 | 1.026493；未定 | 0.841962；达到本批目标 |
| new-request/noquery | 1.043416；未达标 | 1.011712；未定 | 0.872112；达到本批目标 |
| server-encode/ascii | 0.950112；达到本批目标 | 1.017694；未达标 | 0.819768；达到本批目标 |
| request/many | 0.873832；达到本批目标 | 0.815461；达到本批目标 | 0.810580；达到本批目标 |
| request/cookie | 0.537041；达到本批目标 | 0.460659；达到本批目标 | 0.824086；达到本批目标 |
| request/unicode | A结果不等价 | A结果不等价 | 0.887158；达到本批目标 |
| string/original | 0.687747；未定 | 0.598736；达到本批目标 | 0.834497；达到本批目标 |
| new-request/original | A结果不等价 | A结果不等价 | 0.853091；达到本批目标 |
| response/original | A结果不等价 | A结果不等价 | 0.932570；达到本批目标 |
| encode/original | A结果不等价 | A结果不等价 | A结果不等价 |
| server-encode/original | A结果不等价 | A结果不等价 | A结果不等价 |

探索状态计数：Win64 21 达到本批目标、4 未达标、7 未定、11 个 A 不等价；Win32 14/4/14/11；FPC Win64 26/5/8/4。计数仅描述这份探索批次，不覆盖独立确认结果，也不代表阶段一整体通过。原始 #199 Delphi 错误解码、旧 ASCII 输出破坏 Unicode 等不等价路径，以 B/C 合同对照另列。

## 6. A 不等价路径的 B/C 合同对照

原批次为 26 组合各 7 对：24 项达到本批目标、2 项 A/A 不稳定而未定。仅这两个未定项另进行一次事先固定的 31 对独立确认，均通过。其余 24 项仍只有 7 对证据，不能改称全部 26 项完成 31 对终验；这些 B/C 结果也不能证明恢复到旧错误实现。

证据：[Delphi 合同批](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-bc-contract-delphi-20261028/micro.json)、[FPC 合同批](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-bc-contract-fpc-20261028/micro.json)。

| 目标/组合 | B µs | C µs | 配对 r | 单侧95%上界 | A/A偏差 | 对数 | 状态 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| win64 wire/original | 5.322134 | 2.807682 | 0.526366 | 0.536747 | +0.6059% | 7 | 达到本批目标 |
| win64 request/original | 7.606795 | 4.924761 | 0.648509 | 0.656777 | -0.3012% | 7 | 达到本批目标 |
| win64 new-request/unicode | 5.730536 | 5.011415 | 0.875818 | 0.880917 | +0.2349% | 7 | 达到本批目标 |
| win64 response/unicode | 2.507756 | 1.829808 | 0.723741 | 0.734238 | +0.0098% | 7 | 达到本批目标 |
| win64 encode/unicode | 0.593089 | 0.490713 | 0.817777 | 0.832634 | -0.5209% | 7 | 达到本批目标 |
| win64 server-encode/unicode | 1.548853 | 1.366285 | 0.885227 | 0.899668 | -0.3812% | 7 | 达到本批目标 |
| win64 request/unicode | 4.829136 | 4.063138 | 0.841380 | 0.860692 | +0.1549% | 7 | 达到本批目标 |
| win64 new-request/original | 8.556798 | 5.899139 | 0.686861 | 0.703140 | -0.3226% | 7 | 达到本批目标 |
| win64 response/original | 6.173142 | 3.608692 | 0.589218 | 0.592608 | -0.4397% | 7 | 达到本批目标 |
| win64 encode/original | 2.155740 | 1.656697 | 0.767519 | 0.768505 | -0.5612% | 7 | 达到本批目标 |
| win64 server-encode/original | 2.967692 | 2.451011 | 0.834886 | 0.841396 | +0.1452% | 7 | 达到本批目标 |
| win32 wire/original | 4.862510 | 2.471450 | 0.506623 | 0.511739 | +1.3522% | 7 | 未定 |
| win32 request/original | 6.787978 | 4.363292 | 0.643421 | 0.645984 | -0.2901% | 7 | 达到本批目标 |
| win32 new-request/unicode | 5.162307 | 4.378409 | 0.854803 | 0.857611 | -0.0979% | 7 | 达到本批目标 |
| win32 response/unicode | 2.115462 | 1.459374 | 0.688194 | 0.692131 | -0.0064% | 7 | 达到本批目标 |
| win32 encode/unicode | 0.679179 | 0.456943 | 0.668643 | 0.688921 | -0.4526% | 7 | 达到本批目标 |
| win32 server-encode/unicode | 1.592922 | 1.223556 | 0.775135 | 0.783533 | +0.3798% | 7 | 达到本批目标 |
| win32 request/unicode | 4.300873 | 3.567879 | 0.828892 | 0.834932 | -0.3792% | 7 | 达到本批目标 |
| win32 new-request/original | 7.633903 | 5.249278 | 0.689243 | 0.697465 | -0.3279% | 7 | 达到本批目标 |
| win32 response/original | 5.487196 | 3.114254 | 0.566305 | 0.571734 | -0.1593% | 7 | 达到本批目标 |
| win32 encode/original | 2.926909 | 1.718393 | 0.584757 | 0.597451 | +0.0262% | 7 | 达到本批目标 |
| win32 server-encode/original | 3.593840 | 2.355523 | 0.655573 | 0.658690 | -0.9541% | 7 | 达到本批目标 |
| fpc64 encode/unicode | 1.153789 | 0.613500 | 0.531748 | 0.554387 | +2.1856% | 7 | 未定 |
| fpc64 server-encode/unicode | 6.614787 | 4.619870 | 0.702698 | 0.706585 | +0.0283% | 7 | 达到本批目标 |
| fpc64 encode/original | 4.822223 | 2.859344 | 0.594591 | 0.596855 | -0.4506% | 7 | 达到本批目标 |
| fpc64 server-encode/original | 10.454116 | 6.995581 | 0.668615 | 0.669964 | -0.6284% | 7 | 达到本批目标 |
| win32 wire/original | 5.076453 | 2.501783 | 0.493540 | 0.493918 | -0.2041% | 31 | 达到本批目标 |
| fpc64 encode/unicode | 1.153686 | 0.607251 | 0.526598 | 0.527865 | -0.2154% | 31 | 达到本批目标 |

表尾两个 31 对行对应 [Win32 original 确认](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-bc-original-confirm-20261028/micro.json)和 [FPC Unicode 编码确认](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-bc-unicode-confirm-20261028/micro.json)。旧的两条未定记录保留，没有覆盖或挑选有利轮次。

## 7. 真实 HTTP：固定 80 个会话全部结束

证据：[http-c128.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-ac-wide-20261028/http-c128.json)及对应 `http/rtl`、`http/fastmm5` 原始日志。固定 Delphi Win64、128 并发、65 I/O 线程、210 B 响应、默认 backlog、头体分离；RTL/FastMM5 × ab/Bombardier 分组，A/A 各 5 对、A/C 各 15 对，共 80 个服务进程会话。ab 每次先预热 20000 请求，再测 100000 请求；Bombardier 每次 30 s。

已逐一核对全部 80 组 ab/Bombardier/response 原始文件：ab 每次成功 100000、Failed=0、无 Non-2xx、总接收 21000000 B；Bombardier 的 others/1xx/3xx/4xx/5xx 均为 0，接收字节数等于 `210×req2xx`；每次预检响应均为 210 B、200 OK、Content-Length 78。80 份预检正文均为 78 B，SHA-256 一致为 `6a9de4d8d0503a85a3409b604a1342722e2251e6bb1537ef49bd49ec498074dd`。正文内容核对基于这些预检快照，负载阶段的状态/总传输量由工具验证。原始 JSON 的 errors 合计 0。

以下 A/C 为各自点中位数；RPS 的配对坏向比值 `r=A/C`，P99/CPU 为 `r=C/A`。CPU 为服务端进程 CPU/成功响应，ab 由每批 CPU 秒/100000 换算，Bombardier 按实际成功数换算。CI 和 A/A 沿用冻结驱动的配对判定（seed 199、5000 次重采样）。

| 分配器/指标 | A点中位 | C点中位 | 配对 r | 单侧95%下界 | 单侧95%上界 | A/A中位偏差 | 状态 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| RTL ab RPS | 27285.880 | 25123.920 | 1.089407 | 1.072662 | 1.106460 | -0.1258% | 未达标 |
| RTL Bombardier RPS | 26029.976 | 24681.934 | 1.058421 | 1.054464 | 1.061324 | -0.3199% | 未达标 |
| RTL Bombardier P99 ms | 45.897 | 55.659 | 1.212667 | 1.191429 | 1.212715 | -0.0021% | 未达标 |
| RTL ab CPU µs/成功响应 | 53.281 | 51.875 | 0.985030 | 0.953079 | 1.002994 | -2.0528% | 未定，A/A不稳且CI跨1 |
| RTL Bombardier CPU µs/成功响应 | 55.081 | 54.328 | 0.982980 | 0.977240 | 0.993441 | -1.8605% | 未定，A/A不稳 |
| FastMM5 ab RPS | 40270.120 | 39383.650 | 1.023596 | 0.989928 | 1.050769 | -0.1229% | 未定，CI跨1 |
| FastMM5 Bombardier RPS | 99311.618 | 98183.451 | 1.009910 | 1.000084 | 1.027214 | +0.4051% | 未达标，接近边界 |
| FastMM5 Bombardier P99 ms | 1.956 | 1.958 | 1.000000 | 0.999489 | 1.036243 | 0.0000% | 未定，CI跨1 |
| FastMM5 ab CPU µs/成功响应 | 129.688 | 132.500 | 1.014286 | 0.991358 | 1.047559 | +1.6291% | 未定，A/A不稳且CI跨1 |
| FastMM5 Bombardier CPU µs/成功响应 | 113.924 | 114.467 | 1.006197 | 0.995196 | 1.015680 | -0.6489% | 未定，CI跨1 |

RTL 的 ab 吞吐、Bombardier 吞吐和 P99 均有 15/15 对更差；ab 坏向比值范围 1.045238～1.124794，Bombardier 为 1.032706～1.067627，P99 为 1.021288～1.212759。配对吞吐降幅按 `1-1/r` 分别为 8.2069%、5.5197%；两个点中位数的吞吐降幅分别为 7.9234%、5.1788%，二者口径不同。

FastMM5 ab 有 10/15 对更慢，CI 跨 1；Bombardier 有 11/15 对更慢，配对吞吐下降 0.9812%，95% 下界 1.000084 仅略高于 1，因此保留“本批零预算判定未达标、接近边界”的精确表述；P99 有 7/15 对更差、配对中位等于 1，上界仍大于 1，不能归为通过。

A/A 中位偏差符合现行判定不代表每对波动小于 1%。RTL ab 的 A/A 比值范围为 0.931255～1.046799，RTL P99 为 0.979220～1.187493，显示这些指标仍存在宽波动；不把该噪声改作允许退化预算。RTL 三项关键结果的所有 15 对均指向恶化。四个 CPU/成功请求项目均未定，不能用 CPU 点中位略降抵消成功吞吐和 P99 的回退；各工具和分配器也不相互平均。

实际 HTTP 没有恢复到 A。微基准中完整请求/长头的改善不能外推成这份短 GET 负载的恢复。

## 8. 申请次数、累计字节及存活/释放

独立计数与普通计时分开。以下 A/C 申请数据取自关键确认 JSON 的 allocations 独立运行；它们是每次操作的累计申请次数/累计字节，不是存活堆或进程 RSS。新增 raw 会增加小头申请次数，累计字节下降也不能推导存活内存下降。

| 目标/组合 | A申请次数 | C申请次数 | A累计字节 | C累计字节 |
| --- | ---: | ---: | ---: | ---: |
| win64 wire/small | 10 | 13 | 530 | 511 |
| win64 wire/ascii | 24 | 33 | 3342 | 3066 |
| win64 wire/cookie | 14 | 17 | 50144 | 25447 |
| win64 request/ascii | 41 | 51 | 5407 | 4498 |
| win64 new-request/ascii | 54 | 64 | 6423 | 5514 |
| win64 response/ascii | 30 | 36 | 4862 | 3154 |
| win64 encode/small | 8 | 8 | 1018 | 1018 |
| win64 encode/ascii | 14 | 14 | 11667 | 11667 |
| win64 server-encode/ascii | 22 | 22 | 13422 | 13422 |
| win32 wire/small | 10 | 13 | 454 | 375 |
| win32 wire/ascii | 24 | 33 | 3042 | 2498 |
| win32 wire/cookie | 14 | 17 | 49984 | 25159 |
| win32 request/ascii | 41 | 51 | 5003 | 3818 |
| win32 new-request/ascii | 54 | 64 | 5535 | 4350 |
| win32 response/ascii | 30 | 36 | 4526 | 2562 |
| win32 encode/small | 8 | 8 | 982 | 982 |
| win32 encode/ascii | 14 | 14 | 11607 | 11607 |
| win32 server-encode/ascii | 22 | 22 | 13318 | 13318 |
| fpc64 wire/small | 11 | 14 | 610 | 655 |
| fpc64 wire/ascii | 26 | 35 | 3662 | 3674 |
| fpc64 wire/cookie | 14 | 18 | 33606 | 25559 |
| fpc64 request/ascii | 51 | 61 | 6024 | 5403 |
| fpc64 new-request/ascii | 64 | 74 | 6944 | 6323 |
| fpc64 response/ascii | 34 | 39 | 5222 | 3782 |
| fpc64 encode/small | 9 | 7 | 1196 | 966 |
| fpc64 encode/ascii | 15 | 13 | 12991 | 11615 |
| fpc64 server-encode/ascii | 25 | 23 | 14806 | 13372 |

核心 ASCII 的 A/B/C 独立计数见 [allocations-abc.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-199-20261028/allocations-abc.json)。各模式每次 10000 操作；MM hook 下的时间不用于普通性能比较。

| 目标/ASCII模式 | A次数 | B次数 | C次数 | A累计字节 | B累计字节 | C累计字节 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| win64 wire | 24 | 33 | 33 | 3342 | 3066 | 3066 |
| win64 string | 23 | 23 | 23 | 2016 | 2400 | 2400 |
| win64 request | 41 | 51 | 51 | 5407 | 4498 | 4498 |
| win64 new-request | 54 | 64 | 64 | 6423 | 5514 | 5514 |
| win64 response | 30 | 36 | 36 | 4862 | 3154 | 3154 |
| win64 encode | 14 | 14 | 14 | 11667 | 11667 | 11667 |
| win64 server-encode | 22 | 22 | 22 | 13422 | 13422 | 13422 |
| win32 wire | 24 | 33 | 33 | 3042 | 2498 | 2498 |
| win32 string | 23 | 23 | 23 | 1720 | 1912 | 1912 |
| win32 request | 41 | 51 | 51 | 5003 | 3818 | 3818 |
| win32 new-request | 54 | 64 | 64 | 5535 | 4350 | 4350 |
| win32 response | 30 | 36 | 36 | 4526 | 2562 | 2562 |
| win32 encode | 14 | 14 | 14 | 11607 | 11607 | 11607 |
| win32 server-encode | 22 | 22 | 22 | 13318 | 13318 | 13318 |
| fpc64 wire | 26 | 35 | 35 | 3662 | 3674 | 3674 |
| fpc64 string | 25 | 25 | 25 | 2336 | 3008 | 3008 |
| fpc64 request | 51 | 61 | 61 | 6024 | 5403 | 5403 |
| fpc64 new-request | 64 | 74 | 74 | 6944 | 6323 | 6323 |
| fpc64 response | 34 | 39 | 39 | 5222 | 3782 | 3782 |
| fpc64 encode | 15 | 14 | 13 | 12991 | 12303 | 11615 |
| fpc64 server-encode | 25 | 24 | 23 | 14806 | 14089 | 13372 |

大部分模式 C/B 的申请次数及累计字节相同，收益不能统一归因于减少堆申请。FPC encode 和 server-encode 各比 B 少一次申请、少 688 B 和 717 B；其它收益还包括减少扫描、托管记录复制及引用传递。A/B 的差别同时包含 raw 与新检查合同，不能要求功能不同的存活内存相等。

5000 个对象存活堆实测：A/B 各 3 次；C 在 A/C 和 B/C 两批分别各 3 次，共 6 次。下表单位 B/对象，为中位数；释放差额是整批释放后相对预热基线的总字节，不能当作每对象值。

| 目标/对象 | A存活B/对象 | B存活B/对象 | C存活B/对象 | C整批释放差额范围 |
| --- | ---: | ---: | ---: | --- |
| win64 header | 2111.995 | 3360.000 | 3359.995 | [-24, 0] B |
| win64 request | 4984.000 | 5536.000 | 5536.000 | [0, 0] B |
| win32 header | 1775.998 | 2695.998 | 2695.998 | [-12, 0] B |
| win32 request | 4148.000 | 4360.000 | 4360.000 | [0, 0] B |
| fpc64 header | 2240.000 | 3216.000 | 3216.000 | [0, 0] B |
| fpc64 request | 5040.000 | 5408.000 | 5408.000 | [0, 0] B |

已从原始 live_bytes/count 独立重算，B/C 在相同目标/对象上的存活量一致，Win64 Header 中位仅差整批 24 B；其余相同。三目标释放差额仅 0/-12/-24 B，本批未见 C/B 新增存活占用或释放后堆增长。精确原始记录在 [A/C memory](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-ac-wide-20261028/memory.json)、[Delphi B/C memory](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-bc-contract-delphi-20261028/memory.json)、[FPC B/C memory](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-bc-contract-fpc-20261028/memory.json)。

这些是当前 5000 对象批次的存活堆与释放数据，没有测进程 RSS、峰值或长时间保留；不据此宣称这些指标已被验证。

## 9. 工作区保护与证据完整性

实施前已备份 21 个既有改动文件及 staged/unstaged patch。选定 C 及测试输入冻结；非性能验证汇总确认 selected C 的 68 个源无漂移、关键验证输入无变动、index 未变、demo 资源未变。主 `run_http_bench.py` 属于实施前已有未暂存工作，本阶段没有修改；性能驱动使用隔离副本。原有暂存注释与其它工作没有纳入提交。

固定验收汇总：[acceptance-summary.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-199-20261028/acceptance-summary.json)、[ACCEPTANCE_REPORT.md](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-199-20261028/ACCEPTANCE_REPORT.md)。报告编写时再次独立核对：68 个 selected C 源文件及主工作区对应文件共 136 次 hash、54 份证据 JSON、38 个已记录基准/内存/HTTP 二进制及 3 个外部工具，均与各 manifest/build 元数据一致；没有依靠旧 exe 代替当前源的编译记录。

| 证据 | SHA-256 |
| --- | --- |
| selected-c/manifest.json | `b68c4c394d6eac777e99812596ce9d89670c5e4a522c5de866fd339dfe7816ef` |
| A/C source-manifest.json | `e4aa85129e536dc376ed385225c490ec4d55885b05b7bc4cf0f55ab8f9491428` |
| run_bench.py | `1fede27aff853759fe310646f5a90b93b1a8a9be5c1e76dc6a7f1f37b7e2b98e` |
| HeaderBench.lpr | `823adfdd7b3581daf986963b0df8a9b65ecaff50fd21c9a00eee31b69eff9b2a` |
| 原 run_http_bench.py | `227c9cc358ece0bde5895769b2c3f9da2c0482a4eeb47b343edcdfaaeae88025` |
| http-c128.json | `a656f68b4b9e882616d8878c2d9624f630ca835596d12913dc6ee2e69aa12477` |
| 独立31对micro.json | `61a689e3e419e7b14a8cf57e8db1fcc137754da737fa3d342e5cc7fff1e0a093` |
| allocations-abc.json | `fbc48a65291682cbdb00f5b5c7dd86149e5342c2407821ecc610fcaaf72366a4` |
| C Net/Net.CrossHttpParams.pas | `81c58db282cd500192e1a9b0614918772230020d246b0bfeccca6c00f1161682` |
| C Net/Net.CrossHttpUtils.pas | `a384475de1313988524deb301d39e13257ca42480a2c0815d0f501f9a81fd295` |
| C Net/Net.CrossHttpServer.pas | `6178a74d8942a50405fdfc198cef7ef0f2451291aedfb74264f7abf68ed1acef` |
| C Net/Net.CrossHttpClient.pas | `801e99a3a23cf1a342cd28d1ec9784c8c6a31cb795450a7ab775697613a770f4` |
| 工具 ab.exe | `fb56e132d54ca793f5cea6cfec3a6493396307979961f8b6c6ac10730bb951ae` |
| 工具 bombardier-windows-amd64.exe | `8a4559d5981b025523bb5144c4a8ae470b57049f7f27242227dfee71d2a17853` |
| 工具 FastMM5.pas | `07348bd27cdd98cb018b73c4003a55abd64d91273c0254a9270400452ac7ca08` |

代表性运行二进制如下。微基准取独立 31 对确认目录；HTTP 取固定 80 会话目录。完整产物 hash、编译命令/defines、harness hash 与生成路径在各目标 `HeaderBench-build.json`、`HeaderMemoryBench-build.json`、HTTP `source.json` 保留。

| 二进制 | SHA-256 |
| --- | --- |
| win64 A HeaderBench.exe | `ff700113a0ad473fdc185f75f438afa428b45afdf096eee19788780cad3795c1` |
| win64 C HeaderBench.exe | `da127c0736fa009763087f58d66a8a43e4fdd5ea16966656e0c3cff504df96a0` |
| win32 A HeaderBench.exe | `592bd7dbd1e0f494ed5aa583dbeea5a546236add6600ea0f53008ffd81dd5092` |
| win32 C HeaderBench.exe | `668902758ed2048c4c455e77097fa2707073dd384f569b90c9af4b929d2c9445` |
| fpc64 A HeaderBench.exe | `b9bb1cf51e7a704b0a682c77a6f558f265aa1c985cdbc24a336bb7d89a009033` |
| fpc64 C HeaderBench.exe | `a943f3b3d960693f03d8f816da0d48e4ae7342de63fbb8b22ab9bf9a70efd0be` |
| rtl A HttpServer.exe | `a7ca4f60edee290579b1eb550b2bca4af7dbcebb0163a40c08eb9c9b3c778e2c` |
| rtl C HttpServer.exe | `88ac5a3e692b693c8de04821590fbf302b653c2849e5511c6d2b6d97714e232a` |
| fastmm5 A HttpServer.exe | `ee58d9afb21f79e8d36472e1d7b62237ba78a40675ffa018063b308c2bfc961b` |
| fastmm5 C HttpServer.exe | `d9416c6d3524ef39bae3850cf91ed7271244beb899f1dc1abcd942e3e31e8c9f` |

原固定验收结束时的 [performance-process-exit.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-199-20261028/performance-process-exit.json) 记录当时活动基准/编译器进程为 0；后续独立 noquery 候选会话另行记录，不把这份结束快照解释为随后任何时刻都没有进程。

本次没有 Git 提交或推送。原实施中增量记录已原样备份至 [STAGE1_RESULTS.incremental-before-final.md](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-199-20261028/STAGE1_RESULTS.incremental-before-final.md)，SHA-256 为 `a76689e2d7e1d3aaf4c3a226a2fbf6017f96aef6828e959b49a742d8d073beb4`。

## 10. 剩余工作与下一步

| 项目 | 当前状态 | 下一步 |
| --- | --- | --- |
| 正确性、raw、失败、生命周期 | 已运行矩阵通过 | 只有新候选产生后按影响重跑 |
| 第一阶段范围审计 | 选定 C 审查通过 | 持续保持四生产文件白名单及阶段边界 |
| A/C 关键微基准 | 19 通过、4 回退、4 未定 | 继续在 #199 新增/放大成本内解释四个稳定差额；未定项不得刷测到过线 |
| 全门槛 31 对矩阵 | 未完整执行 | 本轮已存在关键失败，不把 7 对广度探索自动提升为全门槛通过 |
| 实际 HTTP | 80 个固定会话结束；RTL关键指标未恢复，FastMM5有回退/未定 | 在第一阶段范围内定位真实短GET差额；原始样本和未定结果保留 |
| A/B/C 分配与内存 | 既定5000对象存活/释放和独立计数已完成 | 新候选出现后按影响验证；RSS/峰值未测，不作声明 |
| 第二阶段 | 未启动 | 第一阶段全部门槛通过前不得启动 |

若第一阶段范围内仍无法恢复，应保持“阶段一未完成”，公开剩余差额和可验证原因。调整目标或扩大范围需要明确说明和更新计划，不能把第二阶段收益合并后宣布 #199 恢复。

## 11. 额外 S1-04 noquery 筛查：已否决，不追加复测

证据：[NOQUERY_HTTP_REPORT.md](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-04-noquery-http-20261028/NOQUERY_HTTP_REPORT.md)、[http-summary.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-04-noquery-http-20261028/http-summary.json)、[原始16会话](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-04-noquery-http-20261028/http-c128.json)、[integrity.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-04-noquery-http-20261028/integrity.json)。

原 C 固定验收结束后，按事先记录的计划另建独立候选 N。参考为 selected C，候选为 `candidates/noquery-selected/source`；两份 68 文件 manifest 仅 `Net.CrossHttpServer.pas` 不同，N 的该文件 SHA-256 为 `8fa704a0f6c99d0f228d51cb5a72992e0c67cc3c293603d371431462b4428153`。主源码及 selected C 没有变化。

仅测 RTL，固定参考自对照 3 对、C/N 5 对；128 并发、65 I/O 线程、210 B 响应、默认 backlog、头体分离，ab 预热 20000 后计时 100000，Bombardier 每次 30 s。16/16 个会话完整结束，已独立核对原始日志：零失败/非预期状态/响应长度异常；16 份预检正文均 78 B，hash 与原 C 固定验收的正文相同。没有删减不利样本。

吞吐坏向比值 `r=C/N`，P99/CPU 为 `r=N/C`。C/N 点中位与配对中位仍分开，不能把点中位较快当成配对判定通过。

| 指标 | C点中位 | N点中位 | 配对 r | 单侧95%下界 | 单侧95%上界 | A/A中位偏差 | 判定 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| ab RPS | 24216.550 | 25211.140 | 0.972814 | 0.917897 | 1.022862 | -1.6229% | 未定，A/A不稳且CI跨1 |
| Bombardier RPS | 23390.016 | 23458.407 | 1.001133 | 0.942413 | 1.008490 | +0.5960% | 未定，CI跨1 |
| Bombardier P99 ms | 46.872 | 46.872 | 1.000000 | 0.842128 | 1.000043 | +0.0055% | 未定，上界大于1 |
| ab CPU µs/成功响应 | 54.219 | 54.688 | 0.971182 | 0.896985 | 1.041667 | -1.1429% | 未定，A/A不稳且CI跨1 |
| Bombardier CPU µs/成功响应 | 56.859 | 57.643 | 1.013788 | 0.936613 | 1.064616 | +0.0596% | 未定，CI跨1 |

已从 16 行原始 JSON 重新构造所有配对并重算 seed=199、5000 次重采样，结果与汇总全部一致。Bombardier 吞吐 5 对只有 2 对候选更快，配对中位 r=1.001133；五项指标均没有获得满足 0% 上界目标的证据。结论是本次预设负载没有证明净收益，按预定退出规则否决 N，不追加复测、不写回生产或 selected C；不外推为所有负载都不可能收益。

该筛查没有改变原 C 的 15 对 HTTP/31 对微基准结果，也没有改变阶段一严格恢复失败的结论。最终 [process-exit.json](D:/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests/bin/s1-04-noquery-http-20261028/process-exit.json) 记录全部会话结束、活动基准/编译器进程为 0；二进制/工具检查及主源码对 selected C 的完整性检查均通过。
