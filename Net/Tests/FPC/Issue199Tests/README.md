# Issue #199 原始报文重放

将 [Issue #199 的抓包评论](https://github.com/winddriver/Delphi-Cross-Socket/issues/199#issuecomment-6392797357) 中 `Reassembled TCP Segments` 的十六进制区块还原，经真实 TCP 发给当前源码编译的 `TCrossHttpServer`。

## 报文与测试范围

- `request.hex` 是原始 HTTP 字节：请求头 700 字节、JSON 正文 165 字节，共 865 字节。Host、Referer、换行、Content-Length 和正文全部保留。
- SHA-256：`f4f7aaf808353472586a1eb8040d9b89c9d09fced27959d34b9e3886fee68e33`。提取时核对转储偏移连续，元数据在 `fixture.json`。
- 只向本机 `127.0.0.1` 随机监听端口发送，不连接报文中出现的地址。TCP 重组载荷发送一次，不重复发送抓包里的重传帧。
- `original-whole`：一次发送完整原始请求。
- `original-split`：先发送 700 字节请求头，间隔 100 ms 后发送 165 字节正文；这是两次应用层发送，不保证操作系统恰好使用两个 TCP 段。
- `ascii-control`：仅将 Referer 中“有模式窗口”的 UTF-8 字节替换为 ASCII `window`。
- `gbk-control`：仅将同一段中文换为 GBK 字节。这是编码对照，不是原始抓包，也不是推荐修复。
- 收到首条 200 后，再在同一连接发送 `GET /after`，检查正文是否污染下一请求。

## 构建和运行

依赖已有 Python 标准库及本机 Delphi/FPC 环境，不需安装 Python 包。构建参数沿用相邻 `HttpFailureTests/run_tests.py`；编译器、cncrypto 和 lazutils 路径集中在脚本顶部，可按其他机器的实际路径调整。

在仓库根目录运行：

```powershell
python -B Net/Tests/FPC/Issue199Tests/run_tests.py
```

只运行 Delphi Win64：

```powershell
python -B Net/Tests/FPC/Issue199Tests/run_tests.py --targets win64
```

要求正文和后续 GET 都正确解析，用作修复前后的回归门槛：

```powershell
python -B Net/Tests/FPC/Issue199Tests/run_tests.py --require-valid-body
```

默认模式只在编译、接收记录、响应观测或进程收尾不完整时返回非零，复现到库缺陷也会完整保存结果。`--require-valid-body` 额外要求所有场景的 Header、正文及后续方法正确；当前 Delphi 缺陷会使它返回 1。每次默认创建一个新的时间戳目录，可用 `--label <名称>` 指定；已有目录会拒绝覆盖。

`Issue199Server.lpr` 是最小诊断服务端，`Issue199Server.dpr` 是 Delphi 入口。服务端在最外层 `LogicReceived` 保存收到的真实字节，再调用原库；诊断副本执行相同字符串转换和 Header.Decode，只用于记录。业务回调保存 Body 并返回观测字段。**200 是诊断处理函数选择的状态，只证明请求进入了业务回调，不代表解析正确。**

输出在 `bin/<label>/`：

- 每目标 `build.log`、`build.json`：编译输出、完整命令、源码及 EXE 哈希。
- 每场景 `sent.bin`、`received-1.bin`：原始请求与服务端实际收到的字节；后者还包含发送成功的后续 GET。
- `body-1.bin`、`response.bin`、`followup-response.bin`、`server.log`：正文、完整响应和服务端记录。
- `result.json`、汇总 `results.json`：字节一致性、解析结果、连接复用及退出状态。

## 2026-10-26 本机实测

基线提交：`2318fb0`；没有修改生产库。Delphi 编译器版本 `37.0.59082.6021`（13.1），FPC 3.3.1；Windows ACP=936。最终命令：

```powershell
python -B Net/Tests/FPC/Issue199Tests/run_tests.py --label final --require-valid-body
```

三个目标全部编译成功；12/12 场景完成观测、收发字节一致、退出正常。8/12 场景解析正确，4/12 复现缺陷，因此严格模式退出码为 1。摘要和源码哈希保存为 `RESULTS.json`，逐次原始文件保存在 `bin/final/`。

| 目标 | RTL 默认代码页 | 原始整包 | 原始头体分开 | ASCII 对照 | GBK 对照 |
| --- | --- | --- | --- | --- | --- |
| Delphi Win64 | 936 | 正文丢失 | 正文丢失 | 正常 | 正常 |
| Delphi Win32 | 936 | 正文丢失 | 正文丢失 | 正常 | 正常 |
| FPC Win64 | 65001 | 正常 | 正常 | 正常 | 正常 |

表中“正常”表示 `Header.Count=10`、`ContentLength=165`、Body/RawBody 非 nil、正文与原始 165 字节逐字节一致，后续请求 Method 为 GET。它不表示所有 Referer 编码均正确显示；例如 FPC 的 GBK 对照仍有字符替换。

Delphi 原始报文的业务观测：

```text
headers=0 content_length=-1 body_type=3 body_nil=1 raw_body_nil=1 body_size=0
HEADER_DECODE ok=0 count=0
```

`body_type=3` 即 `btBinary`，符合报告的 BodyType 正确但正文为空。服务端在解析之前收到的原始 865 字节与 fixture 完全相同。

### 已确认的调用链

1. 抓包中的“有模式窗口”实际是 UTF-8，尾部为 `E5 8F A3 0D 0A`；后续评论将它称为 GBK，两者须区分。
2. `TCrossHttpRequest.ParseHeader`（`Net.CrossHttpServer.pas:4897`）通过 `SetString(FRawRequestText, MarshaledAString(...), ...)` 转成 Unicode。本机 Delphi 默认代码页 936，将该行尾转成 `... 003F 000A`，原来的 CR 消失。这里没有生成 NUL，不能声称逐字复现了评论中的 `00 0A`。
3. `THttpHeader.Decode`（`Net.CrossHttpParams.pas:1427`）遇到 bare-LF，清空 Header 并返回 False。
4. `TCrossHttpRequest.ParseHeader`（`Net.CrossHttpServer.pas:4913`）没有检查这个返回值，继续把 ContentLength 设为 -1，业务回调因此收到空正文。
5. 未消费的 JSON 留在流中；下一条 `GET /after` 到来时，Method 被解析为 `{"SOURCE_URL":...}GET`。诊断处理函数允许任何方法，因此这条错误方法仍返回 200；结果中单独记录了 `followup_method_valid=false`，不会把状态码成功当作解析成功。

本轮只添加复现测试。没有修复库代码，也没有替报告者的 Delphi/Windows 环境、IE 浏览器、Linux/macOS 或 HTTPS 做验证。


## 2026-10-27 实施后的回归

以上为修改前的历史证据；当前源码已迁移为字节 Header 解析及 `RawRequestHeader:TBytes`。新基线保存在 `bin/header-baseline-20261027/`，仍为 8/12 行为正确。修复结果保存在 `bin/header-final-20261027/`，三个 Windows 目标共 **12/12 行为正确**：原始 165 字节正文完整、Header=10、ContentLength=165、后续请求为 GET；新记录的 `header-1.bin` 与原始请求行＋头部＋结束空行逐字节相同。

```powershell
python -B Net/Tests/FPC/Issue199Tests/run_tests.py --label my-header-replay --require-valid-body
```

新增与修改的测试构建参考用户脚本参数，不改全局工具链。当前实现按旧参数和参考参数分别重编的体积为：

| 目标 | 旧参数（字节） | 参考参数（字节） | 减少 |
| --- | ---: | ---: | ---: |
| Delphi Win64 | 3,342,336 | 3,342,336 | 0 |
| Delphi Win32 | 2,255,872 | 2,255,360 | 512 |
| FPC Win64 | 3,189,760 | 3,187,200 | 2,560 |

对照命令和构建日志保存在 `bin/header-size-default-flags-20261027/`。本程序中缩减有限，不能把 O3/智能链接参数描述成显著缩小文件或提高运行性能。此前 `bin/final/` 的体积还包含源码版本差异，不能用于单独衡量编译开关效果。
