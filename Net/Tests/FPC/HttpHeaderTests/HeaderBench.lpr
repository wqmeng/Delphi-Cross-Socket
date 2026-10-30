program HeaderBench;
{$I zLib.inc}
{$IFNDEF FPC}{$APPTYPE CONSOLE}{$ENDIF}
uses
  SysUtils, Classes, Windows, Net.CrossHttpParams, Net.CrossHttpServer,
  Net.CrossHttpClient, Net.CrossHttpUtils, Net.CrossSocket.Base, Net.SocketAPI;
type
  TRequestAccess = class(TCrossHttpRequest);
  TResponseAccess = class(TCrossHttpClientResponse);
  TMode = (bmWire, bmString, bmRequest, bmNewRequest, bmResponse, bmEncode, bmServerEncode, bmEncodeLegacy, bmFieldEncode, bmQueryEncode, bmCookieEncode);
  {$IFDEF FPC}
  TBenchManager = TMemoryManager;
  TBenchSize = PtrUInt;
  {$ELSE}
  TBenchManager = TMemoryManagerEx;
  TBenchSize = NativeInt;
  {$ENDIF}
var
  OriginalManager, CountingManager: TBenchManager;
  AllocationCalls, AllocationBytes: Int64;
threadvar Tracking: Boolean;

function CountGetMem(ASize: TBenchSize): Pointer;
begin
  if Tracking then begin Inc(AllocationCalls); Inc(AllocationBytes, ASize); end;
  Result := OriginalManager.GetMem(ASize);
end;
function CountAllocMem(ASize: TBenchSize): Pointer;
begin
  if Tracking then begin Inc(AllocationCalls); Inc(AllocationBytes, ASize); end;
  Result := OriginalManager.AllocMem(ASize);
end;
function CountReallocMem({$IFDEF FPC}var{$ENDIF} P: Pointer; ASize: TBenchSize): Pointer;
begin
  if Tracking and (ASize > 0) then begin Inc(AllocationCalls); Inc(AllocationBytes, ASize); end;
  Result := OriginalManager.ReallocMem(P, ASize);
end;

procedure StartCounting;
begin
  GetMemoryManager(OriginalManager); CountingManager := OriginalManager;
  CountingManager.GetMem := CountGetMem; CountingManager.AllocMem := CountAllocMem;
  CountingManager.ReallocMem := CountReallocMem;
  AllocationCalls := 0; AllocationBytes := 0;
  SetMemoryManager(CountingManager); Tracking := True;
end;
procedure StopCounting;
begin Tracking := False; SetMemoryManager(OriginalManager); end;

procedure Check(const OK: Boolean; const Text: string);
begin if not OK then raise Exception.Create(Text); end;

function SameParams(const A, B: TBaseParams): Boolean;
var I: Integer;
begin
  if A.Count <> B.Count then Exit(False);
  for I := 0 to A.Count - 1 do
    if (A.Items[I].Name <> B.Items[I].Name) or (A.Items[I].Value <> B.Items[I].Value) then Exit(False);
  Result := True;
end;

procedure Benchmark;
const
  ResponseLine: AnsiString = 'HTTP/1.1 200 OK'#13#10;
  // 出站基准只借此种子建立版本/keepalive，不把旧请求转码缺陷混入发送语义。
  EncodeRequest: AnsiString = 'GET /bench HTTP/1.1'#13#10
    + 'Host: localhost'#13#10'Connection: keep-alive'#13#10#13#10;
var
  Mode: TMode;
  Data, HeaderData, ResponseData, ValidatedHeader: TBytes;
  HeaderText, FirstLine, Target, ExpectedPath, FieldText: string;
  Header, Expected: THttpHeader;
  ExpectedQuery, LDecodedQuery: THttpUrlParams;
  ExpectedCookies, LDecodedCookies: TRequestCookies;
  Request: TRequestAccess;
  Response: TResponseAccess;
  ServerResponse: TCrossHttpResponse;
  ServerResponseOwner: ICrossHttpResponse;
  RequestOwner: ICrossHttpRequest;
  Server: ICrossHttpServer;
  Connection: ICrossHttpConnection;
  ConnectionObj: TCrossHttpConnection;
  Stream: TFileStream;
  I, LineEnd, HeaderEnd, N, EntrySize, RawBytes: Integer;
  BeginTick, EndTick, Frequency, Checksum, UnitChecksum: Int64;
  Allocations, Valid: Boolean;

  // 旧线缆入口必须包含其原有的默认代码页转换，不能用 UTF-8 替它修正后再计时。
  function DecodeWire: Boolean;
  {$IFNDEF HEADER_BYTES}var Text: string;{$ENDIF}
  begin
    {$IFDEF HEADER_BYTES}
    Result := Header.Decode(Pointer(HeaderData), Length(HeaderData));
    {$ELSE}
    SetString(Text, MarshaledAString(Pointer(HeaderData)), Length(HeaderData));
    Result := Header.Decode(Text);
    {$ENDIF}
  end;

  // 保留旧测量表达式，仅供 encode-legacy 补充对照；不属于默认恢复门槛。
  function EncodeLegacyHeader: TBytes;
  var Text: string;
  begin
    Text := 'HTTP/1.1 200 OK'#13#10 + Expected.Encode;
    {$IFDEF HEADER_BYTES}
    TCrossHttpUtils.CheckUtf16(Text);
    Result := TEncoding.UTF8.GetBytes(Text);
    {$ELSE}
    Result := TEncoding.ASCII.GetBytes(Text);
    {$ENDIF}
  end;

  // 编译标记由冻结的 Server/Client 实际出站函数体确定，并写入构建元数据。
  // encode 保持最小完整头组合；server-encode 同时通过真实入口交叉验证。
  function EncodeHeader: TBytes;
  var
    Text: string;
    {$IFDEF OUT_PREFIX_UTF8}Fields: string;{$ENDIF}
  begin
    {$IFDEF OUT_SAFE_UTF8}
    Text := 'HTTP/1.1 200 OK'#13#10 + Expected.Encode;
    Result := TCrossHttpUtils.EncodeHeader(Text);
    {$ELSE}
    {$IFDEF OUT_PREFIX_UTF8}
    Text := 'HTTP/1.1 200 OK'#13#10;
    Fields := Expected.Encode;
    TCrossHttpUtils.CheckUtf16(Text);
    Text := Text + Fields;
    Result := TEncoding.UTF8.GetBytes(Text);
    {$ELSE}
    Text := 'HTTP/1.1 200 OK'#13#10 + Expected.Encode;
    {$IFDEF OUT_FULL_UTF8}
    TCrossHttpUtils.CheckUtf16(Text);
    Result := TEncoding.UTF8.GetBytes(Text);
    {$ELSE}
    {$IFDEF OUT_ASCII}
    Result := TEncoding.ASCII.GetBytes(Text);
    {$ELSE}
    raise Exception.Create('Missing frozen output-path define');
    {$ENDIF}
    {$ENDIF}
    {$ENDIF}
    {$ENDIF}
  end;

  // 可信 fixture 明确为 UTF-8；oracle 独立拆行/冒号/ASCII OWS，不能复用被测 Decode。
  // 只通过未改语义的字符串 Add 建立期望，避免同一解析错误同时污染两侧而互相掩盖。
  procedure InitExpectedHeader;
  var
    LRemaining, LLine, LName, LValue: string;
    LCRLF, LColon, LFirst, LLast: Integer;
  begin
    Expected.Clear;
    LRemaining := HeaderText;
    while LRemaining <> '' do
    begin
      LCRLF := Pos(#13#10, LRemaining);
      if LCRLF > 0 then
      begin
        LLine := Copy(LRemaining, 1, LCRLF - 1);
        LRemaining := Copy(LRemaining, LCRLF + 2, MaxInt);
      end else
      begin
        LLine := LRemaining;
        LRemaining := '';
      end;
      if LLine = '' then
      begin
        Check(LRemaining = '', 'Trusted fixture has trailing header data');
        Break;
      end;
      LColon := Pos(':', LLine);
      Check(LColon > 1, 'Trusted fixture has invalid field name/colon');
      LName := Copy(LLine, 1, LColon - 1);
      LValue := Copy(LLine, LColon + 1, MaxInt);
      LFirst := 1;
      LLast := Length(LValue);
      while LFirst <= LLast do
      begin
        if (LValue[LFirst] <> ' ') and (LValue[LFirst] <> #9) then Break;
        Inc(LFirst);
      end;
      while LLast >= LFirst do
      begin
        if (LValue[LLast] <> ' ') and (LValue[LLast] <> #9) then Break;
        Dec(LLast);
      end;
      Expected.Add(LName, Copy(LValue, LFirst, LLast - LFirst + 1), True);
    end;
  end;

  function ValidRequest(const R: TRequestAccess): Boolean;
  begin
    Result := SameParams(R.Header, Expected) and SameParams(R.Query, ExpectedQuery)
      and (R.Path = ExpectedPath)
      and (R.ContentLength = StrToInt64Def(Expected['Content-Length'], -1));
  end;

  // 语义检查、数据准备、对象预建不计时；new-request 特意把构造/析构计入循环。
  procedure Run(const Count: Integer);
  var J: Integer; R: TRequestAccess; Encoded: TBytes; EncodedFields: string;
  begin
    case Mode of
      bmWire: for J := 1 to Count do
        if DecodeWire then Inc(Checksum, Header.Count);
      bmString: for J := 1 to Count do
        if Header.Decode(HeaderText) then Inc(Checksum, Header.Count);
      bmRequest: for J := 1 to Count do
        if Request.ParseHeader(Pointer(Data), Length(Data)) then Inc(Checksum, Request.Header.Count);
      bmNewRequest: for J := 1 to Count do
        begin
          R := TRequestAccess.Create(ConnectionObj);
          try
            if R.ParseHeader(Pointer(Data), Length(Data)) then Inc(Checksum, R.Header.Count);
          finally R.Free; end;
        end;
      bmResponse: for J := 1 to Count do
        if Response.ParseHeader(Pointer(ResponseData), Length(ResponseData)) then Inc(Checksum, Response.Header.Count);
      bmEncode: for J := 1 to Count do
        begin Encoded := EncodeHeader; Inc(Checksum, Length(Encoded)); end;
      bmEncodeLegacy: for J := 1 to Count do
        begin Encoded := EncodeLegacyHeader; Inc(Checksum, Length(Encoded)); end;
      bmFieldEncode: for J := 1 to Count do
        begin EncodedFields := Expected.Encode; Inc(Checksum, Length(EncodedFields)); end;
      bmQueryEncode: for J := 1 to Count do
        begin EncodedFields := ExpectedQuery.Encode; Inc(Checksum, Length(EncodedFields)); end;
      bmCookieEncode: for J := 1 to Count do
        begin EncodedFields := ExpectedCookies.Encode; Inc(Checksum, Length(EncodedFields)); end;
      bmServerEncode: for J := 1 to Count do
        begin Encoded := HttpHeaderBenchCreate(ServerResponse, 165); Inc(Checksum, Length(Encoded)); end;
    end;
  end;
begin
  if ParamStr(1) = 'wire' then Mode := bmWire
  else if ParamStr(1) = 'string' then Mode := bmString
  else if ParamStr(1) = 'request' then Mode := bmRequest
  else if ParamStr(1) = 'new-request' then Mode := bmNewRequest
  else if ParamStr(1) = 'response' then Mode := bmResponse
  else if ParamStr(1) = 'encode' then Mode := bmEncode
  else if ParamStr(1) = 'server-encode' then Mode := bmServerEncode
  else if ParamStr(1) = 'encode-legacy' then Mode := bmEncodeLegacy
  else if ParamStr(1) = 'field-encode' then Mode := bmFieldEncode
  else if ParamStr(1) = 'query-encode' then Mode := bmQueryEncode
  else if ParamStr(1) = 'cookie-encode' then Mode := bmCookieEncode
  else raise Exception.Create('Unknown benchmark');
  N := StrToInt(ParamStr(3)); Check(N > 0, 'Invalid count');
  Allocations := ParamStr(4) = 'alloc';
  // 参数为亲和掩码，不是 CPU 编号；默认 4 即逻辑 CPU 2。只固定基准线程。
  Check(SetThreadAffinityMask(GetCurrentThread, NativeUInt(StrToInt64(ParamStr(5)))) <> 0, 'Affinity failed');
  QueryPerformanceFrequency(Frequency);
  Stream := TFileStream.Create(ParamStr(2), fmOpenRead or fmShareDenyNone);
  try
    SetLength(Data, Stream.Size); Stream.ReadBuffer(Data[0], Length(Data));
  finally Stream.Free; end;
  LineEnd := -1; HeaderEnd := -1;
  for I := 0 to Length(Data) - 2 do
    if (Data[I] = 13) and (Data[I+1] = 10) then begin LineEnd := I; Break; end;
  for I := 0 to Length(Data) - 4 do
    if (Data[I] = 13) and (Data[I+1] = 10) and (Data[I+2] = 13) and (Data[I+3] = 10) then
    begin HeaderEnd := I+4; Break; end;
  Check((LineEnd > 0) and (HeaderEnd > LineEnd), 'Fixture has no complete header');
  SetLength(Data, HeaderEnd); // ParseHeader 不接收正文；保留 Content-Length 元数据供验证。
  HeaderData := Copy(Data, LineEnd+2, HeaderEnd-LineEnd-2);
  HeaderText := TEncoding.UTF8.GetString(HeaderData);
  FirstLine := TEncoding.UTF8.GetString(Data, 0, LineEnd);
  Target := Copy(FirstLine, Pos(' ', FirstLine)+1, MaxInt);
  Target := Copy(Target, 1, Pos(' ', Target)-1);
  SetLength(ResponseData, Length(ResponseLine)+Length(HeaderData));
  Move(ResponseLine[1], ResponseData[0], Length(ResponseLine));
  Move(HeaderData[0], ResponseData[Length(ResponseLine)], Length(HeaderData));
  Expected := THttpHeader.Create; Header := THttpHeader.Create; ExpectedQuery := THttpUrlParams.Create;
  Server := TCrossHttpServer.Create(1, False);
  ConnectionObj := TCrossHttpConnection.Create(Server as TCrossHttpServer, INVALID_SOCKET, ctAccept, '', nil);
  Connection := ConnectionObj;
  Request := TRequestAccess.Create(ConnectionObj);
  // ServerResponse 会持有请求接口；统一持有一个本地接口，避免最后一次 Release 后再 Free。
  RequestOwner := Request;
  ServerResponse := nil;
  Response := TResponseAccess.Create(ICrossHttpClientConnection(nil));
  try
    InitExpectedHeader;
    I := Pos('?', Target);
    if I > 0 then begin ExpectedQuery.Decode(Copy(Target, I+1, MaxInt)); Target := Copy(Target, 1, I-1); end;
    ExpectedPath := TCrossHttpUtils.UrlDecode(Target);
    try
      case Mode of
      bmWire: Valid := DecodeWire and SameParams(Header, Expected);
      bmString: Valid := Header.Decode(HeaderText) and SameParams(Header, Expected);
      bmRequest, bmNewRequest: Valid := Request.ParseHeader(Pointer(Data), Length(Data)) and ValidRequest(Request);
      bmResponse: Valid := Response.ParseHeader(Pointer(ResponseData), Length(ResponseData))
        and SameParams(Response.Header, Expected) and (Response.StatusCode = 200);
      bmEncode: Valid := TEncoding.UTF8.GetString(EncodeHeader) = 'HTTP/1.1 200 OK'#13#10 + Expected.Encode;
      bmEncodeLegacy: Valid := TEncoding.UTF8.GetString(EncodeLegacyHeader) = 'HTTP/1.1 200 OK'#13#10 + Expected.Encode;
      bmFieldEncode:
        begin
          FieldText := Expected.Encode;
          Valid := Header.Decode(FieldText) and SameParams(Header, Expected);
        end;
      bmQueryEncode:
        begin
          // 参数解析、独立回读对象及语义比较均在计时/分配统计之外。
          FieldText := ExpectedQuery.Encode;
          LDecodedQuery := THttpUrlParams.Create;
          try
            Valid := LDecodedQuery.Decode(FieldText);
            // 空query的Decode返回False，Encode仍须是空串且回读集合为空。
            if ExpectedQuery.Count = 0 then Valid := (FieldText = '') and not Valid;
            Valid := Valid and SameParams(LDecodedQuery, ExpectedQuery);
          finally
            LDecodedQuery.Free;
          end;
        end;
      bmCookieEncode:
        begin
          // 解析和回读在计时外；只借用Request持有的Cookie集合，不转移所有权。
          Check(Request.ParseHeader(Pointer(Data), Length(Data)), 'Cookie seed request invalid');
          ExpectedCookies := Request.Cookies;
          FieldText := ExpectedCookies.Encode;
          LDecodedCookies := TRequestCookies.Create;
          try
            Valid := LDecodedCookies.Decode(FieldText);
            if ExpectedCookies.Count = 0 then Valid := (FieldText = '') and not Valid;
            Valid := Valid and SameParams(LDecodedCookies, ExpectedCookies);
          finally
            LDecodedCookies.Free;
          end;
        end;
      bmServerEncode:
        begin
          Check(Request.ParseHeader(@EncodeRequest[1], Length(EncodeRequest)), 'ASCII encode seed request invalid');
          ServerResponse := TCrossHttpResponse.Create(ConnectionObj, RequestOwner, nil);
          ServerResponseOwner := ServerResponse;
          ServerResponseOwner.Header.Assign(Expected);
          // _CreateHeader 会补默认字段；必须先调用，再读取更新后的 Header。
          // 不在同一个比较表达式两侧做有副作用的读取，Delphi/FPC 求值顺序不同。
          ValidatedHeader := HttpHeaderBenchCreate(ServerResponse, 165);
          Valid := TEncoding.UTF8.GetString(ValidatedHeader)
            = 'HTTP/1.1 200 OK'#13#10 + ServerResponseOwner.Header.Encode;
        end;
    else Valid := False; end;
    except
      on E: Exception do
      begin
        if (E is EConvertError) or (E is EArgumentException) or (E is ERangeError)
          or (E.ClassName = 'EEncodingError') or (E.ClassName = 'EUnicodeError') then
        begin
          Writeln('SKIP invalid_output mode=', ParamStr(1), ' fixture=', ParamStr(2),
            ' conversion=', E.ClassName, ': ', E.Message);
          Exit;
        end;
        raise;
      end;
    end;
    if not Valid then
    begin
      Writeln('SKIP invalid_output mode=', ParamStr(1), ' fixture=', ParamStr(2));
      Exit;
    end;
    {$IFDEF HEADER_BYTES}EntrySize := SizeOf(TParamEntry);{$ELSE}EntrySize := SizeOf(TNameValue);{$ENDIF}
    RawBytes := 0;
    {$IFDEF HEADER_BYTES}
    if Mode in [bmRequest, bmNewRequest] then
      for I := 0 to Request.Header.Count-1 do Inc(RawBytes, Length(Request.Header.Items[I].RawValue))
    else if Mode = bmWire then
      for I := 0 to Header.Count-1 do Inc(RawBytes, Length(Header.Items[I].RawValue));
    {$ENDIF}
    if Mode = bmQueryEncode then
      Writeln('VALID query_fields=', ExpectedQuery.Count, ' input_bytes=', Length(Data), ' entry_size=', EntrySize)
    else if Mode = bmCookieEncode then
      Writeln('VALID cookie_fields=', ExpectedCookies.Count, ' input_bytes=', Length(Data), ' entry_size=', EntrySize)
    else
      Writeln('VALID fields=', Expected.Count, ' header_bytes=', Length(Data), ' entry_size=', EntrySize, ' raw_value_bytes=', RawBytes);
    // 纯语义验证不预热、不计时，便于在其它候选准备期间检查 fixture 与所有权。
    if ParamStr(4) = 'validate' then begin Writeln('VALID_ONLY'); Exit; end;
    UnitChecksum := Expected.Count;
    if Mode = bmEncode then UnitChecksum := Length(EncodeHeader)
    else if Mode = bmEncodeLegacy then UnitChecksum := Length(EncodeLegacyHeader)
    else if Mode = bmFieldEncode then UnitChecksum := Length(Expected.Encode)
    else if Mode = bmQueryEncode then UnitChecksum := Length(ExpectedQuery.Encode)
    else if Mode = bmCookieEncode then UnitChecksum := Length(ExpectedCookies.Encode)
    else if Mode = bmServerEncode then UnitChecksum := Length(HttpHeaderBenchCreate(ServerResponse, 165));
    // 只复用对象；TList.Clear 在两个 RTL 都释放容量，不能把它写成容量复用。
    Checksum := 0; Run(2000); // 同进程预热分支和页。
    Checksum := 0;
    if Allocations then StartCounting;
    try
      QueryPerformanceCounter(BeginTick); Run(N); QueryPerformanceCounter(EndTick);
    finally if Allocations then StopCounting; end;
    Check(Checksum = Int64(N) * UnitChecksum, 'Incomplete work recorded');
    Writeln(Format('METRIC n=%d us=%.3f calls=%d bytes=%d checksum=%d',
      [N, (EndTick-BeginTick)*1000000.0/Frequency, AllocationCalls, AllocationBytes, Checksum]));
  finally
    ServerResponseOwner := nil; ServerResponse := nil; Response.Free;
    RequestOwner := nil; Request := nil;
    Connection := nil; Server := nil;
    ExpectedQuery.Free; Header.Free; Expected.Free;
  end;
end;
begin
  try
    FormatSettings.DecimalSeparator := '.';
    Benchmark;
  except on E: Exception do begin
    Writeln('FAIL mode=', ParamStr(1), ' fixture=', ParamStr(2), ' ', E.ClassName, ': ', E.Message);
    Halt(1);
  end; end;
end.
