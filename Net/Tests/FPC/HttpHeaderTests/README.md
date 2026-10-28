# HTTP Header 字节解析回归

对应 `docs/plans/2026-10-26-http-header-byte-decoding-design.md` 的 C01～C25。直接构建当前仓库源码，不复用旧可执行文件；构建命令、相关源码 SHA-256、二进制 SHA-256、体积和运行结果保存在 `bin/<label>/<target>/`。

## 接口迁移

- `TNameValue` 改为 `TParamEntry`，直接增加 `RawValue: TBytes` 和 `HasRawValue: Boolean`。三个比较器类型同步改名，无旧名别名。
- 字节构造器复制传入缓冲区，并调用 `TUtils.GetString`；字符串构造器不生成 raw。`Add(record)`、`Items`、枚举和 `Assign` 保留完整记录及普通动态数组共享语义。通过名称写入字符串只清除被覆盖项的 raw。
- `THttpHeader` 继续继承 `TBaseParams`，新增 `Decode(Pointer, Integer, Boolean)`。字段名、冒号、CRLF、控制字节及 OWS 均先按字节处理；文本转换不参与寻找字段边界。
- 字符串 Decode 使用相同的结构规则，但不生成 raw。语法失败返回 False；清空模式最终为空，追加模式只回滚本次新增项。Header 的字符串 Create/Add 失败抛 `EConvertError`，经基类引用调用也相同。
- 非法指针/长度组合在改变集合前抛 `EArgumentException`。文本转换仍遵守 `TUtils.GetString` 的既有行为，转换异常也回滚本次新增项。
- `RawRequestText: string` 替换为 `RawRequestHeader: TBytes`；快照包含请求行、字段和结束空行，不包含正文，getter 返回副本。`ICrossHttpRequest` 新 GUID 为 `{6F42E391-5B3B-41F6-9A75-8C03312E8669}`，依赖必须完整重编。
- `Header.Encode` 返回当前 Value 组成的 Unicode 文本；客户端请求和服务端响应整头统一无 BOM UTF-8。非法字段和无效 UTF-16 拒绝输出；异步发送沿用已有失败回调，不发送半份 Header。raw 不用于默认发送。

## 测试覆盖

组件测试覆盖记录复制/共享、重复项、排序、删除、自赋值、字符串来源、所有字节值的语法检查、长度边界、BOM、GetString 的转换或异常、替换/追加回滚、UTF-16 合法性、请求行和 query 分隔、百分号仅解码一次、接口 GUID 及快照隔离。

服务端使用真实 Parser/路由/响应队列，网络写替换为同步内存收集器，核对 UTF-8 字节、失败零写入、一次回调和后续请求不污染旧快照。multipart 使用实际文件流，覆盖整块/分块/逐字节输入、中文 filename、二进制正文和失败后的临时文件清理，存储目录显式设为当前测试二进制所在目录。

客户端使用真实 TCP 回环，由 Python 发送原始响应并捕获完整请求：UTF-8 值与状态描述、非 UTF-8 字节、正文定界、重复 Set-Cookie、空头/空原因文本、非法响应，以及字段/首行/query 名称/query 值中的无效 Unicode。非法发送必须没有任何线上请求字节且回调一次。

## 运行

在仓库根目录执行；每次使用新的 label，避免覆盖证据。

```powershell
python -B Net/Tests/FPC/HttpHeaderTests/run_tests.py --label my-release
python -B Net/Tests/FPC/HttpHeaderTests/run_tests.py --label my-checked --checked
python -B Net/Tests/FPC/HttpHeaderTests/run_tests.py --label my-linux --targets delphi-linux64 fpc-linux64 --build-only
```

Linux 二进制在目标系统运行同一个 Python runner：

```sh
cd /mnt/d/GitHub/Delphi-Cross-Socket/Net/Tests/FPC/HttpHeaderTests
python3 -B run_tests.py --label my-linux --executable bin/my-linux/delphi-linux64/HttpHeaderTests
python3 -B run_tests.py --label my-linux --executable bin/my-linux/fpc-linux64/HttpHeaderTests
```

Release 参数参考用户编译脚本：Delphi `C- D+ I- L+ O+ Q- R- Y+`，FPC `-O3 -CX -Xs -XX`；Checked 分别启用断言、I/O、范围和溢出检查。保留 Delphi 调试信息，没有加入 madExcept 或改动全局编译配置。

## 2026-10-27 验证

| 目标 | 配置 | 组件断言 | TCP 场景 | 结果目录 |
| --- | --- | ---: | ---: | --- |
| Delphi 13.1 Win64 | Release / Checked | 各 1041 | 各 9 | `bin/final-v2/win64`、`bin/final-checked-v2/win64` |
| Delphi 13.1 Win32 | Release / Checked | 各 1041 | 各 9 | `bin/final-v2/win32`、`bin/final-checked-v2/win32` |
| FPC 3.3.1 Win64 | Release / Checked | 各 1041 | 各 9 | `bin/final-v2/fpc64`、`bin/final-checked-v2/fpc64` |
| Delphi Linux64，WSL Ubuntu 22.04 实际运行 | Release | 785 | 9 | `bin/final-linux-v3/delphi-linux64` |
| FPC Linux64，WSL Ubuntu 22.04 实际运行 | Release | 1041 | 9 | `bin/final-linux-v3/fpc-linux64` |

以上均通过。Delphi Linux 的 GetString 对本测试中的 128 个非法 UTF-8 高位单字节组合抛转换异常；这些场景检查异常类别和回滚，不再检查不存在的解码结果，所以断言数较少。其他四个目标的同组组合不抛异常。此处保留工具函数既有平台差异，不宣称已实现跨平台统一的非 UTF-8 回退；正常 UTF-8 中文及 emoji 在五个目标上通过。`Utils.Utils.pas` 未修改。

相邻测试重新构建后的结果（Windows 三目标均为 Delphi Win64/Win32、FPC Win64）：

- #199 原始整包/分段及 ASCII、GBK 对照：12/12；正文 165 字节、Header=10、下一请求 GET，完整请求头快照逐字节一致。
- HTTP 失败终态/队列/正文策略/存储：每目标 143/143。
- HEAD：每目标 14/14；临时响应 9/9；响应解析/连接复用 22/22。
- 认证：每目标 59/59；请求生命周期：每目标 6/6。
- MultipartStream：每目标 21/21。
- 热路径 tokens / headers / router：每目标 13543 / 4008 / 19 项断言。
- WebSocket 握手：每目标 47/47，包括非法 Header 拒绝发送的新预期。
- Chunk/trailer：Delphi Win64 100/100。

额外执行的 `WebSocketFrameTests` 综合程序在 Win64 发送器测试阶段发生访问异常；用修改前 HEAD 的四个 HTTP 单元隔离重编也在同一偏移 `16DFCC` 复现。记录分别在 `../WebSocketFrameTests/bin/header-20261027/win64/run-0.log` 和 `../WebSocketFrameTests/bin/header-baseline-20261027/run.log`。没有将该综合测试列为通过，也没有扩大本次范围修改发送器；握手用独立回环工程验证。

未执行 macOS、ARM、TLS 全矩阵或长时间性能压力测试。双存储增加 raw 的分配/持有成本，本次不作性能提升结论。


## 2026-10-28 三项修复验收

上一轮复核发现的协议字段 BOM 归一、multipart 失败未传播、WebSocket 测试 nil Owner 三项问题均已修复。`REPAIR_RESULTS.json` 保存本轮源码哈希、结果路径、失败基线和正式构建记录；前面的 2026-10-27 结果保留为历史记录。

1. 在字节 Decode 中、构造 TParamEntry/调用 GetString 之前检查原始协议控制字节。CL、Connection、Upgrade、WebSocket Key/Version/Accept/Protocol 等字段禁止高位字节；Expect、Transfer-Encoding 的引号参数保留允许的 obs-text，原始引号/转义边界单独扫描。现有层继续检查数字、token、Base64 和支持的传输编码。这不是新增完整字段语法解析器，也不改变字符串 Decode 的结构合同。TParamEntry 的字段/两个构造器、RawValue 共享约定、GetString 本身均保持不变。
2. 服务端遇到 multipart.Decode=frFailed 立即进入 Parser.Fail(400)，不把已解析的部分表单交给成功业务回调。文件清理继续遵循 AutoDeleteFiles 配置；本轮清理测试明确启用 True，没有改变默认配置。
3. WebSocketSenderTests 的连接持有有效 TCrossSocket Owner，直到连接完成析构。生产构造器和发送算法未放宽；内存 HTTP 连接辅助测试用独立 FCleaned 标记确保 INVALID_SOCKET 的清理实际执行一次。

回归新增：控制字段 BOM/重复项/高位字节、普通 BOM 文本兼容、合法/非法引号参数；双端 BOM 握手；HTTP CL/TE BOM 拒绝；完整 multipart 请求的路由不触发、断连、分段到达和上传文件清理。TCP 分段用例等待本次新建的文件，排除上一连接异步删除文件造成的测试同步竞争。

| 目标 | 配置 | 组件断言 | 真实 TCP 场景 | 结果标签 |
| --- | --- | ---: | ---: | --- |
| Delphi Win64/Win32、FPC Win64 | 各 Release | 各 1138 | 各 22 | `repair-final-v3-20261028` |
| Delphi Win64/Win32、FPC Win64 | 各 Checked | 各 1138 | 各 22 | `repair-checked-v3-20261028` |
| Delphi Linux64，WSL Ubuntu 22.04 实际运行 | Release | 882 | 22 | `repair-linux-v3-20261028` |
| FPC Linux64，WSL Ubuntu 22.04 实际运行 | Release | 1138 | 22 | `repair-linux-v3-20261028` |

22 个真实 TCP 场景由服务端 10 项和客户端 12 项组成；包含 Expect UTF-8 引号参数及 HEAD 响应 TE 引号参数元数据。Delphi Linux 的断言数量差异仍来自 GetString 对 128 个非 UTF-8 组合的既有异常行为。

- 双向 WebSocket 握手：Windows 三目标及 Linux 两目标各 **55/55**，包含 8 个新增 BOM 拒绝场景；失败基线为 **46/55**。
- WebSocket 综合程序：五目标均通过组件 61 项、11,056 组帧/拆分组合、原发送生命周期测试和 11 个真实 TCP 回环，之前的访问异常不再出现。Windows 使用原 `run_tests.py` 的 bin 日志，Linux 使用 `header-repair-linux-v3-20261028` 标签。
- Sender 队列专项：Windows 三目标各 **23/23**，标签 `sender-header-repair-20261028`。
- HTTP 失败/队列/存储：Windows 三目标各 **143/143**；HEAD、临时响应、响应解析/复用分别各 **14/14、9/9、22/22**，标签 `header-repair-final-20261028`。
- 热路径 tokens/headers/router：Windows 三目标各 **13543/4008/19**；#199 原始整包/分段及对照 **12/12**，包括原始头快照和 165 字节正文。
- 正式工程：通过原 `.dproj` 的 MSBuild Release/Win64 全量单元构建，HttpServer、WebSocketServer、HttpClient 均成功。日志在本目录 `bin/repair-production-v2-20261028/`。

未执行 macOS、ARM、完整 TLS 互操作或吞吐压力测试。现有 multipart EOF/边界完整语法、非 UTF-8 回退策略不在本轮扩展范围；本轮明确修复 frFailed 的 HTTP 传播。尚未提交或推送。
