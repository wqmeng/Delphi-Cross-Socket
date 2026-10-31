// 仅使用公开 API；可合入现有 HttpHeaderTests，不依赖内部列表或指针同一性。
function RunHeaderNameTests: Integer;
const
  NAMES: array[0..43] of string = (
    HEADER_ACCEPT,
    HEADER_ACCEPT_CHARSET,
    HEADER_ACCEPT_ENCODING,
    HEADER_ACCEPT_LANGUAGE,
    HEADER_ACCEPT_RANGES,
    HEADER_AUTHORIZATION,
    HEADER_CACHE_CONTROL,
    HEADER_CONNECTION,
    HEADER_CONTENT_DISPOSITION,
    HEADER_CONTENT_ENCODING,
    HEADER_CONTENT_LANGUAGE,
    HEADER_CONTENT_LENGTH,
    HEADER_CONTENT_RANGE,
    HEADER_CONTENT_TYPE,
    HEADER_COOKIE,
    HEADER_CROSS_HTTP_CLIENT,
    HEADER_CROSS_HTTP_SERVER,
    HEADER_ETAG,
    HEADER_EXPECT,
    HEADER_HOST,
    HEADER_IF_MODIFIED_SINCE,
    HEADER_IF_NONE_MATCH,
    HEADER_IF_RANGE,
    HEADER_LAST_MODIFIED,
    HEADER_LOCATION,
    HEADER_PRAGMA,
    HEADER_PROXY_AUTHENTICATE,
    HEADER_PROXY_AUTHORIZATION,
    HEADER_RANGE,
    HEADER_REFERER,
    HEADER_SEC_WEBSOCKET_ACCEPT,
    HEADER_SEC_WEBSOCKET_KEY,
    HEADER_SEC_WEBSOCKET_VERSION,
    HEADER_SETCOOKIE,
    HEADER_TRANSFER_ENCODING,
    HEADER_UPGRADE,
    HEADER_USER_AGENT,
    HEADER_VARY,
    HEADER_WWW_AUTHENTICATE,
    HEADER_X_METHOD_OVERRIDE,
    HEADER_X_FORWARDED_FOR,
    HEADER_X_REAL_IP,
    HEADER_X_FORWARDED_HOST,
    HEADER_X_FORWARDED_SERVER
  );
var
  H: THttpHeader;
  LChecks, I: Integer;
  LVariant: string;
  B: TBytes;

  procedure Check(ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then
      raise Exception.CreateFmt('Header name check %d: %s', [LChecks, AMessage]);
  end;

  procedure CheckRecord(AIndex: Integer; const AName, AValue: string);
  var
    E: TParamEntry;
    J: Integer;
  begin
    E := H.Items[AIndex];
    Check(E.Name = AName, 'name preserved: ' + AName);
    Check(E.Value = AValue, 'value preserved: ' + AName);
    Check(E.HasRawValue, 'byte source retained: ' + AName);
    Check(Length(E.RawValue) = Length(AValue), 'raw length: ' + AName);
    for J := 0 to Length(E.RawValue) - 1 do
      Check(E.RawValue[J] = Ord(AValue[J + 1]), 'raw byte: ' + AName);
  end;

  procedure CheckName(const AName: string);
  var
    LBuffer: TBytes;
    J: Integer;
  begin
    LBuffer := TEncoding.UTF8.GetBytes(AName + ': value'#13#10#13#10);
    Check(H.Decode(Pointer(LBuffer), Length(LBuffer)), 'byte decode: ' + AName);
    Check(H.Count = 1, 'byte count: ' + AName);
    CheckRecord(0, AName, 'value');
    // 调用期间借用的源缓冲不再有效后，名称、值和 raw 仍须保持。
    for J := 0 to Length(LBuffer) - 1 do LBuffer[J] := $CC;
    CheckRecord(0, AName, 'value');
  end;

  function NearName(const AName: string): string;
  begin
    Result := AName;
    if Result[Length(Result)] = 'Z' then
      Result[Length(Result)] := 'Y'
    else
      Result[Length(Result)] := 'Z';
  end;

  function ToggleFirstLetter(const AName: string): string;
  var
    J: Integer;
  begin
    Result := AName;
    for J := 1 to Length(Result) do
    begin
      if (Result[J] >= 'A') and (Result[J] <= 'Z') then
      begin
        Result[J] := Char(Ord(Result[J]) + 32);
        Exit;
      end;
      if (Result[J] >= 'a') and (Result[J] <= 'z') then
      begin
        Result[J] := Char(Ord(Result[J]) - 32);
        Exit;
      end;
    end;
  end;

  procedure CheckMixedAndMutation(const AName: string);
  var
    LCustom, LChanged: string;
    LBuffer: TBytes;
    E: TParamEntry;
    J: Integer;
  begin
    LCustom := NearName(AName);
    LBuffer := TEncoding.UTF8.GetBytes(AName + ': one'#13#10 +
      LCustom + ': two'#13#10 + AName + ': three'#13#10#13#10);
    Check(H.Decode(Pointer(LBuffer), Length(LBuffer)), 'mixed decode: ' + AName);
    Check(H.Count = 3, 'mixed count: ' + AName);
    // 常见名 -> 同长度自定义名 -> 常见名：验证每条记录保留各自名称。
    CheckRecord(0, AName, 'one');
    CheckRecord(1, LCustom, 'two');
    CheckRecord(2, AName, 'three');
    for J := 0 to Length(LBuffer) - 1 do LBuffer[J] := $CC;
    CheckRecord(0, AName, 'one');
    CheckRecord(1, LCustom, 'two');
    CheckRecord(2, AName, 'three');
    // 外部完整记录修改 Name，不能污染原列表、其它同名项或后续解析结果。
    E := H.Items[0];
    LChanged := ToggleFirstLetter(AName);
    E.Name[1] := LChanged[1];
    Check(E.Name = LChanged, 'external indexed name write');
    CheckRecord(0, AName, 'one');
    CheckRecord(1, LCustom, 'two');
    CheckRecord(2, AName, 'three');
    H.Items[0] := E;
    CheckRecord(0, LChanged, 'one');
    CheckRecord(2, AName, 'three');
    CheckName(AName);
    Check(E.Name = LChanged, 'external record survives next decode');
  end;

begin
  LChecks := 0;
  H := THttpHeader.Create;
  try
    for I := Low(NAMES) to High(NAMES) do
    begin
      CheckName(NAMES[I]);
      CheckName(LowerCase(NAMES[I]));
      CheckName(UpperCase(NAMES[I]));
      LVariant := ToggleFirstLetter(NAMES[I]);
      Check(LVariant <> NAMES[I], 'case variant differs');
      CheckName(LVariant);
      CheckName(NearName(NAMES[I]));
      CheckName(Copy(NAMES[I], 1, Length(NAMES[I]) - 1));
      CheckName(NAMES[I] + 'X');
      CheckMixedAndMutation(NAMES[I]);
      // 字符串入口保留名称原貌及无 raw 来源。
      Check(H.Decode(NAMES[I] + ': text'#13#10#13#10), 'string decode');
      Check(H.Count = 1, 'string count');
      Check(H.Items[0].Name = NAMES[I], 'string name unchanged');
      Check(H.Items[0].Value = 'text', 'string value unchanged');
      Check(not H.Items[0].HasRawValue, 'string has no byte source');
      Check(Length(H.Items[0].RawValue) = 0, 'string raw empty');
    end;
    CheckName('X-Custom-Trace');
    CheckName('x-private');
    CheckName('A');
    CheckName('$Custom');
    // 非 token 名称必须在扫描阶段失败，并回滚本次新增记录。
    B := TEncoding.UTF8.GetBytes('Host : value'#13#10#13#10);
    Check(not H.Decode(Pointer(B), Length(B)), 'name whitespace rejected');
    Check(H.Count = 0, 'invalid name rollback');
    B := TEncoding.UTF8.GetBytes('X-名字: value'#13#10#13#10);
    Check(not H.Decode(Pointer(B), Length(B)), 'non-ASCII name rejected');
    Check(H.Count = 0, 'non-ASCII rollback');
  finally
    H.Free;
  end;
  Result := LChecks;
end;
