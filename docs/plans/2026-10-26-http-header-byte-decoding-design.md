# THttpHeader 原始字节解码与 UTF-8 输出设计草案

创建日期：2026-10-26。最近更新：2026-10-27。

状态：补充修复已完成并验证（2026-10-28）。C01～C25 数据结构与 GetString 解码策略保持，第 15 节三项问题已修复；第 14 节是上一轮历史验证。提交另按用户授权处理。

历史复现与初次分析基线：master，`2318fb0a687399527fa8abdc88325b2297ceedde`。文中原始行号和 #199 历史测试结果对应这一基线。

设计确认时的工作区 HEAD：`48159035aac1285b37af4ecb82c166438e827920`，其后续提交优化了 TUtils.GetString 并补充相关测试；这是已有工作，本次不修改或回退。实施时复核并使用当前 GetString，不把历史 #199 结果表述为该新 HEAD 的实测结果。工作区已有未跟踪的 `Net/Tests/FPC/Issue199Tests/`，属于本会话前序复现工作，保留不动。

## 1. 已确认的设计决策

| 编号 | 决策 | 边界 |
| --- | --- | --- |
| C01 | `THttpHeader` 继续继承 `TBaseParams` | 保留基类可替代性，不实施独立 Header 类；公开记录重命名及原始请求头属性替换另有源码兼容变更 |
| C02 | 新增 `Decode(Pointer, Size)` 作为网络字节入口 | 字段结构按原始字节解析，已提取字段值通过字节构造器转换为 Unicode 字符串 |
| C03 | 仅字节来源构造/解码保存原始值字节 | 同时保存 `Value` 和 `RawValue`；字符串构造或赋值不生成、缓存原始字节 |
| C04 | 通过集合字符串赋值路径修改时，使对应原始值失效 | 清除被改项的 `RawValue` 和 `HasRawValue`，不影响其他项；完整 record 写入按 C23 信任其完整状态 |
| C05 | 直接扩展原公开记录，并将 `TNameValue` 重命名为 `TParamEntry` | 容器、Items、枚举统一使用同一种公开记录；不保留另一层 Name/Value 投影视图，不新增 TryGetRawValue |
| C06 | 原始字节保留为接收来源信息 | 用于诊断、检查和必要时按明确编码重新解释，不参与默认发送，不承诺完整报文字节级重放 |
| C07 | `Encode` 继续生成 Unicode Header 文本 | 服务端响应头、客户端请求头均在最终组合完成后统一 UTF-8 编码，不加 BOM |
| C08 | 特定字段继续执行自身格式化规则 | URI、下载文件名参数、Cookie 等先格式化，再整体 UTF-8；ASCII 格式化结果的字节保持不变 |
| C09 | `TParamEntry` 提供字符串、原始指针两个重载构造器 | 字节构造器复制原始数据为自有 TBytes；字符串构造器 RawValue=nil、HasRawValue=False |
| C10 | 字节到字符串直接复用 `TUtils.GetString` | 沿用该函数已有编码检测和回退，不新增严格 UTF-8 解码助手，不在本次修改 TUtils.IsUTF8 |
| C11 | 服务端 `RawRequestText: string` 替换为 `RawRequestHeader: TBytes` | 同步字段、getter、接口和类属性命名；保存原始请求行＋请求头＋结束空行，不含正文，不随解析后 Header 修改 |
| C12 | D02 选择 B：保留 Header 的字符串入口，统一严格语法和失败报告 | Decode(string) 返回整段是否有效，合法空集合为 True、非法行使整次失败；Create(string)/Add(string) 解析失败抛异常，仅作用于 Header，不改变其他参数类的合同 |
| C13 | D03-1 选择 B：Decode 按指定长度解析字段集合，末尾结束空行可省略 | 单字段可不带 CRLF；字段之间仍须 CRLF；已经出现结束空行后再有数据则失败；网络/Multipart 上层仍负责完整报文边界 |
| C14 | D03-2 选择 A：空输入有效，非法指针/长度组合统一抛参数异常 | Decode(nil,0) 或 Decode(P,0) 成功且不读取指针；空字节构造得到空 Value/RawValue、HasRawValue=True；负长度或 nil/正长度在任何状态修改之前抛异常 |
| C15 | D03-3 选择 A：RawValue 只保存去掉值两侧 OWS 后的字节 | Header.Decode 在字节层仅裁去最外侧 SP/HTAB，再调用记录字节构造器；不删内部/引号内空白或其他字符，构造器不做 Trim；完整空白仍在 RawRequestHeader 快照中 |
| C16 | D04-1 选择 A：替换模式解析失败清空，追加模式失败回滚本次新增项 | 参数合法但字段语法错误时，AClear=True 最后为空，False 保留全部原有项及 raw；不留下半份新字段。C14 的参数错误仍在状态修改前抛异常 |
| C17 | D04-2 选择 A：Assign(Self) 直接返回 | 基类在 Clear 之前判断 ASource=Self，保留全部项、顺序、文本、raw 和标记，不做复制或重新分配；经基类引用同样成立，Assign(nil) 仍清空 |
| C18 | D05-1 选择 A：RequestCmdLine 和 StatusText 使用 TUtils.GetString 生成 | 先按原始字节确定首行/各部分边界，再转换相应文本；RequestCmdLine 不包含末尾 CRLF、不执行百分号解码；公开文本不反向驱动结构解析，RawRequestHeader 不变 |
| C19 | D05-2 选择 A：保留非 ASCII request-target 的兼容能力 | 先确定方法/目标/版本及路径/查询结构，文本片段使用 TUtils.GetString，再按既有 URL 规则解码一次；标准百分号形式和直接 UTF-8 形式均支持，不额外收紧为 ASCII-only，不顺带改写加号、非法百分号或路径规范化行为 |
| C20 | D06-1 选择 B：Encode 遇到非法字段拒绝整块输出并抛异常 | 不再静默跳过非法名称或含 CR/LF/NUL 等不允许字符的字段；容器内容不变，不自动替换/删除控制字符；不把校验提前到普通字符串赋值，发送入口必须处理生成失败 |
| C21 | D06-2 选择 A：完整 Header 文本含无效 UTF-16 序列时拒绝发送并抛异常 | 最终 UTF-8 编码前检查完整文本，含字段、首行/状态描述及其他拼接内容；合法代理项对正常编码，孤立或错误配对不得替换后继续；不改容器、不提前到赋值时抛错、不发送部分 Header |
| C22 | D07 选择 B：公开 TParamEntry 的 TBytes 沿用共享语义 | Items/枚举及接纳、复制记录时不为 RawValue 自动深拷贝；调用方将共享 raw 视为只读，修改前自行 Copy。字节构造器复制输入和 RawRequestHeader getter 返回副本的既定合同保持 |
| C23 | D08 选择 A：Add(record) 和 Items 赋值完整接纳 TParamEntry | 保留 Name/Value/RawValue/HasRawValue，RawValue 按 C22 共享；不重新解码或自动修正，调用方负责记录内部一致性。字符串入口仍按 C04 使对应 raw 失效 |
| C24 | D09-1 选择 A：四个公开类型统一改名，不保留旧名别名 | TNameValue→TParamEntry，INameValueComparer→IParamEntryComparer，TNameValueComparison→TParamEntryComparison，TNameValueComparer→TParamEntryComparer；源码引用同步迁移并全量重编 |
| C25 | D09-2 选择 A：ICrossHttpRequest 更换 GUID | 接口名保持，实施时生成新 GUID 标识新返回类型/属性合同；不保留旧接口或适配器，不顺带修改其他未变更接口的 GUID；调用方迁移并全量重编 |

以上 C01～C25 均已确认；逐项选择汇总见第 9 节。接收文本复用 TUtils.GetString，字段语法、失败状态、来源信息和发送行为分别依上述合同执行。发送侧 UTF-16 校验不改变接收侧复用工具函数的决定。接口名保持 ICrossHttpRequest，接口标识按 C25 更新；本文件不再保留尚未决定的设计分支，也不把设计确认视为实施授权。

## 2. 背景、现有证据与范围

Issue #199 的十六进制转储已还原为 865 字节 HTTP 请求：700 字节头、165 字节 JSON 正文；SHA-256 为 `f4f7aaf808353472586a1eb8040d9b89c9d09fced27959d34b9e3886fee68e33`。转储中 Referer 的中文实际为 UTF-8，后续评论称为 GBK，两者需区分。

当前 Delphi Win32/Win64 实测：默认代码页 936 将 Referer 行尾转换成 `003F 000A`，CR 消失；Header.Decode 失败且清空字段，但返回值被忽略，导致 `ContentLength=-1`、`BodyType=btBinary`、`Body=nil`、`RawBody=nil`。未消费的 JSON 继续污染下一条 GET 的 Method。没有复现评论中的 NUL 替换细节。FPC Win64 默认代码页 65001 下原始报文正常。

前序测试三个编译目标均构建成功，12/12 场景完成观测，4 项复现 Delphi 缺陷、8 项解析正常。完整记录见 `Net/Tests/FPC/Issue199Tests/README.md` 和 `RESULTS.json`。Node.js v24.19.0 后续内存重放也确认整包/头体分开发送均保留 10 个字段和完整 165 字节正文，下一请求为 GET；没有新增 Node 测试文件。

目标：保证字节结构、Unicode 使用体验、来源信息和失败处理各自明确，并迁移服务端请求、客户端响应、multipart 三个字节入口。

非目标：重写 socket、HTTP 正文状态机、WebSocket、认证或 URL/Cookie 协议；新增通用协议框架；原始 Header 透明转发；逐字段混合输出编码；未授权的提交或推送。

## 3. 公开数据结构、构造器与继承兼容

保持 `THttpHeader = class(TBaseParams)`。直接扩展原公开记录，并统一重命名为 `TParamEntry`；这是原类型的演进，不是额外新增一个只供内部使用的平行类型。内部仍采用有序列表、允许重复，不引入单值字典。

```pascal
TParamEntry = record
  Name: string;
  Value: string;
  RawValue: TBytes;
  HasRawValue: Boolean;

  constructor Create(const AName, AValue: string); overload;
  constructor Create(const AName: string;
    const ARawValue: Pointer; const ARawSize: Integer); overload;
end;
```

- `FParams` 使用 `TList<TParamEntry>`；公开 `Items[index]`、GetItem/SetItem、Add(record)、枚举器 Current 均使用同一个 `TParamEntry`。调用方直接读取 `Items[I].RawValue/HasRawValue`，不增加 TryGetRawValue 方法或另一层公开视图。
- D09-1 已选择 A：比较器和回调的泛型元素类型统一为 TParamEntry，相关类型名称同步为 IParamEntryComparer、TParamEntryComparison、TParamEntryComparer；不保留 TNameValue、INameValueComparer、TNameValueComparison、TNameValueComparer 的别名。默认名称索引器继续返回/写入 string。
- `HasRawValue=True` 且数组为空表示“解码得到的空字段值”；`False` 表示没有有效原始来源。不能只用数组长度判断。
- HasRawValue 仅表示有原始字节来源，不表示通过了严格 UTF-8 验证。Value 采用 TUtils.GetString 的现有转换结果。
- URL 参数、Cookie 等其他使用者同步采用扩展后的公开记录，但其字符串路径不生成原始字节；记录扩大带来的固定内存成本需评估，不借机改变这些类的协议语义。
- 当前 Add、Assign、Remove、Clear、Sort 及私有 SetParam/SetItem 均直接修改基类列表。必须在基类统一写入路径维护状态，不能只在派生类隐藏同名方法或重新声明属性。
- 通过 `TBaseParams` 引用执行同样操作必须结果一致。保留继承避免了 Header 到基类的赋值断点，但公开记录布局/命名和 ICrossHttpRequest 返回类型会改变，需迁移调用方并全量重编；不承诺旧 DCU/PPU 二进制兼容。
- 不采用派生类并行原始值表：重复字段、排序和 Assign 会使对应关系复杂化；也不新增稳定 ID、元数据插件等通用框架。

### 3.1 两个构造器的职责

字符串构造器直接保存 Name/Value，设置 RawValue=nil、HasRawValue=False，不执行 UTF-8 编码。

字节构造器在检查指针和长度之后，先复制原始数据，再使用现有 TBytes 重载转换：

```pascal
Name := AName;
SetLength(RawValue, ARawSize);
if ARawSize > 0 then
  Move(ARawValue^, RawValue[0], ARawSize);
Value := TUtils.GetString(RawValue);
HasRawValue := True;
```

以上是校验后的核心逻辑示意，未实现。D03-2 已选择 A：长度为零时不读取指针，Value=''、RawValue=nil、HasRawValue=True；负长度或非零正长度配合空指针抛参数异常。检查应早于字段赋值、分配及数据复制。构造器只负责值的字节保存与文本转换，不承担 HTTP 名称、冒号、CRLF 等语法验证。

TUtils.GetString 的现有行为包含 BOM 检测、UTF-8 识别及必要时的系统默认编码回退；这项复用已由用户明确指定，不再称为严格 UTF-8 解码，也不另造协议专用解码函数。该调用只作用于已经截取出的字段值，不能恢复成“先将整块请求头转 string 再拆行”。原始副本保留完整字节，即使 GetString 去除 BOM 或使用回退编码，也不修改 RawValue。

THttpHeader 字节 Decode 可直接构造 TParamEntry 并加入列表，不需要另加 `_AddDecodedParam` 辅助接口。

### 3.2 状态维护规则

| 操作 | Value | 原始值状态 |
| --- | --- | --- |
| 字节构造器 / 字节 Decode 成功 | 保存 TUtils.GetString 的结果 | 保存自有字节，HasRawValue=True |
| Header[Name] 赋值 | 更新现有规则确定的那一项 | 清除该项 RawValue，HasRawValue=False；其他同名项不受影响 |
| Create(Name, Value) / Add(Name, Value) | 保存传入字符串 | 新项不生成原始值；覆盖项原始值失效 |
| Add(TParamEntry) | 追加传入的完整记录，允许重复 | 按 C23 保留四个字段，raw 按 C22 共享，不重新解码、不自动清除或校正 |
| 外部 Items[Index] 替换 | 替换指定的完整记录 | 按 C23 保留传入状态，调用方负责 Name/Value/raw/标记一致性 |
| Header 到 Header 的 Assign | 按顺序复制字符串 | 通过可信内部复制保留对应 RawValue/HasRawValue；经基类引用调用也应成立 |
| 从普通字符串参数对象 Assign | 按既有顺序复制字符串 | 不合成原始接收值 |
| Remove / Sort | 按整个内部项操作 | 原始信息和文本一同删除或移动 |
| Clear / 析构 | 清除当前项 | 同时释放原始字节 |

`Assign` 必须复制完整记录并保留已有元数据，RawValue 按 C22 共享；在完成 C17 的自赋值保护后，可按完整记录遍历/追加的方式复制。来源是字符串构造项时，其 raw 本来为空，不根据文本合成来源信息。公开记录已包含 RawValue，旧稿“公开视图必然丢失 raw”的前提不再成立。

D08 已选择 A：容器信任完整 record 的四个字段，不重新调用 GetString 去比较 raw 与 Value，也不通过来源猜测自动清除 raw。调用方取出记录后若直接改 Value 再写回，必须自行维护来源状态；推荐使用 `TParamEntry.Create(原名称, 新字符串)` 重新构造，或同时将 RawValue 置 nil、HasRawValue 置 False。仅改变共享数组引用不构成重新解码；若修改字节，应按 C22 先 Copy，并用字节构造器建立一致的新记录。

Header 内部 Assign 复制已有字节来源，不是根据字符串新生成 raw，和“字符串写入不保存原始字节”的规则一致。

“原始值”关联当前未修改项，不保存已被替换字段的历史版本。不把 `UTF8.GetBytes(新 Value)` 标记为原始接收数据；字符串写入没有这次转换和分配。

### 3.3 内存所有权与原始值读取

字节构造器复制需保留的输入值，并生成自有字符串；Parser 清空/复用接收缓冲不影响它。D07 已选择 B：公开记录按值返回/传递时，RawValue 沿用 TBytes 引用计数共享语义，不为 Items、枚举、Add(record)、Items 回写或 Assign 自动深拷贝。D08 已选择 A：完整记录写入保留元数据，并采用该共享规则。

调用方必须把共享 RawValue 视为只读；需要修改数组元素时先显式 Copy。直接写共享数组会影响其他持有者，而且不会自动重新生成 Value 或调整 HasRawValue。此项是使用约定，不通过新增包装类型或自动写时复制机制强制执行。

容器内部替换字符串、清除/删除字段、排序或失败回滚应调整完整记录及引用，不原地修改共享原始数组的字节。释放一份记录/对象后，其他有效引用仍可读取原数组。Name/Value 字符串保持其原有语言语义。

本选择不改变两项已确认边界：TParamEntry 字节构造器仍从 Pointer+Size 复制数据；Request.RawRequestHeader getter 仍返回完整快照的副本。它只决定公开记录中已经持有的 RawValue 如何在记录/容器之间共享。

读取方式直接使用公开记录：

```pascal
LItem := Header.Items[I];
LText := LItem.Value;
if LItem.HasRawValue then
  LBytes := LItem.RawValue;
```

不新增 TryGetRawValue 或独立 raw 属性。无来源由 HasRawValue=False 表示，不自动从 Value 编码补齐。D03-3 已选择 A：Header.Decode 截取去除字段值两侧 SP/HTAB 后的原始字节，并将该范围传给记录字节构造器。RawValue 不包含冒号、两侧 OWS 或 CRLF；不等于请求对象保存的完整 RawRequestHeader 快照。

该裁剪只发生在 HTTP 字段解析器中，TParamEntry 字节构造器自身仍完整保存调用方传入的范围并调用 TUtils.GetString，不附加 Trim。字段值内部空白、引号内空白和中文空格均不裁剪；全为 OWS 的值得到 Value=''、RawValue=nil、HasRawValue=True。

### 3.4 集合声明的类型替换

```pascal
FParams: TList<TParamEntry>;

function GetItem(AIndex: Integer): TParamEntry;
procedure SetItem(AIndex: Integer; const AValue: TParamEntry);
procedure Add(const AParamValue: TParamEntry); overload;

property Items[AIndex: Integer]: TParamEntry
  read GetItem write SetItem;
property Params[const AName: string]: string
  read GetParam write SetParam; default;
property Count: Integer read GetCount;
```

枚举器 Current 和比较器的元素类型统一替换。不保留“内部 TParamEntry＋公开 TNameValue”的双层结构；按 C24 不提供旧名称过渡别名，所有使用者迁移到新名称。

比较器声明按原 Delphi/FPC 条件分支保持同样的能力，仅统一名称与元素类型：

```pascal
IParamEntryComparer = IComparer<TParamEntry>;
TParamEntryComparison = {$IFDEF DELPHI}TComparison<TParamEntry>{$ELSE}TComparisonAnonymousFunc<TParamEntry>{$ENDIF};
TParamEntryComparer = {$IFDEF DELPHI}TDelegatedComparer<TParamEntry>{$ELSE}TDelegatedComparerAnonymousFunc<TParamEntry>{$ENDIF};
```

Sort 等使用比较回调的声明同步引用 TParamEntryComparison。旧名称删除属于明确的源码兼容变更；外部业务工程需要修改引用，所有相关工程必须全量重编，不能混用旧布局 DCU/PPU 或二进制。

## 4. 字节 Decode 的职责与接口

接口方向示意，尚未添加到源码：

```pascal
THttpHeader = class(TBaseParams)
public
  function Decode(const AData: Pointer; const ASize: Integer;
    AClear: Boolean = True): Boolean; overload;
  function Decode(const AEncodedParams: string;
    AClear: Boolean = True): Boolean; overload; override;
  function Encode: string; override;
  // 集合接口继续继承；原始值直接从公开 TParamEntry 读取。
end;
```

处理顺序：完整字段块字节 → 校验结构/字段名 → 取得各字段值字节范围 → 调用 TParamEntry 字节构造器 → 复制 raw 并用 TUtils.GetString 生成 Value。

1. 只接收完整字段区间，不含 request/status line 和 body，不负责增量接收。现有 Parser 与 multipart 状态机继续负责分包收集及容量上限。
2. D03-1 已选择 B：字段集合按 ASize 或字符串长度结束，最后一条字段可不带 CRLF，也可带 CRLF 或结束空行；字段之间严格使用 CRLF。输入若已经包含结束空行，其后仍有任何数据则失败，不能忽略尾部内容。server/client 从原始首个 CRLF 之后取切片；multipart 可传已收集的完整 part 头。是否带末尾结束空行不改变语法结果，不先将整块输入转换为字符串。
3. D03-2 已选择 A：精确遵守 ASize，不依赖 NUL 结尾；负长度或 nil/正长度属于调用参数错误，统一抛参数异常，并在清空集合、分配或读取数据前检查，原有容器不受这类错误影响。Decode(nil,0) 与 Decode(P,0) 均成功且不读取指针；AClear=True 得到空集合，False 不追加任何项。指针非空时由调用方保证指定范围可读。合法参数下的字段语法错误返回 False，其容器状态按 C16 处理；HTTP/1.1 Host 等由请求语义层处理。
4. 字段名使用 ASCII token 规则。D03-3 已选择 A：仅在字节层裁去值两侧 SP/HTAB，RawValue 与传入 TUtils.GetString 的范围一致；保留内部、引号内及其他字符的空白。结构性失败整块失败，不能跳过畸形 CL/TE 后继续把请求当作无正文。
5. 文本转换仅作用于已确定的字段值，使用 TUtils.GetString(RawValue)；编码检测、BOM 处理和默认回退均沿用现有函数。HasRawValue 不能被当成 UTF-8 合法性标记。
6. 不对整块 HTTP 头先做文本解码再拆结构。此前提出的独立严格 UTF-8 解码器不再实施，也不顺带修订 TUtils.IsUTF8；现有工具函数的非严格行为需要在测试记录中准确呈现，不能宣称新增严格 UTF-8 接收策略。
7. Boolean 表示整块字段语法有效，不再等价于 Count > 0。D04-1 已选择 A：参数合法但语法解析失败时，AClear=True 清除本次已解析字段、最终为空集合；AClear=False 仅回滚本次新增项，原有项、顺序及 RawValue/HasRawValue 不变。两种模式都不得留下半份新字段，文本和 raw 一同清理。此合同适用于 Header 的字节和字符串 Decode；参数错误仍按 C14 在操作开始前抛异常且不改集合。
8. D02 已选择 B：保留 Header 的 string Decode/Create(string)/Add(string) 入口，统一严格字段语法；Decode 返回整段是否有效，合法空集合返回 True，不再根据 Count 判断成功，不再跳过非法字段行后仍成功。Header 的 Create(string)/Add(string) 无 Boolean 返回值，解析失败时抛异常。
9. 字符串来源不持久化 RawValue；选择共用解析时应区分提交路径，字符串结果用字符串构造器，不经字节构造器生成一份 raw 后再丢弃。该决定不改变 TParamEntry 字节构造器采用 TUtils.GetString 的已定行为。
10. Header 便利入口的异常报告必须在经 TBaseParams 引用调用时同样成立，不能仅隐藏一个派生同名 Add；必要的封装或 virtual/override 调整应先列入接口清单。URL 参数、Cookie 等其他 TBaseParams 子类的解析合同不一并收紧。Add(string) 作为追加入口，解析失败按 C16 回滚新增项后抛异常，已有内容不变；Create(string) 失败抛异常，不返回部分初始化对象。

D03-1 已确认的输入示例（`\r\n` 表示实际 CRLF）：

| 输入字段集合 | 结果 |
| --- | --- |
| `X-Test: abc` | 成功 |
| `X-Test: abc\r\n` | 成功 |
| `X-Test: abc\r\n\r\n` | 成功 |
| `X-Test: abc\n` | 失败：裸 LF |
| `X-Test: abc\r\n\r\nExtra: data` | 失败：结束空行后存在额外数据 |

此项只定义完整字段区间的调用接口，不放宽 TCP 上的 HTTP 头结束规则。TCrossHttpParser 和 multipart 状态机仍先确认线上边界；THttpHeader.Decode 不承担从网络判断报文是否已经完整到达的职责。

## 5. 已确认的发送契约：完整 Header 文本统一 UTF-8

客户端请求头和服务端响应头继续先组合 Unicode 字符串，在最终输出点统一转换为 UTF-8 字节，不加 BOM，实际发送长度使用字节数组长度。

```pascal
LHeaderStr := LFirstLine + CRLF + FHeader.Encode;
Result := TEncoding.UTF8.GetBytes(LHeaderStr);
```

以上仅示意最终编码原则；实际代码还包括独立生成的 Set-Cookie 等内容。

- `Encode: string` 输出当前 Value 对应的字段文本，不优先使用 RawValue。
- RawValue 用于接收证据、诊断和显式重新解码；默认发送不做原始值透传，不根据原始输入编码选择输出字符集。
- 字符串直接写入时不提前编码、不缓存一份发送字节；仅在真正构建消息时编码。
- URI、下载文件名参数、Cookie 等先按自身规则格式化，再统一 UTF-8。格式化后的 ASCII 内容在 UTF-8 下字节不变；百分号序列不会因为 GetBytes 再次转义或解码。
- 接收非 UTF-8 字节后，如果字符串视图只是回退表示，直接再发将得到这个当前字符串的 UTF-8 字节，并非原始输入；需要恢复实际文本时由明确编码处理后重新赋值。
- 新普通文本字段直接使用 UTF-8，是本库选择的发送策略；具体标准字段仍须符合其语法，对端如何解释自定义非 ASCII 文本取决于约定。
- D06-1 已选择 B：Encode 遇到非法字段名或字段值中的 CR/LF/NUL 等不允许的控制字符，拒绝整块输出并抛异常，不再 Continue 静默过滤。正常中文等非 ASCII 文本不因本项被拒绝，仍按统一 UTF-8 策略输出。
- D06-2 已选择 A：最终 UTF-8 编码前校验完整 Header 文本的 UTF-16 合法性，包括字段、请求/响应首行、状态描述和独立拼接内容。合法代理项对正常编码；孤立高/低代理项或错误配对使本次输出失败并抛异常，不自动替换为 U+FFFD，也不输出本次部分数据。
- Unicode 输出校验不修改容器、不提前到普通字符串赋值时执行；正常 U+FFFD 字符本身合法。Delphi/FPC 必须给出一致的拒绝结果，不依赖不同 RTL 的默认替换行为；接收侧仍使用现有 TUtils.GetString，不因此新增接收端严格 UTF-8 策略。
- 输出失败不删除或改写容器里的字段，调用者可修正后重试；不自动替换控制字符，也不顺带改变普通 Add/字符串赋值的校验时机。
- 客户端和服务端发送入口必须正确处理 Header 生成失败，按其现有发送 API 报告失败，不将本次部分 Header 写到网络，避免异常导致队列、资源或回调悬置。此项是待实施验证的要求，不表示现有路径已满足；独立 Set-Cookie 等拼接路径也需核对是否绕过统一输出失败合同，不扩大为 Cookie 协议重写。

本轮不增加逐字段混合编码、优先发送原始字节的 EncodeBytes，也不采用“默认严格 ASCII、UTF-8 必须显式开启”的旧建议。原始值存储不会自动改变发送策略。

## 6. 三个网络入口与容易遗漏的配套位置

### 6.1 必须修改或核对的实施项

| 编号 | 文件与当前行号 | 工作 |
| --- | --- | --- |
| H01 | Net.CrossHttpParams.pas:40、45、52、64、273 及其调用方 | 保持继承；原公开记录直接扩展为 TParamEntry，按 C24 同步重命名三个比较器/回调类型且不保留旧名别名；统一列表、Items、枚举、Sort 等引用，不新增双层记录或原始值查询接口 |
| H02 | Net.CrossHttpParams.pas:1073、1086、1098、1103、1201、1246、1251、1266；1303、1543、1637 等 | 统一基类 Add/Assign/Clear/Remove/SetItem/SetParam/Sort 的原始值生命周期；适配同单元其他参数类 Decode 对 FParams 的直接访问，不能依赖派生隐藏方法 |
| H03 | Net.CrossHttpParams.pas:1061、1081、1378、1495 | 新增字节 Decode；保留 Encode:string 但非法字段改为抛异常并拒绝整块输出；Header 字符串解析严格且不生成持久 raw，Decode 返回有效性、Create/Add 失败抛异常；不改变其他参数类合同 |
| H04 | Net.CrossHttpServer.pas:119、121、170、1851、1898、1945、4828、4878、4897、4913 | RawRequestText 字段/getter/接口与类属性替换为 RawRequestHeader:TBytes；按 C25 更换 ICrossHttpRequest GUID；复制原始头快照，按字节拆首行并解码字段；检查 Decode 失败 |
| H05 | Net.CrossHttpClient.pas:3380、3393、3403、3422 | 按字节拆响应状态行，截取 reason phrase 后用 TUtils.GetString 生成 StatusText；字段区间传新 Decode；空头、无正文设置和失败时序分别核对 |
| H06 | Net.CrossHttpParams.pas:2323、2333、2538 | multipart helper 改 Pointer+Size；去掉提前 GetString，统一结束符范围；失败返回 frFailed 并保留流/文件清理 |
| H07 | Net.CrossHttpServer.pas:5760、5770、5772；Net.CrossHttpClient.pas:2709、2714 | 两端完整 Header 输出改为无 BOM UTF-8，编码前检查完整 Unicode 文本并拒绝无效 UTF-16 序列；首行、独立 Set-Cookie 等组合路径一并核对，长度用字节数，失败不输出部分数据 |
| H08 | Net.CrossHttpParams.pas 新 TParamEntry 构造器；Utils.Utils.pas:329、343、352 | 增加字节构造器，复制数据并调用现有 TUtils.GetString(TBytes)；Utils 单元不在本次修改范围内 |
| H09 | Net.CrossHttpClient.pas:3255、3428；HttpHeadTests.lpr:96；Issue199Tests/Issue199Server.lpr:169 | 验证基类引用、Assign、for-in 和重复字段；迁移公开记录类型名及 RawRequestText 使用处，测试改按原始字节检查 |

### 6.2 已确认的原始请求头定义与首行边界

当前 RawRequestText 实际只保存请求行、全部请求头及结束空行，不含正文。用户已确认保持这个范围，以 TBytes 直接保存，并使用准确名称 RawRequestHeader。以下只列接口和类中变化的成员：

```pascal
ICrossHttpRequest = interface
  function GetRawRequestHeader: TBytes;
  // 原始请求行、请求头及结束空行，不包含正文。
  property RawRequestHeader: TBytes read GetRawRequestHeader;
end;

TCrossHttpRequest = class(TInterfacedObject, ICrossHttpRequest)
private
  FRawRequestHeader: TBytes;
protected
  function GetRawRequestHeader: TBytes;
public
  property RawRequestHeader: TBytes read GetRawRequestHeader;
end;
```

- 替换 FRawRequestText、GetRawRequestText 和接口/类上的 RawRequestText，类型由 string 改为 TBytes，不再保留旧的诊断字符串方案。
- 直接复制 ParseHeader 收到的数据块，任何文本解码前保存完整快照；不先转 string 再重编码，不借用 Parser 缓冲区。
- 快照表示收到时的请求头。后续修改 Request.Header、删除字段或重新编码输出，都不反向修改 FRawRequestHeader。
- GetRawRequestHeader 返回字节副本，避免调用者修改内部快照。它与可变公开 TParamEntry 中 RawValue 的读写合同分开定义。
- 完整快照包含首行与空行；Items[I].RawValue 只关联单个字段值，通过集合字符串赋值路径修改时使对应 raw 失效。公开 record 和 Items 回写按 C23 整体接纳、由调用者维护一致性；两者生命周期不能混同。
- 这是 ICrossHttpRequest 的公开 getter 返回类型及属性名变更。D09-2 已选择 A：保持接口名，在实施时生成新 GUID，替换旧 `{B26B7E7B-6B24-4D86-AB58-EBC20722CFDD}`；不保留旧接口定义、旧 GUID 支持或适配器，不顺带更换其他未变更合同接口的 GUID。
- 所有实现和调用方必须迁移并全量重编，不承诺旧二进制兼容。新 GUID 使 QueryInterface/Supports/as 等按新标识查询时能够区分新旧合同，但不能替代依赖重编，也不能保证任意绕过接口查询的旧二进制可以安全混用。实施已生成并使用新 GUID `{6F42E391-5B3B-41F6-9A75-8C03312E8669}`，旧 GUID 不再由实现支持。
- 不扩展为包含正文的 RawRequestData，也不复制上传文件正文。原始 Header 快照和正文流保持各自职责。
- D05-1 已选择 A：RequestCmdLine 继续为 string，由原始请求首行（不含末尾 CRLF）通过 TUtils.GetString 生成；不执行 URL 百分号解码。Method、request-target、Version 的边界先按字节确定，不再从转换后的 RequestCmdLine 重新定位结构。
- D05-2 已选择 A：保留 `FRawPathAndQuery` 和公开路径对未百分号编码的非 ASCII 目标的兼容。先在原始或尚未百分号解码的表示中识别结构分隔，已确定的文本片段用 TUtils.GetString 转换，再沿用 URL 解码；不能让字符转换改变方法/目标/版本及路径/查询的边界，也不能先解码整个查询后再把原值中的转义分隔符误当成参数分隔。
- 标准百分号形式与直接 UTF-8 中文目标都接受；百分号只解码一次，例如 `%252F` 得到 `%2F`，不继续变成 `/`。RequestCmdLine 按 C18 保留百分号文本，业务 Path 则是对应解码结果。
- `UrlDecode`（Utils:1823）先把 string 编为 UTF-8，因此不能将原始 UTF-8 的一字节映射直接传入期望恢复中文。其他非 ASCII 字节沿用 TUtils.GetString 的检测/回退，不新增编码猜测承诺；直接非 ASCII 目标是兼容策略，不表示它是标准 URI 表示。
- 不因兼容非 ASCII 就放宽空格、CR/LF、NUL 等结构性限制；本项不顺带改变 `+`、非法 `%xx`、路径规范化等既有行为，不扩展为 URL 处理整体重写。
- Client `FRawResponseHeader`（1178）是私有解析字段，可去掉其结构解析职责；D05-1 已确认 StatusText（3422）继续为 string，在原始状态行中确定版本、状态码和 reason phrase 区间后，仅用 TUtils.GetString 转换描述部分，不含末尾 CRLF。版本/状态码的结构处理不依赖解码后的描述字符串。
- 三个入口在调用新的 Header.Decode 之前都不能先将整个原始块转换成 string。

### 6.3 保留实现、以回归为主的部分

- `TCrossHttpParser`（54、417、932）已经提供 Pointer+Size，继续负责字节边界、容量及正文状态机，无需整体重写。
- CL/TE（Parser:935、949）仍通过 GetHeaderValues 读取全部重复字段；不要改成单值查询或自动合并。
- trailer（Parser:610）直接按字节验证且不存字段，不是遗漏的 Header.Decode 调用者。
- 认证中间件（Net.CrossHttpMiddleware.pas:153）检查重复 Authorization；WebSocket server/client（1073/1324）依赖重复值和 token 查询，主要做回归。
- Server ParseHeader=False 已进入 Fail(400)、psFailed、Connection: close；Client 已有失败/关闭回调。预期非法输入应按策略回退或返回 False，不应因普通解码异常误走 Parser 的 500 分支。
- Parser 在完整请求通知前可能 Reset 头缓冲（1045），是 RawValue 必须自行持有字节的依据。
- URL 参数、Cookie 等继续继承 TBaseParams，协议实现不重构，但基类存储变更需要覆盖其基本容器行为。

同单元内 `THttpUrlParams`、`TDelimitParams`、`TRequestCookies` 的 Decode 也会直接访问或清空 FParams。实施时必须逐处核对这些访问并适配重命名的记录；其字符串来源不自动生成 raw，公开完整记录写入遵循 C23 保留传入状态。不能只修改基类公开方法后就认为全部路径已覆盖。

## 7. 既有集合语义与兼容约束

- 原 TNameValue 直接改为公开 TParamEntry；Items、Add(record)、枚举器和比较器统一使用它。保留默认 Params[name]、Count、Clear、Sort、GetParamValue、GetHeaderValues、ExistsParam、ContainsToken 等接口职责；名称/元素类型变更需迁移调用方。
- Add(record) 永远追加；Add(name,value,True) 保留重复；默认 Add/索引器赋值只更新第一条同名项。
- GetParamValue/默认索引器取第一条；GetHeaderValues 返回全部同名值，保留顺序和空值，缺失时清空 out。
- Remove(name) 删除全部同名项，大小写不敏感，其他项相对顺序不变；Remove(index) 只删一项。
- ContainsToken 保留跨全部同名字段的完整 token 匹配及 ASCII 比较；不能改为子串匹配。旧实现中空白裁剪行为若收紧，需单独说明并更新差分参考。
- Set-Cookie 不自动逗号合并；Cookie 的分号合并仍由调用层负责。
- 继续支持 Assign(TBaseParams) 及经基类引用的调用，按 C23 的完整记录复制规则保留元数据，并按 C22 共享原始数组；不增加按对象类型剥离元数据的隐藏规则，避免另加不必要的同名重载。D04-2 已选择 A：Assign(Self) 在 Clear 之前直接返回，字段、顺序、文本、RawValue 和标记都不变，不重新复制数据；TBaseParams 的其他子类同样受此自赋值保护。Assign(nil) 仍清空。
- Sort 的比较函数同步接受 TParamEntry，排序必须移动整个记录，不能只移动 Name/Value。默认排序不借机改变规则。
- 基类新增数据不能让普通字符串写入分配 RawValue；保留既有 GetHeaderValues 缺失查询无临时数组等行为。原始值确实增加 Header 的字节存储成本，需单独报告，不能宣称本次是性能优化。

## 8. RFC 与成熟实现提供的设计边界

- [RFC 9112 §2.2、§5](https://www.rfc-editor.org/rfc/rfc9112.html#section-2.2)：消息及字段结构按字节解析。字符串解释放在字段边界确定之后。
- [RFC 9110 §5.5](https://www.rfc-editor.org/rfc/rfc9110.html#section-5.5)：通用字段值允许 obs-text 高位字节，并不赋予所有字段统一的 UTF-8 字符集。本库复用 TUtils.GetString 的现有编码检测与回退，并统一使用 UTF-8 输出，这是明确的接口/互操作策略。
- [RFC 8187 §3.2.1](https://www.rfc-editor.org/rfc/rfc8187.html#section-3.2.1)、[RFC 6266 §4.3](https://www.rfc-editor.org/rfc/rfc6266.html#section-4.3)：扩展参数需由具体字段定义采用；下载文件名可用 filename*，最终 ASCII 格式化文本经 UTF-8 输出字节不变。不能把所有 Header 自动套成百分号编码。
- [RFC 7578 §4.2](https://www.rfc-editor.org/rfc/rfc7578.html#section-4.2)：multipart 上传 part 的 filename 与下载响应的 filename* 不同，上传禁止使用该扩展参数形式；有原始 UTF-8 和可选百分号表示的互操作历史。
- [Fetch FormData 编码](https://fetch.spec.whatwg.org/#concept-bodyinit-extract)、[HTML multipart 算法](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#multipart-form-data)：Fetch 的 FormData 使用 UTF-8；字段名/文件名只对 CR、LF、双引号作规定转义。此处用于界定参数处理，未授权顺带重写全部 multipart 参数解析。
- [RFC 3629 §4](https://www.rfc-editor.org/rfc/rfc3629.html#section-4)：描述现代严格 UTF-8 的字节范围。当前用户选择复用 TUtils.GetString，而不是本轮新增严格验证；该规范保留作行为边界参考，不能据此声称现有 IsUTF8 已完全符合或扩大修改范围。

已查官方源码：Nginx 1.28.0、Apache httpd 2.4.65、Go `fac60caa`、Node `ba3c34a9`/llhttp `69936dbc`、Kestrel `ceb0eec4`。共同点是先确定字节结构，具体容错与文本策略不完全一致。Node 展示一字节映射的字符串接口先例，Kestrel 展示字段边界确定后严格 UTF-8 解码的先例；不把任何一个实现的全部宽松规则直接复制为本库策略。

本项目正常业务回调期望完整正文；这不是要求所有 HTTP 实现必须先缓存正文。其他服务器可以采用流式 Body 接口，连接复用前仍需保证定界与正文处理正确。

## 9. 逐项决策汇总（全部已确认）

原待确认清单已经逐条收口，当前没有未选择的设计分支。D01 在逐项确认之前已由用户指定复用 TUtils.GetString，因此不再单独选择自定义回退策略；其余选择如下。

| 编号 | 用户选择 | 已确认行为 |
| --- | --- | --- |
| D01 | 直接复用现有工具函数 | 字节构造器调用 TUtils.GetString，不新增严格 UTF-8 接收助手 |
| D02 | B | 保留字符串入口，统一严格字段语法；Decode 返回有效性，Create/Add(string) 失败抛异常 |
| D03-1 | B | 以长度确定字段集合，允许省略末尾结束空行，拒绝裸 LF 和结束空行后的额外数据 |
| D03-2 | A | 空输入有效，非法参数在状态修改前统一抛参数异常 |
| D03-3 | A | RawValue 取去除最外侧 SP/HTAB 后的字节；由 Header.Decode 截取，构造器不 Trim |
| D04-1 | A | 替换模式解析失败清空，追加模式仅回滚本次新增项 |
| D04-2 | A | Assign(Self) 不操作，Assign(nil) 仍清空 |
| D05-1 | A | 字节边界确定后，用 GetString 生成 RequestCmdLine/StatusText；请求行文本不解码百分号 |
| D05-2 | A | 保留非 ASCII request-target 兼容，按既有工具转换文本，百分号只解码一次 |
| D06-1 | B | Encode 遇到非法字段抛异常，拒绝整块输出，不静默过滤或修改容器 |
| D06-2 | A | 完整 Header 文本含无效 UTF-16 时在输出前抛异常，不替换、不发送部分数据 |
| D07 | B | 公开 record 的 RawValue 沿用 TBytes 共享语义，约定只读；需要修改时自行 Copy |
| D08 | A | Add(record)/Items 整体接纳四字段，不自动纠正，调用方负责一致性 |
| D09-1 | A | 四个类型统一使用 TParamEntry 系列新名称，不保留旧名别名 |
| D09-2 | A | ICrossHttpRequest 保持名字、更换 GUID，不提供旧接口适配层，全部依赖重编 |

方法 token、Host、完整版本语法等相邻加固可独立列项。新增字节入口并不自动授权全面改动这些协议行为。

## 10. 实施后的验证设计

以下为验收设计；已执行的目标、结果与明确边界见第 14 节。

1. **类型与容器兼容**：公开 TParamEntry、两个构造器、Items/枚举/比较器的引用同步迁移；有序重复项、第一条 setter、Add(record) 与默认 Add 的差异、全量删除、Sort、自复制/nil；直接 Header 引用与 TBaseParams 引用结果相同；普通 URL/Cookie 容器原有语义不变。
   按 C24 核对源码中的四个旧类型名已迁移到新名称，不提供兼容别名；编译覆盖比较器、回调、构造器、枚举和外部声明形状，并要求全部依赖重新构建。
   按 C25 验证 ICrossHttpRequest 新 GUID 可查询、旧 GUID 不再被该实现支持，原 RawRequestText 名称迁移为 RawRequestHeader:TBytes；核对无其他未改变合同的接口 GUID 被顺带更换。不以新 GUID 查询通过冒充旧二进制兼容。
   按 C17 覆盖空/非空集合及带 raw 的重复字段自赋值，全部内容和顺序不变；经 TBaseParams 引用自赋值同样不操作。Assign(nil) 则清空，不能混同。
   Header 字符串入口另测合法空集合 True、混入非法字段行时 Decode=False、Create(string)/Add(string) 抛异常，以及经 TBaseParams 引用调用时行为相同；字符串来源均无 raw。按 C16 验证替换失败后为空、追加失败保留原有项与原始来源，均无本次部分结果。
2. **原始值生命周期**：字节构造器/Decode 同时产生文本/raw；字符串构造及赋值不合成 raw，字符串覆盖使对应项失效而其他项保留；原始空值与无来源用 HasRawValue 区分；重复名称、删除、排序、Assign 后对应关系正确。按 C23 验证 Add(record)/Items 回写完整保留四字段、不自动修正；使用字符串构造器重建后再写入则没有 raw；调用方手动修改记录须自行维护一致性，不断言字段公开赋值会自动更新元数据。不调用已取消的查询 API。
3. **所有权和共享**：字节构造输入 buffer 覆盖/释放后仍可读取；按 C22 验证 Items、枚举和适用复制路径的 RawValue 引用共享，不断言普通记录读取会深拷贝；显式 Copy 后修改独立数组不影响来源。复制对象/记录、释放源持有者后其余有效引用仍可读取；容器字符串替换、失败回滚、Clear 不原地破坏其他持有者的原始数组。RawRequestHeader getter 的副本隔离仍单独按 C11 验收。
4. **原始字段语法与参数边界**：合法空块、空值、NUL/CTL、非法名称/冒号/CRLF；覆盖 C13 的三种合法末尾形式，以及裸 LF/结束空行后额外数据失败。按 C14 验证 nil/0 和 P/0 成功、零长度不读取指针、负长度及 nil/正长度抛参数异常且不改变已有集合；合法缓冲中的语法错误仍返回 False，不得静默删掉 CL/TE，容器状态按 C16 验证。混合有效字段后遇到错误时，必须同时清理本次文本与 raw。
   按 C15 验证 Header.Decode 去掉值最外侧 SP/HTAB 后保存 raw，保留内部和引号内空白；全 OWS 字段仍有空的原始来源。单独调用 TParamEntry 字节构造器时不自动裁剪，完整请求头快照保留原样空白。
5. **现有文本转换行为**：ASCII、UTF-8 中文、四字节字符、空数组、BOM、非 UTF-8、截断及异常字节；字节构造器 Value 与 TUtils.GetString 对同一副本的结果一致，RawValue 始终准确。记录当前 helper 对非法/旧 UTF-8 序列的实际行为，不写成新增严格验证通过；混合字段转换不影响原始结构和相邻字段。
6. **#199 与原始请求头快照**：原始整包/头体分开均收到 165 字节正文，Header=10，下一请求为正常 GET；Referer 文本和原始值准确；RawRequestHeader 与原报文前 700 字节逐字节相同，不含正文；修改解析后 Header、复用 Parser 缓冲、后续请求都不改变快照；修改 getter 返回副本不影响内部。
7. **Client**：非 ASCII 响应字段、空头、多个 Set-Cookie、非法头失败、连接复用、HEAD/1xx/204/304、101 升级；保持无正文设置时序。
   按 C18 核对 RequestCmdLine/StatusText 与相应原始区间调用 TUtils.GetString 的结果一致；RequestCmdLine 中 `%E4%B8%AD` 等序列保持原文本，转换不改变字节边界或 RawRequestHeader。按 C19 覆盖 ASCII、标准百分号路径、直接 UTF-8 路径、混合文本、路径/查询分隔与转义分隔符；`%252F` 只解一次，不出现二次编码/解码。其他原始非 ASCII 文本与 GetString 行为一致；本项不改变既有加号/非法百分号/规范化规则。
8. **Multipart**：UTF-8 中文 filename 与原始参数字节、part 头结束符拆包、失败时流/临时文件清理；文件内容二进制字节不变。完整 quoted-string/百分号/filename* 参数兼容另行界定。
9. **出站 UTF-8**：服务端响应和客户端请求两端真实抓取字节；Unicode 值正确编码、无 BOM、长度为字节数；ASCII 字段、URI 百分号和 filename* ASCII 格式不变；已格式化内容不二次转义；含 raw 的项仍从当前 Value 统一 UTF-8 输出，raw 不参与默认发送；Set-Cookie 独立拼接路径也覆盖。
   按 C20 验证合法字段混入非法名称/CR/LF/NUL 时 Encode 抛异常、容器保持原状；发送入口明确失败且没有本次部分 Header 输出。修正字段后可重试；普通字符串写入不因本项提前抛异常。原 WebSocket `filtered-protocol` 等依赖静默过滤的用例应迁移为明确失败预期，并检查无握手误成功、回调遗漏或挂起。
   按 C21 覆盖孤立高代理项、孤立低代理项、错误配对在客户端/服务端输出前均抛异常且不发送部分 Header；正常中文、合法四字节字符对应的代理项对及 U+FFFD 正常输出。检查首行/状态描述和其他独立拼接部分，不只检查 FHeader；原容器内容保持不变，不自动替换坏序列。
10. **相关回归**：HttpHotPathTests、HttpHeadTests、HttpFailureTests、HttpChunkTests、HttpAuthTests、HttpRequestLifetimeTests、MultipartStreamTests、WebSocketFrameTests/Loopback；按影响选择，不仅检查 #199。
11. **构建与资源边界**：Delphi Win32/Win64 与 FPC Win64 顺序构建；涉及跨平台字符串/字节差异时，使用已有 Linux 工具链补实际验证。检查基础容器新增元数据成本、原始值分配与复制次数，不把双存储包装成性能提升。Pascal 保持 UTF-8 BOM，Windows 脚本保持 CRLF。

## 11. 用户提供的编译参数参考

2026-10-27 用户指出生成文件偏大，并提供以下两份脚本作为参考：

- `D:/Project/太安厅/智夫子重构版/自考.批量自动编译/Build(delphi).bat`：`-$C- -$D+ -$I- -$L+ -$O+ -$Q- -$R- -$Y+`，并带 RELEASE 等工程条件定义。
- `D:/Project/太安厅/智夫子重构版/自考.批量自动编译/Build(fpc).bat`：Release 使用 `-O3 -CX -Xs -XX`，Debug 使用 `-g -gl -gv`。

该轮仅完成脚本、当前构建命令和 EXE 体积读取，随后用户转回 Header 设计；没有修改本仓库编译参数，没有启动重编译。外部业务脚本只作参考，未运行，也不复制其中清理、打包或业务工程路径。

后续构建优化应按本测试目标选择适用参数，区分 Release 与 Checked/诊断用途，不盲目引入 madExcept、关闭所有检查或删除调试资料。Delphi 参考脚本保留 D+/L+/Y+，不能称为关闭调试信息。FPC 智能链接与符号剥离的体积效果需要同源码实测；O3 也不直接等同于最小体积或一定更快。

前序 `bin/final` 中 Issue199Server.exe 体积：Delphi Win64 3,396,608 字节，Win32 2,288,640 字节，FPC Win64 3,418,624 字节。仅为旧构建基线，不是新参数验证结果。

## 12. 实施顺序和当前状态

- [x] 完成 #199 原始报文复现、RFC/成熟实现研究及影响分析。
- [x] 确认保留继承、原公开记录直接扩展/重命名为 TParamEntry、字符串写入不生成 raw、集合字符串赋值路径修改使对应 raw 失效；完整 record 按 C23 接纳。
- [x] 确认字符串和字节两个构造器，字节构造器保存副本并直接复用 TUtils.GetString；不新增 TryGetRawValue 或严格 UTF-8 解码器。
- [x] 确认服务端 FRawRequestHeader/GetRawRequestHeader/RawRequestHeader:TBytes，范围为请求行＋头＋空行，不含正文，快照独立且 getter 返回副本。
- [x] 确认原始值不参与默认发送，完整 Header 文本最终统一无 BOM UTF-8。
- [x] D02 用户选择 B：Header 字符串入口保留并统一严格语法；Decode 返回有效性，Create/Add 解析失败抛异常；其他参数类合同保持。
- [x] D03-1 用户选择 B：字段集合按指定长度解析，可省略末尾结束空行，字段间仍使用 CRLF，结束空行后额外数据失败；线上报文边界由上层保证。
- [x] D03-2 用户选择 A：空输入有效；负长度或 nil/正长度统一抛参数异常，所有检查早于状态修改；字段内容语法错误仍按 Decode=False 处理。
- [x] D03-3 用户选择 A：RawValue 去除最外侧 SP/HTAB，由 Header.Decode 在字节层截取；构造器不 Trim，其他空白保留，完整头快照不变。
- [x] D04-1 用户选择 A：参数合法但解析失败时，替换模式清空，追加模式回滚本次新增项；不保留半份结果，原有追加目标及其 raw 不变。
- [x] D04-2 用户选择 A：Assign(Self) 在 Clear 前直接返回，所有数据不变；基类引用行为一致，Assign(nil) 保持清空。
- [x] D05-1 用户选择 A：先按字节确定首行/各段边界，再用 TUtils.GetString 生成 RequestCmdLine/StatusText；RequestCmdLine 不解码百分号，RawRequestHeader 不受影响。
- [x] D05-2 用户选择 A：保留非 ASCII 请求目标兼容，先识别结构、后用 GetString 转换相应文本，百分号只解码一次；不扩大 URL 行为变更范围。
- [x] D06-1 用户选择 B：非法出站字段使 Encode 抛异常并拒绝整块输出，不静默过滤、不修改容器；发送入口需正确报告失败。
- [x] D06-2 用户选择 A：完整 Header 的无效 UTF-16 序列在最终 UTF-8 编码前抛异常并拒绝发送，不自动替换；合法代理项对与 U+FFFD 正常处理，接收策略不变。
- [x] D07 用户选择 B：公开记录中的 RawValue 沿用 TBytes 共享语义，调用方按只读约定使用、修改前自行 Copy；字节构造和 RawRequestHeader getter 的副本规则不变。
- [x] D08 用户选择 A：Add(record)/Items 整体保留记录四字段，raw 共享，不重新解码或自动纠正；调用方保证记录内部一致性，字符串入口仍使对应 raw 失效。
- [x] D09-1 用户选择 A：公开记录和三个比较器/回调类型统一改名，不保留旧名称别名，源码调用方同步迁移并全量重编。
- [x] D09-2 用户选择 A：ICrossHttpRequest 实施时更换 GUID，保持接口名，不保留旧接口或适配器；其他未变更接口标识保持。
- [x] 全部逐项决策已确认，正文、影响清单、接口定义和验收要求已经同步；编译参数参考已用于本次新增/修改的测试 runner，效果按同实现对照记录。
- [x] 用户明确开始实施；以 4815903 为起点，保留已有 GetString 优化及 #199 历史证据。
- [x] H01/H02：公开记录、泛型引用与统一基类写路径迁移，按已确认的记录写入规则验证原始值生命周期。
- [x] H03/H08：字节 Decode、两个记录构造器和 TUtils.GetString 复用，处理 string 兼容入口。
- [x] H04/H05/H06：迁移三个入口及服务端原始头属性、首行拆分、所有权和失败路径。
- [x] H07/H09：统一 UTF-8 出站、复制/枚举及依赖调用者验证。
- [x] 按已确认构建策略完成目标和相邻回归，保存日志/哈希/体积结果；另按用户授权处理提交。

## 13. 风险与回退

主要风险是公开记录布局/命名和 ICrossHttpRequest 返回类型变更、基类修改影响其他参数容器、公开记录中原始字节与文本不同步、可变数组别名、旧 Decode 语义变化、现有文本检测的回退差异、请求路径二次编码和对端对非 ASCII 出站值的解释差异。实施应分可审查批次，保留原始测试证据，不通过破坏性 Git 操作覆盖用户改动。源码实施、编译配置修改和提交均尚未执行。

## 14. 变更记录

以下按时间保留设计演进；其中“待确认”等表述仅描述当时状态。当前有效结论以第 1 节和第 9 节的已确认决策为准。

- 2026-10-26：创建初稿，比较字节 Decode 重载、独立 Header 类及公共容器基类，整理三个主入口和相邻影响。
- 2026-10-27（早期版本，结构部分已被下述修订替代）：主方案改为保留 TBaseParams 继承；当时提出内部扩展项和公开字符串视图，后被用户纠正。字符串写入不生成 raw 且使旧 raw 失效的约束继续有效。
- 2026-10-27：按用户最新确认，保留 Encode:string，客户端和服务端最终整块 Header 统一 UTF-8；原始值只供来源信息/重新解码，不走默认输出。独立类、默认严格 ASCII 和原始值优先 EncodeBytes 不再作为当前实施方向。
- 2026-10-27：更新影响清单、基类修改风险、双存储/出站验收，并补记尚未应用的编译脚本参考；未将未确认细节标为已决定，未修改实现代码。
- 2026-10-27（本次修订）：按用户接口纠正，直接扩展原公开记录并重命名 TParamEntry，Items/枚举使用同一结构；取消公开/内部双层视图和 TryGetRawValue。
- 2026-10-27（本次修订）：新增字符串、原始指针两个 record 构造器；字节构造器复制 raw 后直接调用 TUtils.GetString，取消新增严格 UTF-8 helper 的计划。Add(record)/Items 如何接纳带来源的公开记录仍待确认。
- 2026-10-27（本次修订）：按用户确认将服务端 RawRequestText:string 替换为 RawRequestHeader:TBytes，并同步字段/getter/接口与类属性；明确完整请求头快照不含正文、不随 Header 修改，getter 返回副本。更新迁移、兼容和验收清单，仅改草案。
- 2026-10-27（逐项确认 D02）：用户选择 B，确定保留 Header 字符串入口并采用统一严格结构语法；合法空集合成功、非法行整次失败，Create(string)/Add(string) 解析失败抛异常。待确认表移除 D02，字段块边界和失败后容器状态继续分别由 D03/D04 决定。仅更新文档。
- 2026-10-27（逐项确认 D03-1）：用户选择 B，允许字段集合省略末尾结束空行，按显式长度解析；仍拒绝裸 LF 和结束空行后的额外数据。D03 拆为已确认的结束符规则及待确认 D03-2 参数边界、D03-3 原始空白范围；只更新草案。
- 2026-10-27（逐项确认 D03-2）：用户选择 A，确定零长度空输入有效且不读取指针；负长度或 nil/正长度在状态修改前统一抛参数异常。待确认表移除 D03-2，合法缓冲中的语法错误仍与参数错误区分，失败容器状态继续由 D04 决定。仅更新文档。
- 2026-10-27（逐项确认 D03-3）：用户选择 A，RawValue 保存由 Header.Decode 去除值两侧 SP/HTAB 后的字节，构造器不作 Trim，完整原始空白由 RawRequestHeader 快照保留。D03 全部确认；D04 拆为 D04-1 解析失败状态和 D04-2 自赋值逐条讨论。仅更新文档。
- 2026-10-27（逐项确认 D04-1）：用户选择 A，确定 Header 字节/字符串 Decode 在语法失败时，替换模式最终为空、追加模式仅回滚本次新增项；Add(string) 回滚后抛异常。待确认表移除 D04-1，参数错误仍按 C14 保持原有状态，自赋值继续由 D04-2 决定。仅更新草案。
- 2026-10-27（逐项确认 D04-2）：用户选择 A，确定 Assign(Self) 在基类清空前直接返回，不改变或重新复制任何项及其原始来源；Assign(nil) 仍清空。D04 全部确认；D05 拆为首行文本表示和 request-target 接受/解码规则，避免将两件事一起隐含决定。仅更新文档。
- 2026-10-27（逐项确认 D05-1）：用户选择 A，确定 RequestCmdLine 和 StatusText 在字节边界确定后使用 TUtils.GetString 转换；请求行文本不执行百分号解码，不再驱动结构解析。待确认表移除 D05-1，实际 request-target 的接受与路径/查询解码仍由 D05-2 决定。仅更新草案。
- 2026-10-27（逐项确认 D05-2）：用户选择 A，确定继续兼容未百分号编码的非 ASCII request-target；文本用 TUtils.GetString、百分号仅解码一次，并保持结构边界及既有 URL 特殊处理。D05 全部确认；D06 拆为非法字段输出和非法 Unicode 输出两项逐条讨论。仅更新文档，工作区已有源码和测试改动未触碰。
- 2026-10-27（逐项确认 D06-1）：用户选择 B，确定 Encode 遇到非法名称/不允许控制字符时拒绝整块输出并抛异常，保留容器内容，不自动替换或静默删除。补充发送失败及既有过滤用例迁移验收，待确认表移除 D06-1；Unicode 序列合法性继续由 D06-2 决定。仅更新草案。
- 2026-10-27（逐项确认 D06-2）：用户选择 A，确定发送前校验完整 Header 文本，孤立或错误配对的 UTF-16 代理项导致异常并拒绝本次发送，不替换、不修改容器、不输出部分 Header。正常代理项对与 U+FFFD 合法，接收端 GetString 决策不变。D06 全部确认，仅更新文档。
- 2026-10-27（逐项确认 D07）：用户选择 B，确定公开 TParamEntry 中 RawValue 采用正常动态数组共享语义，不在读取/接纳/适用复制边界自动深拷贝；调用方按只读约定使用，需要修改时自行 Copy。输入字节构造的自有副本与 RawRequestHeader getter 的副本合同保持，原始元数据是否被记录写入保留继续由 D08 决定。仅更新草案。
- 2026-10-27（逐项确认 D08）：用户选择 A，确定 Add(TParamEntry)/Items 赋值整体接纳记录，保留 Name/Value/RawValue/HasRawValue，数组按 D07 共享；不重新解码或自动修正，调用方负责记录一致性。字符串入口来源失效规则保持，完整记录复制保留原始元数据。D08 已确认，D09 拆为类型命名/别名和接口版本两项。仅更新文档。
- 2026-10-27（逐项确认 D09-1）：用户选择 A，确定 TParamEntry、IParamEntryComparer、TParamEntryComparison、TParamEntryComparer 为统一名称，删除旧名称且不提供别名；更新声明、调用迁移及全量重编验收。待确认表仅保留 D09-2 接口标识/版本。仅更新草案。
- 2026-10-27（逐项确认 D09-2 与设计收口）：用户选择 A，确定 ICrossHttpRequest 保持接口名、更换 GUID，不提供旧接口或适配器。全部待定项已确认，第 9 节改为决策汇总，补全接口查询与迁移验收；区分历史复现基线 2318fb0 和当前工作区 4815903。仅修改设计文档，不执行源码修改、编译或提交。
- 2026-10-27（开始实施）：用户明确授权实施。执行 H01～H09，先保存当前基线的 #199 红色结果，新增容器/字节解码/输出专项测试；本次新增或修改的测试构建参考用户 Release 参数，另保留 Checked 验证。已有 GetString 优化不修改，旧日志和复现数据不覆盖。


## 14. 2026-10-27 实施与验收结果

- H01/H02：TParamEntry 直接扩展原记录；完整记录写入/共享语义保持，字符串 setter 清除被覆盖项 raw；Assign(Self) 提前返回。公开类型及比较器调用同步迁移，无旧别名。
- H03/H08：字节和字符串入口共用结构扫描逻辑，字节值通过记录构造器复制并调用 GetString。语法失败回滚；非法参数在清空前抛异常；Header Create/Add 的字符串解析失败经基类引用也抛异常。GetString 自身的转换异常继续向上传播，同时回滚本次追加内容。
- H04/H05/H06：Server、Client、multipart 全部改用字节入口；首行与请求目标结构先按字节拆分。RawRequestHeader 保存自有快照，getter 返回 Copy。新 ICrossHttpRequest GUID 为 `{6F42E391-5B3B-41F6-9A75-8C03312E8669}`。
- H07/H09：服务端响应和客户端请求统一无 BOM UTF-8；非法字段、无效 UTF-16、独立 Cookie 输出失败均进入已有发送失败结算。客户端 Query 可在初始化回调修改，因此自动 URL 编码前额外检查当前参数名和值，避免编码器先替换坏代理项。

详细、可复跑的说明与结果：`Net/Tests/FPC/HttpHeaderTests/README.md`、`RESULTS.json`；#199 历史与修复对照：`Net/Tests/FPC/Issue199Tests/README.md`。新源码/二进制哈希与命令保存在各结果目录。

专项验证：Delphi Win64、Win32、FPC Win64 各 Release/Checked 均通过（每份 1041 个组件断言和 9 个 TCP 场景）；Delphi/FPC Linux64 已交叉编译并在 WSL Ubuntu 22.04 实际运行通过。Delphi Linux 785 个组件断言，FPC Linux 1041 个，两者各 9 个 TCP 场景。Delphi Linux 的 GetString 对测试中的 128 种非法高位单字节组合抛异常，故按既有 helper 异常/回滚合同验收，不断言所有平台都能将任意旧编码转换成字符串。正常 UTF-8 中文及 emoji 五目标通过，Utils.Utils.pas 未改。

Windows 三目标相邻回归：HTTP 失败/队列/存储各 143/143；HEAD 各 14/14、临时响应各 9/9、响应/复用各 22/22；认证各 59/59；请求生命周期各 6/6；multipart stream 各 21/21；热路径 tokens/headers/router 各 13543/4008/19；WebSocket 握手各 47/47。Delphi Win64 chunk/trailer 100/100。#199 整包/分段和两种对照共 12/12，原始头快照、165 字节正文和后续 GET 均正确。

额外 WebSocketFrameTests 综合程序在 Win64 发送器测试阶段出现访问异常，修改前 HEAD 的隔离源码构建也在相同位置复现；记录为已有失败，不列入通过。受本次变更影响的握手路径已由独立真实 TCP 工程验证。未执行 macOS、ARM、TLS 全矩阵、长时间压力或吞吐性能测量；新增原始值复制与持有成本不宣称为性能收益。

编译参数已参考用户脚本应用，保留 Delphi D+/L+/Y+；另有 Checked 配置。当前 Issue199Server 按旧/参考参数重编：Win64 3,342,336/3,342,336 字节，Win32 2,255,872/2,255,360 字节，FPC64 3,189,760/3,187,200 字节。实际缩减有限，详见同源码参数对照日志。当前任务未提交或推送。


## 15. 2026-10-28 三项补充修复

用户明确要求全部修复，保持当前数据结构与 GetString 解码方式，并补回归。修复范围：

- [x] R01：在 THttpHeader 字节 Decode 的原始值区间加入协议控制字段 ASCII 校验，早于 TParamEntry.Create/GetString。覆盖 Content-Length、Transfer-Encoding、Content-Encoding、Accept-Encoding、Connection、Upgrade、Expect 及 Sec-WebSocket-Key/Version/Accept/Protocol/Extensions 的全部同名项。通过 ASCII 检查后，现有 Parser/握手层继续验证数字、token、Base64 等具体语法；原始 ASCII 文本不会被 GetString 改写。普通 Referer/自定义字段保留 Unicode 与 BOM 解码语义，TParamEntry/GetString/公开记录布局不改，不增加原始值查询 API。字符串 Decode 维持现有结构语法，发送端继续整头 UTF-8。
- [x] R02：服务端 _OnBodyData 检查 multipart.Decode 的 frFailed，调用既有 Parser.Fail(400) 终止该请求；保证不进入业务成功回调、关闭/队列收尾及临时文件释放。此项不扩展 multipart 参数格式，也不另增解码状态数据结构。
- [x] R03：WebSocketSenderTests 的测试连接持有真实、有效的 TCrossSocket Owner，直到连接析构完成；不放宽生产构造器的 Owner 合同，不修改 WebSocket 发送算法。
- [x] R04：补 Header 原始控制字段（含 BOM、重复项、OWS、普通字段兼容）、双端 WebSocket BOM 握手、HTTP CL/TE BOM、完整 multipart 错误/分段/临时文件清理回归；先保存失败结果，再跑 Delphi Win64/Win32、FPC Win64 的 Release/Checked 与已有 Linux 交叉编译/实际运行。修复 Owner 后重跑 WebSocket 综合帧、Sender 队列及真实回环。

使用现有 HttpHeaderTests、Issue199Tests、WebSocketFrameTests、HttpFailureTests、HttpHeadTests 等 runner 和各目录 README 构建方式。测试构建参数继续参考用户脚本，结果写入新的 bin label，不覆盖上一轮证据；Pascal 保持 UTF-8 BOM/CRLF。已确认的普通字段解码、共享数组、raw 快照和输出语义继续适用；不改 Utils.Utils.pas，不自行提交/推送。


2026-10-28 复核修订：R01 对 Expect、Transfer-Encoding 不能整值限定 ASCII；它们的 quoted-string 参数允许 obs-text。字节校验改为：控制内容及引号外保持 ASCII，两个字段的引号参数按原始引号/反斜杠边界保留高位字节；未闭合引号失败。既有层继续执行数字、token、Base64 与支持的编码策略，不新增传输编码支持。补 Expect 合法 UTF-8 扩展参数、HEAD 的 TE 元数据兼容及引号外 BOM 拒绝测试，防止修复扩大合法输入限制。


2026-10-28 最终结果：R01～R04 完成。Header 专项 Windows 三目标 Release/Checked 各 1138 组件断言和 22 个真实 TCP 场景；Delphi/FPC Linux64 在 WSL Ubuntu 22.04 实际运行分别 882/1138 组件断言及各 22 个 TCP 场景。保留 GetString 的已知平台差异。

WebSocket 握手五目标各 55/55；综合组件、11,056 帧拆分组合、发送生命周期和每目标 11 个真实回环均通过，旧 nil Owner 崩溃解决；Windows 发送队列专项各 23/23。相邻 Windows 三目标 HTTP 失败/队列/存储各 143/143，HEAD/临时响应/响应解析复用各 14/9/22，热路径各 13543/4008/19，#199 共 12/12。HttpServer、WebSocketServer、HttpClient 原 dproj 的 MSBuild Release/Win64 全量单元构建成功。

详见 `Net/Tests/FPC/HttpHeaderTests/README.md` 和新建的 `REPAIR_RESULTS.json`，其中保留红例、当前源码哈希、实际测试路径和未验证平台。数据结构和 GetString 未改，AutoDeleteFiles 的配置语义未改；当前任务未提交/推送。
