// 直接ParseHeader只验证请求对象的元数据计算；不表示这些TE组合能通过完整Parser的定界校验。
// 保留原Trim.Split算法作预期，特别保留尾空段和quoted参数中逗号的既有处理。
function RunChunkedMetadataTests: Integer;
const
  Values: array[0..29] of string = (
    '', ' ', #9, 'chunked', 'CHuNkEd', ' gzip ',
    'gzip,chunked', 'gzip, chunked', ' chunked , gzip ', ',chunked',
    ',,chunked', 'chunked,', 'chunked, ', 'chunked,,', ' , , ',
    'gzip,,chunked', #9'gzip,'#9'CHUNKED '#9,
    'custom; p="a,b", chunked', 'custom; p="x,chunked"',
    'custom; p="x, chunked", gzip', 'custom; p="中文", chunked',
    'chunked; q=1', 'unchunked', 'chunked-more', 'chunked,chunked',
    ',', '  , chunked , '#9, '"chunked"',
    'custom; p="a\,b", chunked', 'custom; p="a\",b", chunked');
  InvalidValues: array[0..7] of string = (
    'chunked'#0, 'chunked'#10, 'chunked'#13'x', 'chunked'#127,
    #$FEFF'chunked', '中文', 'custom; p="unterminated', 'custom; p="bad\');
var
  I, LChecks: Integer;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('分块元数据: ' + AMessage);
  end;

  function ReferenceIsChunked(const AValue: string): Boolean;
  var
    LParts: TArray<string>;
  begin
    LParts := AValue.Trim.Split([',']);
    if Length(LParts) > 0 then
      // 与TStrUtils.SameText的当前实现相同，不要求主测试程序增加uses。
      Result := SysUtils.{$IFDEF DELPHI}SameText{$ELSE}UnicodeSameText{$ENDIF}(
        LParts[High(LParts)].Trim, 'chunked')
    else
      Result := False;
  end;

  procedure CheckHeader(const AValue, ALabel: string; const AInclude, AValid: Boolean);
  var
    F: TFixture;
    B: TBytes;
    S: string;
    LParsed: Boolean;
  begin
    F := TFixture.Create;
    try
      S := 'GET /chunk-metadata HTTP/1.1'#13#10'Host: localhost'#13#10;
      if AInclude then S := S + 'Transfer-Encoding: ' + AValue + #13#10;
      B := Bytes(S + #13#10);
      LParsed := TRequestAccess(F.Request as TCrossHttpRequest).ParseHeader(
        Pointer(B), Length(B));
      Check(LParsed = AValid, ALabel + ' 字段解析结果');
      if LParsed then
        Check(F.Request.IsChunked = ReferenceIsChunked(F.Request.TransferEncoding),
          ALabel + ' 与旧最后一段算法一致')
      else
        Check(F.Request.Header.Count = 0, ALabel + ' 非法字段回滚整个头集合');
    finally
      F.Free;
    end;
  end;

begin
  LChecks := 0;
  CheckHeader('', 'missing', False, True);
  for I := Low(Values) to High(Values) do
    CheckHeader(Values[I], 'valid-' + IntToStr(I), True, True);
  for I := Low(InvalidValues) to High(InvalidValues) do
    CheckHeader(InvalidValues[I], 'invalid-' + IntToStr(I), True, False);
  Result := LChecks;
end;
