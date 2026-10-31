// 保留旧循环作独立对照；验证现有表和查找规则，不把本轮变成映射内容更新。
function RunLookupTests: Integer;
const
  UnknownStatuses: array[0..7] of Integer = (
    -1, 0, 99, 103, 299, 599, Low(Integer), High(Integer));
  FileCases: array[0..15] of string = (
    '', 'plain', '.', '.hidden', 'file.', 'archive.tar.gz',
    'FILE.TxT', 'folder.name/file', 'folder.name/file.TXT',
    '文档/报告.PDF', '文档/文件.unknown-extension', '文档/éxample.html',
    '.txt', 'file.扩展名', 'file.'#0'txt', 'file.txt'#0'.html');
var
  I, J, LChecks: Integer;
  LFileName, LUpper, LActual, LCopy, LExpected: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('常量表查找: ' + AMessage);
  end;

  function ReferenceStatus(const ACode: Integer): string;
  var
    LItem: THttpStatus;
  begin
    for LItem in STATUS_CODES do
      if LItem.Code = ACode then Exit(LItem.Text);
    Result := ACode.ToString;
  end;

  function ReferenceMime(const AFileName: string): string;
  var
    LExt: string;
    LItem: TMimeValue;
  begin
    LExt := ExtractFileExt(AFileName).Substring(1);
    for LItem in MIME_TYPES do
      if TStrUtils.SameText(LItem.Key, LExt) then Exit(LItem.Value);
    Result := TMediaType.APPLICATION_OCTET_STREAM;
  end;

begin
  LChecks := 0;
  for I := Low(STATUS_CODES) to High(STATUS_CODES) do
    Check(TCrossHttpUtils.GetHttpStatusText(STATUS_CODES[I].Code)
      = ReferenceStatus(STATUS_CODES[I].Code), '状态表 item=' + IntToStr(I));
  for I := Low(UnknownStatuses) to High(UnknownStatuses) do
    Check(TCrossHttpUtils.GetHttpStatusText(UnknownStatuses[I])
      = IntToStr(UnknownStatuses[I]), '未知状态回退 item=' + IntToStr(I));

  // 每个扩展名都由旧循环求首个匹配值，不能假定表内不存在重复Key。
  for I := Low(MIME_TYPES) to High(MIME_TYPES) do
  begin
    LFileName := 'file.' + MIME_TYPES[I].Key;
    Check(TCrossHttpUtils.GetFileMIMEType(LFileName) = ReferenceMime(LFileName),
      'MIME表 item=' + IntToStr(I));
    // 表中的Key均为ASCII；显式转换ASCII字母，避免测试依赖系统大小写区域设置。
    LUpper := MIME_TYPES[I].Key;
    for J := 1 to Length(LUpper) do
      if (LUpper[J] >= 'a') and (LUpper[J] <= 'z') then
        LUpper[J] := Char(Ord(LUpper[J]) - Ord('a') + Ord('A'));
    LFileName := 'file.' + LUpper;
    Check(TCrossHttpUtils.GetFileMIMEType(LFileName) = ReferenceMime(LFileName),
      '大写MIME后缀 item=' + IntToStr(I));
  end;
  for I := Low(FileCases) to High(FileCases) do
    Check(TCrossHttpUtils.GetFileMIMEType(FileCases[I]) = ReferenceMime(FileCases[I]),
      '路径和后缀边界 item=' + IntToStr(I));

  // 整串扩展名可能与输入共享存储；删除点号不能改动输入及其副本。
  LFileName := '.txt';
  UniqueString(LFileName);
  LCopy := LFileName;
  LExpected := ReferenceMime(LFileName);
  LActual := TCrossHttpUtils.GetFileMIMEType(LFileName);
  Check(LActual = LExpected, '共享动态扩展名匹配旧结果');
  Check(LFileName = '.txt', '共享动态扩展名输入不变');
  Check(LCopy = '.txt', '共享动态扩展名副本不变');

  // 输入动态且唯一时，结果赋回输入变量也必须保持旧查询行为。
  LFileName := '.txt';
  UniqueString(LFileName);
  LExpected := ReferenceMime(LFileName);
  LFileName := TCrossHttpUtils.GetFileMIMEType(LFileName);
  Check(LFileName = LExpected, '唯一动态扩展名自赋值匹配旧结果');

  // 固定预期单独保护表内重复扩展名的首项优先，避免oracle掩盖次序变化。
  Check(TCrossHttpUtils.GetFileMIMEType('file.class') = 'application/java-vm',
    'class使用首个映射');
  Check(TCrossHttpUtils.GetFileMIMEType('file.DMG') = 'application/octet-stream',
    'dmg使用首个映射');
  Check(TCrossHttpUtils.GetFileMIMEType('file.wmz') = 'application/x-ms-wmz',
    'wmz使用首个映射');
  Check(TCrossHttpUtils.GetFileMIMEType('file.sub') = 'image/vnd.dvb.subtitle',
    'sub使用首个映射');
  Check(TCrossHttpUtils.GetFileMIMEType('file.unknown-extension')
    = TMediaType.APPLICATION_OCTET_STREAM, '未知后缀固定回退');

  LActual := TCrossHttpUtils.GetHttpStatusText(200);
  LCopy := LActual;
  LCopy[1] := #1;
  Check((LActual <> LCopy) and (LActual = ReferenceStatus(200)), '状态文本副本写时复制');
  LActual[1] := #2;
  LCopy := TCrossHttpUtils.GetHttpStatusText(200);
  Check((LCopy[1] <> #2) and (LCopy = ReferenceStatus(200)), '修改结果不污染状态常量');

  LActual := TCrossHttpUtils.GetFileMIMEType('file.txt');
  LCopy := LActual;
  LCopy[1] := #1;
  Check((LActual <> LCopy) and (LActual = ReferenceMime('file.txt')), 'MIME文本副本写时复制');
  LActual[1] := #2;
  LCopy := TCrossHttpUtils.GetFileMIMEType('file.txt');
  Check((LCopy[1] <> #2) and (LCopy = ReferenceMime('file.txt')), '修改结果不污染MIME常量');
  Result := LChecks;
end;
