# GetString 回归与性能验证

覆盖 `TUtils.GetString` 四个重载、`GetBufEncoding`、`IsUTF8` 和指针版 `TEncodingHelper.GetString`。`GetStringReference.pas` 冻结提交 `2318fb0` 的原算法；测试逐输入比较 UTF-16 结果、异常类名和消息、流位置，不把 Delphi/FPC 不同的 RTL 行为强行统一。

## 运行

在仓库根目录执行。构建参数沿用 `Net/Tests/FPC/HttpHotPathTests/run_tests.py`，编译器路径见 runner 顶部；不需要另外安装依赖。每个 label 保存不可变的 `Utils.Utils.pas` 快照，源码不同时必须使用新 label。

```powershell
python Utils/Tests/FPC/GetStringTests/run_tests.py --label verify
python Utils/Tests/FPC/GetStringTests/run_tests.py --label verify-checked --checked
python Utils/Tests/FPC/GetStringTests/run_tests.py --label verify-bool --checked --complete-boolean --case guard
```

默认依次重建 Delphi 13.1 Win64、Win32、FPC Win64；可以用 `--targets win32` 等选择目标。Checked 开启范围和溢出检查。`--complete-boolean` 只在测试源码快照内插入 `{$B+}`，不修改生产文件；该配置只执行保护页测试，不把基线原有的其他 B+ 行为问题混入本次验证。

普通/Checked 最终各 329,313 条检查；其中保护页 7,360 条。覆盖所有单字节/双字节组合、定向非法 UTF-8、确定性随机和合法 Unicode 样本、BOM、独立/无 BOM/自定义编码对象、非对齐切片、流位置及读取失败。保护页输入尾部紧贴 `PAGE_NOACCESS`，检查 1–80 字节及所有机器字对齐余数。

## 对比

先构建两份代码（相同版本的测试文件），再比较。`--source-root` 指向包含 `Utils.Utils.pas` 的独立源码目录；其他项目依赖仍来自本仓库。测试会拒绝比较测试源码不同、Checked 构建或程序哈希改变的产物。

```powershell
python Utils/Tests/FPC/GetStringTests/run_tests.py --label before --source-root <基线源码目录>
python Utils/Tests/FPC/GetStringTests/run_tests.py --label after
python Utils/Tests/FPC/GetStringTests/run_tests.py --label before --no-build --bench-only --before before --tag aa
python Utils/Tests/FPC/GetStringTests/run_tests.py --label after --no-build --bench-only --before before --duration 0.08
```

原始基线是 Git 提交 `2318fb0` 的 `Utils/Utils.Utils.pas`；可用 `git show` 取得原始字节并保存到新的基线目录，无须切换当前分支或恢复工作区文件。本轮本地快照保存在 `bin/baseline/source/`。

基准固定到逻辑 CPU 掩码 `4`（本机 CPU 2），串行执行；若机器不支持该掩码会明确失败。预热后 7 轮交替执行两份程序，按基线耗时校准同一迭代数，核对校验和，记录中位数和每轮原始样本。`--duration` 是每个计时样本的目标秒数；`--scenarios` 可选择样本。分配统计单独执行 1,000 次，计入 GetMem/AllocMem/ReallocMem，不把分配计数钩子的耗时用于性能结果。

17 个样本包括 8/32/128 字节、1/64 KiB ASCII，约 1 KiB 中文，约 1/64 KiB 中英混合及四字节字符，显式 UTF-8、UTF-8 BOM、UTF-16LE/BE、默认编码回退、内存流、通用流及自定义编码。多字节样本按完整模式重复，实际字节数略小于标称长度，前后完全一致。

## 证据

- `bin/<label>/<target>/source.json`：编译命令、生产源码/测试/runner/程序哈希。
- `build.log`、`results.txt` 或 `failure.txt`：构建和行为结果。
- `benchmarks.json`、`aa.json`、复测 JSON：原始计时、迭代数、配对变化及分配。
- `RESULTS.md`：本轮候选取舍、各阶段结果及最终基线对比。

Pascal 源文件使用 UTF-8 BOM/CRLF；`bin/` 和 `lib/` 是被 Git 忽略的独立产物目录。本机结果只代表 Windows 微基准；未验证 ARM、Linux、macOS 或端到端 HTTP 吞吐。
