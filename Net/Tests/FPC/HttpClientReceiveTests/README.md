# Issue #218：客户端接收与请求终态回归

修复普通 HTTP 请求完成、失败或超时后仍进入响应解析导致 `FRequestObj=nil` 的问题。生产改动仅在 `Net.CrossHttpClient.pas`，没有修改通用 Parser、Socket/TLS 或公共接口方法及 IID；连接私有字段改变，依赖单元需要完整重编译。

## 行为

- 活动请求检查与进入接收保护共用请求锁，终态清理先发生时不再启动解析。
- 普通最终响应在 `Decode` 返回并检查剩余字节后才发布成功、允许复用。同批额外数据被耗尽，完整合法响应仍成功一次，连接不复用。
- 已成功的早响应遇到额外数据时，保留在途发送，发送结束后关闭；失败和超时仍按原取消语义关闭。
- 正文写入内关闭或失败不能补发成功；完整响应就绪后关闭仍成功一次，只禁止复用。
- EOF 定界响应正常完成；100/102/103 和 101 协议升级保留原有语义。

## 测试边界

`HttpClientReceiveTests.lpr` 的 20 项组件测试运行真实请求、发送链、Parser、响应存储和完成逻辑。仅物理写由既有 `CROSS_HTTP_SEND_TESTS` 钩子保存回调，连接使用 `INVALID_SOCKET`；EOF 显式进入真实 `LogicDisconnected`。因此不把组件用例称为真实 TCP/TLS 测试。

覆盖正常复用、拆分头/正文、同包 CRLF/完整多余响应、空闲尾部、回调立即发下一请求、独立/旧入口早响应、早响应尾部及 close、失败/超时清理窗口、EOF/截断、响应流失败/重入关闭、临时响应、完整响应待发布期间关闭。失败/超时窗口通过覆写测试连接的 `Close`、在调用 inherited 前重入接收来确定次序，不靠线程抢时序。超时用例的 1100ms 等待只用于跨过真实的一秒阈值。

`run_network.py` 复用相邻 `HttpHeadTests` 的真实 TCP 客户端和 runner，在本地临时端口发送正常响应、追加 CRLF、完整多余响应、半截状态行、103 后最终响应及尾部，共 5 项。异常尾部必须保留第一个合法响应并换连接完成第二个请求；正常对照继续复用。没有新增依赖或外网请求。

组件构建启用既有 `CROSS_HTTP_SEND_TESTS`、`CROSS_HTTP_TIMEOUT_TESTS`；相邻网络套件及正式示例构建均不启用这些宏。Delphi 13.1 Win64 的响应流测试同时覆写 LongInt/NativeInt 写入入口，避免当前 RTL 从另一个虚方法进入而让断言失效。

## 构建和运行

在仓库根目录执行，沿用相邻 `HttpHeadTests` 的本机 Delphi 13.1/FPC 3.3.1、Lazarus 和 SDK 搜索路径，不修改全局工具链。输出仅在忽略的 bin/lib。

```powershell
python -B Net/Tests/FPC/HttpClientReceiveTests/run_tests.py delphi-win64 delphi-win32 fpc-win64 --label verified
python -B Net/Tests/FPC/HttpClientReceiveTests/run_tests.py delphi-linux64 fpc-linux64 --label verified
python -B Net/Tests/FPC/HttpHeadTests/build_response_tests.py delphi-win64 delphi-win32 fpc-win64 delphi-linux64 fpc-linux64 --label issue218-verified
python -B Net/Tests/FPC/HttpClientReceiveTests/run_network.py Net/Tests/FPC/HttpHeadTests/bin/issue218-verified/delphi-win64/HttpHeadTests.exe --report Net/Tests/FPC/HttpHeadTests/bin/issue218-verified/delphi-win64/network-results.json
```

Linux 编译产物在已有 Ubuntu 22.04/WSL 中运行。在本目录执行：

```bash
python3 -B run_tests.py --label verified --exe bin/verified/delphi-linux64/HttpClientReceiveTests
python3 -B run_tests.py --label verified --exe bin/verified/fpc-linux64/HttpClientReceiveTests
python3 -B run_network.py ../HttpHeadTests/bin/issue218-verified/delphi-linux64/HttpHeadTests --report ../HttpHeadTests/bin/issue218-verified/delphi-linux64/network-results.json
```

每项独立进程且有外部时限；网络服务器和子进程由 runner 负责回收。`--source-root` 可选择隔离源码重编基线，不能覆盖生产单元；`--cases` 可筛选组件场景。构建前后校验源码哈希，结果保存二进制哈希。

## 2026-10-30 验证结果

| 目标 | 专项组件 | 新增真实 TCP | 响应定界 | HEAD | 临时响应 |
| --- | --- | --- | --- | --- | --- |
| Delphi Win64 | 20/20 | 5/5 | 22/22 | 14/14 | 9/9 |
| Delphi Win32 | 20/20 | 5/5 | 22/22 | 14/14 | 9/9 |
| FPC Win64 | 20/20 | 5/5 | 22/22 | 14/14 | 9/9 |
| Delphi Linux64 | 20/20 | 5/5 | 22/22 | 14/14 | 9/9 |
| FPC Linux64 | 20/20 | 5/5 | 22/22 | 14/14 | 9/9 |

修复前隔离源码在三个 Windows 目标的首批 19 项中均为 10 通过、9 失败，记录在 `bin/before-final/<target>/results.json`，源码保留于 `bin/before-source/`。后加的第 20 项完整响应关闭竞争，在中间修复中单独确认失败，见 `bin/intermediate-eof-test/`，未冒称包含于首批基线。

最终组件结果位于 `bin/verified/<target>/`；Linux 为 `verified-results.json`，Windows 为 `results.json`。50 项网络回归及日志在相邻 `HttpHeadTests/bin/issue218-verified/<target>/`。最终生产单元 SHA-256 为 `943f441dfb8373d51f664dbb6973f3ffefdbbec7a3946cdd1b5bf88f134d9e8b`，五个网络构建的源码记录均匹配。

Delphi Win64/Win32、FPC Win64 的 WebSocket 组件（每目标 61 项定向、11,056 组合法组合及原有严格/发送/关闭检查）、11 项真实回环均通过；握手每目标 55/55。`HttpClient.dproj`、`WebSocketClient.dproj` 的 Release/Win64 正式 MSBuild 构建通过，日志在 `bin/formal/`。项目已有编译警告仍存在，不声称零警告。

额外运行了既有 `BatchCleanupTests/run_client.py`：Delphi Win64 为 2/6，FPC Win64 为 5/6。以修复前客户端源码隔离重编后，失败集合完全相同：Delphi 的 request-progress/request-reuse/request-expire 测试流仅覆写 LongInt 写入入口；两个编译器的 idle-reserved 也在原版本失败。本次未修改这些历史用例或扩展其实现范围。对照结果在相邻 `BatchCleanupTests/bin/issue218-{before,verified}/<target>/client-results.json`。

验证期间发现生产客户端单元曾回到与修复前副本逐字节相同的内容；在确认无新增用户差异后，按已通过构建的源码哈希恢复本次改动，随后重新进行最终构建、回归与哈希检查。保留 `bin/intermediate-eof/`、`bin/final-source/` 对照，不推断外部覆盖来源。

本轮没有实测 macOS、ARM、Win10 专属环境、真实 TLS 下的新增尾部场景或长期压力；Windows 使用本机系统。HTTP/1.1 无请求标识，若非法对端数据延迟到新请求已经发出之后才到达，不能据此声称可识别所有跨请求错误归属。
