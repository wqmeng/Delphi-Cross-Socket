// 仅替换现有文件流扩展点：保留真实文件part分支、GUID和文件名处理，不访问磁盘。
const
  MULTIPART_BENCH_BOUNDARY = 'header-bench-multipart';
type
  TMultipartMemoryBench = class(THttpMultiPartFormData)
  public
    FileStreams: NativeInt;
  protected
    function CreateFileStream(const AField: TFormField): TStream; override;
  end;

function TMultipartMemoryBench.CreateFileStream(const AField: TFormField): TStream;
begin
  Inc(FileStreams);
  Result := TBytesStream.Create(nil);
end;

function ValidateMultipartBench(const AForm: TMultipartMemoryBench;
  const AData: TBytes; const AFixture: string): Integer;
var
  I, J, LConsumed: Integer;
  LIsFile: Boolean;
  LField: TFormField;
  LExpectedName, LExpectedFileName, LDisposition: string;
  LExpectedValue, LActualValue: TBytes;
  LMatch: TMatch;
  LResult: TFormDataDecodeResult;
begin
  LIsFile := AFixture = 'mp-file-unicode.bin';
  if AFixture = 'mp-text-one.bin' then Result := 1
  else if AFixture = 'mp-text-many.bin' then Result := 16
  else
  begin
    Check(LIsFile, 'Unsupported multipart fixture');
    Result := 1;
  end;
  Check(Length(AData) > 0, 'Multipart fixture body is empty');
  Check(not AForm.AutoDeleteFiles, 'Memory-only multipart must not delete file paths');
  Check(DirectoryExists(AForm.StoragePath), 'Multipart fixture directory is missing');
  AForm.InitWithBoundary(MULTIPART_BENCH_BOUNDARY);
  LResult := AForm.Decode(Pointer(AData), Length(AData), LConsumed);
  Check((LResult = frComplete) and (LConsumed = Length(AData)),
    'Multipart fixture did not complete and consume all bytes');
  Check(AForm.Count = Result, 'Multipart field count mismatch');
  Check(AForm.FileStreams = Ord(LIsFile), 'Multipart file branch mismatch');
  LExpectedFileName := '';
  if LIsFile then
  begin
    LDisposition := 'form-data; name="upload"; filename="报告'#$D83D#$DE00'.bin"';
    // 两侧都以原静态正则+路径清理为兼容判据，不把旧平台的Unicode结果单边修正。
    LMatch := TRegEx.Match(LDisposition, '\bfilename="(.*?)"(?=;|$)', [TRegExOption.roIgnoreCase]);
    Check(LMatch.Success, 'Legacy multipart filename matcher failed');
    LExpectedFileName := TPathUtils.GetFileName(LMatch.Groups[1].Value);
    Check(LExpectedFileName <> '', 'Legacy multipart filename is empty');
    SetLength(LExpectedValue, 36);
    for I := 0 to 31 do LExpectedValue[I] := I;
    LExpectedValue[32] := Ord('b');
    LExpectedValue[33] := Ord('o');
    LExpectedValue[34] := Ord('d');
    LExpectedValue[35] := Ord('y');
  end;
  for I := 0 to Result - 1 do
  begin
    LField := AForm.Items[I];
    if LIsFile then LExpectedName := 'upload'
    else
    begin
      LExpectedName := 'field' + IntToStr(I);
      LExpectedValue := TEncoding.UTF8.GetBytes('value-' + IntToStr(I));
    end;
    Check(LField.Name = LExpectedName, 'Multipart field name/order mismatch');
    Check(LField.FileName = LExpectedFileName, 'Multipart filename differs from legacy output');
    Check(LField.IsFile = LIsFile, 'Multipart field kind mismatch');
    Check(LField.Value is TBytesStream, 'Multipart fixture unexpectedly uses a disk stream');
    if LIsFile then
    begin
      Check(LField.ContentType = 'application/octet-stream', 'Multipart file Content-Type mismatch');
      Check((LField.FilePath <> '') and (LField.ContentTransferEncoding = 'binary'),
        'Multipart file path generation or transfer metadata was skipped');
    end else
      Check((LField.ContentType = '') and (LField.FilePath = '')
        and (LField.ContentTransferEncoding = ''), 'Multipart text metadata mismatch');
    LActualValue := LField.AsBytes;
    Check(Length(LActualValue) = Length(LExpectedValue), 'Multipart payload length mismatch');
    for J := 0 to High(LExpectedValue) do
      Check(LActualValue[J] = LExpectedValue[J], 'Multipart payload byte mismatch');
  end;
end;
