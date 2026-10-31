// 通过真实multipart Decode比较旧两条静态正则的结果，不把参数解析改为新的RFC规则。
function RunMultipartHeaderTests: Integer;
type
  THeaderCase = record
    Disposition, ContentType: string;
  end;
const
  Cases: array[0..27] of THeaderCase = (
    (Disposition: 'form-data; name="field"'; ContentType: ''),
    (Disposition: 'form-data; filename="file.txt"; name="field"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name="first"; name="second"; filename="a.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; filename="first.bin"; filename="second.bin"; name="x"'; ContentType: 'application/octet-stream'),
    (Disposition: 'FORM-DATA; NaMe="MiXeD"; FiLeNaMe="A.TXT"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name=""; filename=""'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name=""'; ContentType: ''),
    (Disposition: 'form-data; name="中文"; filename="报告.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name="'#$D83D#$DE00'"; filename="'#$D83D#$DE00'.bin"'; ContentType: 'application/octet-stream'),
    (Disposition: 'form-data; filename="no-name.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name="no-filename"'; ContentType: 'application/octet-stream'),
    (Disposition: 'form-data'; ContentType: 'application/octet-stream'),
    (Disposition: 'form-data; name="text"; filename="ignored.txt"'; ContentType: ''),
    (Disposition: 'form-data; name=plain; filename="f.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name=''single''; filename="f.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name = "spaced"; filename="f.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name="x"junk; filename="f.txt"'; ContentType: 'text/plain'),
    (Disposition: 'name="at-end"'; ContentType: ''),
    (Disposition: 'form-data; name="a;b"; filename="semi;colon.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name="a"b"; filename="quoted.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name="a\"b"; filename="slash.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; xname="wrong"; name="right"'; ContentType: ''),
    (Disposition: 'form-data; x-name="legacy"; filename="x.bin"'; ContentType: 'application/octet-stream'),
    (Disposition: 'form-data; éname="platform-word-boundary"; filename="x.bin"'; ContentType: 'application/octet-stream'),
    (Disposition: 'form-data; name="has'#9'tab"; filename="tab.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; filename="one.txt"; name="x"; filename="two.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name="path"; filename="../folder/文件.txt"'; ContentType: 'text/plain'),
    (Disposition: 'form-data; name="unterminated; filename="file.txt"'; ContentType: 'text/plain'));
var
  I, LChecks: Integer;
  LDirectory: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('multipart头参数: ' + AMessage);
  end;

  procedure RunCase(const ACase: THeaderCase; const AIndex, AChunkSize: Integer);
  var
    LForm: THttpMultiPartFormData;
    LMatch: TMatch;
    LExpectedName, LExpectedFileName, LFilePath, LHeader, LLabel: string;
    LHasFileName: Boolean;
    LBytes: TBytes;
    LOffset, LCount, LUsed: Integer;
    LResult: TFormDataDecodeResult;
  begin
    LLabel := IntToStr(AIndex) + ' ';
    LExpectedName := '';
    // 完整保留旧的独立搜索表达式和选项；异常引号、词边界仍以原实现为准。
    LMatch := TRegEx.Match(ACase.Disposition, '\bname="(.*?)"(?=;|$)', [TRegExOption.roIgnoreCase]);
    if LMatch.Success then LExpectedName := LMatch.Groups[1].Value;
    LExpectedFileName := '';
    LHasFileName := False;
    if ACase.ContentType <> '' then
    begin
      LMatch := TRegEx.Match(ACase.Disposition, '\bfilename="(.*?)"(?=;|$)', [TRegExOption.roIgnoreCase]);
      LHasFileName := LMatch.Success;
      if LHasFileName then LExpectedFileName := TPathUtils.GetFileName(LMatch.Groups[1].Value);
    end;

    LHeader := '--multipart-header'#13#10'Content-Disposition: ' + ACase.Disposition + #13#10;
    if ACase.ContentType <> '' then LHeader := LHeader + 'Content-Type: ' + ACase.ContentType + #13#10;
    LHeader := LHeader + 'Content-Transfer-Encoding: binary'#13#10#13#10;
    LBytes := Bytes(LHeader + 'body'#13#10'--multipart-header--'#13#10);
    LFilePath := '';
    LForm := THttpMultiPartFormData.Create;
    try
      LForm.InitWithBoundary('multipart-header');
      LForm.StoragePath := LDirectory;
      LForm.AutoDeleteFiles := True;
      LOffset := 0;
      LResult := frContinue;
      while LOffset < Length(LBytes) do
      begin
        LCount := Length(LBytes) - LOffset;
        if (AChunkSize > 0) and (LCount > AChunkSize) then LCount := AChunkSize;
        LResult := LForm.Decode(@LBytes[LOffset], LCount, LUsed);
        Check((LResult <> frFailed) and (LUsed = LCount), LLabel + '分片完整消费');
        Inc(LOffset, LUsed);
      end;
      Check((LResult = frComplete) and (LForm.Count = 1), LLabel + '表单完成且只接管一项');
      Check(LForm.Items[0].Name = LExpectedName, LLabel + 'name与旧正则一致');
      Check(LForm.Items[0].ContentType = ACase.ContentType, LLabel + 'Content-Type保持');
      Check(LForm.Items[0].ContentTransferEncoding = 'binary', LLabel + '传输编码属性保持');
      Check(LForm.Items[0].AsString = 'body', LLabel + '正文完整且归字段所有');
      Check(LForm.Items[0].IsFile = (ACase.ContentType <> ''), LLabel + '仍由Content-Type决定文件分类');
      LFilePath := LForm.Items[0].FilePath;
      if ACase.ContentType <> '' then
      begin
        if LHasFileName then
          Check(LForm.Items[0].FileName = LExpectedFileName, LLabel + 'filename与旧正则及路径清理一致')
        else
          Check(LForm.Items[0].FileName.EndsWith('.bin')
            and (LForm.Items[0].FileName = ExtractFileName(LFilePath)), LLabel + '缺少filename时使用临时bin名');
        Check(ExtractFilePath(LFilePath) = IncludeTrailingPathDelimiter(LDirectory), LLabel + '临时文件只在独立目录内');
        Check(FileExists(LFilePath), LLabel + '文件字段具有真实存储');
      end else
      begin
        Check((LForm.Items[0].FileName = '') and (LFilePath = ''), LLabel + '文本字段不采用filename');
        Check(LForm.Items[0].Value is TBytesStream, LLabel + '文本仍使用内存流');
      end;
    finally
      LForm.Free;
    end;
    if LFilePath <> '' then Check(not FileExists(LFilePath), LLabel + '释放拥有字段后删除临时文件');
  end;

  procedure CheckFailureCleanup;
  var
    LForm: THttpMultiPartFormData;
    LBytes: TBytes;
    LFilePath: string;
  begin
    LFilePath := '';
    LForm := THttpMultiPartFormData.Create;
    try
      LForm.InitWithBoundary('multipart-header');
      LForm.StoragePath := LDirectory;
      LForm.AutoDeleteFiles := True;
      // 第一项已拥有文件；第二项头合法但缺少Disposition，必须失败且不接管半初始化项。
      LBytes := Bytes('--multipart-header'#13#10
        + 'Content-Disposition: form-data; name="first"; filename="first.txt"'#13#10
        + 'Content-Type: text/plain'#13#10#13#10'first'#13#10
        + '--multipart-header'#13#10'Content-Type: text/plain'#13#10#13#10
        + 'second'#13#10'--multipart-header--'#13#10);
      Check(LForm.Decode(Pointer(LBytes), Length(LBytes)) = frFailed, '后续缺Disposition时失败');
      Check(LForm.Count = 1, '失败字段未加入拥有列表');
      Check(LForm.Items[0].AsString = 'first', '先前接管的文件内容保持');
      LFilePath := LForm.Items[0].FilePath;
      Check(FileExists(LFilePath), '失败前已接管的文件仍由表单持有');
    finally
      LForm.Free;
    end;
    Check((LFilePath <> '') and not FileExists(LFilePath), '失败表单释放后清理已拥有文件');
  end;

begin
  LChecks := 0;
  // 目录在本次测试EXE所在bin下，名称唯一；不复用或清理其它测试的目录。
  LDirectory := IncludeTrailingPathDelimiter(ExtractFilePath(ParamStr(0)))
    + 'multipart-header-' + TUtils.GetGUID;
  Check(not DirectoryExists(LDirectory), '隔离目录此前不存在');
  Check(CreateDir(LDirectory), '创建隔离目录');
  try
    for I := Low(Cases) to High(Cases) do
      if I = 7 then RunCase(Cases[I], I, 1)
      else if I = 27 then RunCase(Cases[I], I, 7)
      else RunCase(Cases[I], I, 0);
    CheckFailureCleanup;
  finally
    // 每个表单负责自己的文件；这里仅能删除空目录，避免掩盖文件泄漏。
    Check(RemoveDir(LDirectory), '所有表单释放后隔离目录为空且可删除');
  end;
  Result := LChecks;
end;
