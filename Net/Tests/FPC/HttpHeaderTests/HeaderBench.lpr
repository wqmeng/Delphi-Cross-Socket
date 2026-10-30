program HeaderBench;
{$I zLib.inc}
{$IFNDEF FPC}{$APPTYPE CONSOLE}{$ENDIF}
uses
  SysUtils, Classes, Windows, Net.CrossHttpParams, Net.CrossHttpServer,
  Net.CrossHttpClient, Net.CrossHttpUtils, Net.CrossSocket.Base, Net.SocketAPI, Utils.StrUtils;
type
  TRequestAccess = class(TCrossHttpRequest);
  TResponseAccess = class(TCrossHttpClientResponse);
  TMode = (bmWire, bmString, bmRequest, bmNewRequest, bmResponse, bmEncode, bmServerEncode, bmEncodeLegacy, bmFieldEncode, bmQueryEncode, bmCookieEncode, bmAssign, bmCompressChoice, bmRouteMatch, bmStatusText, bmMimeType, bmResponseCookies, bmDockKey, bmServerCookies, bmHtmlEncode, bmRoutePath, bmClientEncode);
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

{$IFDEF DOCK_KEY_BENCH}
{$I DockKeyChecks.inc}
{$ENDIF}

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
  Header, Expected, AssignSource: THttpHeader;
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
  ExpectedCompressType, SelectedCompressType: TCompressType;
  RouteTree: TCrossHttpRouterTree;
  StaticRouter, WildcardRouter, RegexRouter, ExpectedRouter, MatchedRouter: IRouter;
  RouteSegments, ExpectedSegments: TArray<string>;
  RoutePath: string;
  LookupStatusCode: Integer;
  LookupFileName, ExpectedLookupText: string;
  HtmlInput, ExpectedHtml: string;
  ServerCookieFixture, ServerCookieName, ServerCookieValue, ServerCookiePrefix: string;
  ServerCookieCount: Integer;
  ServerCookie: TResponseCookie;
  {$IFDEF COOKIE_RESPONSE_BENCH}
  CookieClientObj: TCrossHttpClient;
  CookieClientOwner: ICrossHttpClient;
  CookieConnection: ICrossHttpClientConnection;
  CookieLiveBefore, ExpectedCookieCount: Integer;
  CookieName, CookieValue, CookieFixture: string;
  {$ENDIF}
  {$IFDEF CLIENT_ENCODE_BENCH}
  ClientRequestObj: TCrossHttpClientRequest;
  ClientRequestOwner: ICrossHttpClientRequest;
  ClientConnectionObj: TCrossHttpClientConnection;
  ClientTarget, ExpectedClientLine: string;
  {$ENDIF}
  {$IFDEF DOCK_KEY_BENCH}
  DockSocket: TCrossHttpClientSocket;
  DockProtocol, DockHost, ExpectedDockKey: string;
  DockPort: Word;
  {$ENDIF}

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
    LCompressType: TCompressType;
    LMatchedRouter: IRouter;
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
      bmResponseCookies: for J := 1 to Count do
        begin
          Response.Cookies.Clear;
          if Response.ParseHeader(Pointer(ResponseData), Length(ResponseData)) then Inc(Checksum, Response.Header.Count);
        end;
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
      bmAssign: for J := 1 to Count do
        begin Header.Assign(AssignSource); Inc(Checksum, Header.Count); end;
      bmStatusText: for J := 1 to Count do
        begin EncodedFields := TCrossHttpUtils.GetHttpStatusText(LookupStatusCode); Inc(Checksum, Length(EncodedFields)); end;
      bmMimeType: for J := 1 to Count do
        begin EncodedFields := TCrossHttpUtils.GetFileMIMEType(LookupFileName); Inc(Checksum, Length(EncodedFields)); end;
      bmHtmlEncode: for J := 1 to Count do
        begin EncodedFields := TCrossHttpUtils.HtmlEncode(HtmlInput); Inc(Checksum, Length(EncodedFields)); end;
      {$IFDEF CLIENT_ENCODE_BENCH}
      bmClientEncode: for J := 1 to Count do
        begin
          Encoded := HttpHeaderBenchClientCreate(ClientConnectionObj, ClientRequestObj);
          Inc(Checksum, Length(Encoded));
        end;
      {$ENDIF}
      {$IFDEF DOCK_KEY_BENCH}
      bmDockKey: for J := 1 to Count do
        begin EncodedFields := HttpHeaderBenchDockKey(DockSocket, DockProtocol, DockHost, DockPort); Inc(Checksum, Length(EncodedFields)); end;
      {$ENDIF}
      bmRoutePath: for J := 1 to Count do
        begin RouteSegments := TCrossHttpRouterTree.ParsePath(RoutePath); Inc(Checksum, Length(RouteSegments)); end;
      bmRouteMatch: for J := 1 to Count do
        begin
          Request.Params.Clear;
          if RouteTree.MatchRouter(RouteSegments, RequestOwner, LMatchedRouter) then
            Inc(Checksum, 2)
          else
            Inc(Checksum);
        end;
      bmCompressChoice: for J := 1 to Count do
        begin
          HttpHeaderBenchCompress(ServerResponse, 1024, LCompressType);
          Inc(Checksum, Ord(LCompressType) + 1);
        end;
      bmServerEncode, bmServerCookies: for J := 1 to Count do
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
  else if ParamStr(1) = 'assign' then Mode := bmAssign
  else if ParamStr(1) = 'compress-choice' then Mode := bmCompressChoice
  else if ParamStr(1) = 'route-path' then Mode := bmRoutePath
  else if ParamStr(1) = 'route-match' then Mode := bmRouteMatch
  else if ParamStr(1) = 'status-text' then Mode := bmStatusText
  else if ParamStr(1) = 'mime-type' then Mode := bmMimeType
  else if ParamStr(1) = 'response-cookies' then Mode := bmResponseCookies
  else if ParamStr(1) = 'dock-key' then Mode := bmDockKey
  else if ParamStr(1) = 'server-cookies' then Mode := bmServerCookies
  else if ParamStr(1) = 'html-encode' then Mode := bmHtmlEncode
  else if ParamStr(1) = 'client-encode' then Mode := bmClientEncode
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
  RouteTree := nil;
  {$IFDEF COOKIE_RESPONSE_BENCH}
  CookieClientObj := nil;
  CookieLiveBefore := 0;
  {$ENDIF}
  {$IFDEF CLIENT_ENCODE_BENCH}
  ClientRequestObj := nil;
  ClientConnectionObj := nil;
  {$ENDIF}
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
      bmAssign:
        begin
          // 来源的字节解析及完整记录核对都在计时外，循环只测清空和复制集合。
          Check(Request.ParseHeader(Pointer(Data), Length(Data)), 'Assign seed request invalid');
          AssignSource := Request.Header;
          Header.Assign(AssignSource);
          Valid := SameParams(Header, Expected);
          {$IFDEF HEADER_BYTES}
          for I := 0 to Header.Count - 1 do
            Valid := Valid and (Header.Items[I].HasRawValue = AssignSource.Items[I].HasRawValue)
              and (Length(Header.Items[I].RawValue) = Length(AssignSource.Items[I].RawValue))
              and (Pointer(Header.Items[I].RawValue) = Pointer(AssignSource.Items[I].RawValue));
          {$ENDIF}
        end;
      bmCompressChoice:
        begin
          // 三个显式fixture使用固定预期，不复用被测协商逻辑生成答案。
          if Expected['Accept-Encoding'] = 'gzip, deflate' then
            ExpectedCompressType := ctGZip
          else if Expected['Accept-Encoding'] = 'br, gzip;q=0, deflate;q=1, identity;q=0, gzip;q=0' then
            ExpectedCompressType := ctDeflate
          else
          begin
            Check(Expected['Accept-Encoding'] = '', 'Unsupported compression fixture');
            ExpectedCompressType := ctNone;
          end;
          Check(Request.ParseHeader(Pointer(Data), Length(Data)), 'Compression seed request invalid');
          Server.Compressible := True;
          Server.MinCompressSize := 1;
          ConnectionObj.ConnectStatus := csConnected;
          ServerResponse := TCrossHttpResponse.Create(ConnectionObj, RequestOwner, nil);
          ServerResponseOwner := ServerResponse;
          ServerResponseOwner.ContentType := 'application/json';
          Valid := HttpHeaderBenchCompress(ServerResponse, 1024, SelectedCompressType);
          Check((Valid = (ExpectedCompressType <> ctNone))
            and (SelectedCompressType = ExpectedCompressType), 'Compression selection mismatch');
          // 确认真实连接门槛有效，再恢复connected；不能把早退路径误当协商成本。
          ConnectionObj.ConnectStatus := csDisconnected;
          Valid := HttpHeaderBenchCompress(ServerResponse, 1024, SelectedCompressType);
          Check(not Valid and (SelectedCompressType = ctNone), 'Disconnected compression gate failed');
          ConnectionObj.ConnectStatus := csConnected;
          Valid := HttpHeaderBenchCompress(ServerResponse, 1024, SelectedCompressType);
          Valid := (Valid = (ExpectedCompressType <> ctNone))
            and (SelectedCompressType = ExpectedCompressType);
        end;
      bmRoutePath:
        begin
          RoutePath := ExpectedPath;
          // 旧实现作判据；只计时ParsePath，不把请求解码和树匹配混入。
          if (RoutePath = '') or (RoutePath = '/') then ExpectedSegments := ['']
          else
          begin
            ExpectedSegments := RoutePath.Split(['/'], TStringSplitOptions.ExcludeEmpty);
            if ExpectedSegments = nil then ExpectedSegments := [''];
          end;
          RouteSegments := TCrossHttpRouterTree.ParsePath(RoutePath);
          Valid := Length(RouteSegments) = Length(ExpectedSegments);
          if Valid then
            for I := 0 to High(ExpectedSegments) do
              Valid := Valid and (RouteSegments[I] = ExpectedSegments[I]);
        end;
      bmRouteMatch:
        begin
          Check(Request.ParseHeader(Pointer(Data), Length(Data)), 'Route seed request invalid');
          RouteTree := TCrossHttpRouterTree.Create;
          StaticRouter := TRouter.Create('GET');
          WildcardRouter := TRouter.Create('*');
          RegexRouter := TRouter.Create('(GET|POST)');
          RouteTree.AddRouter('GET', '/fixed', StaticRouter);
          RouteTree.AddRouter('*', '/files/*', WildcardRouter);
          RouteTree.AddRouter('(GET|POST)', '/items/:id([0-9]+)', RegexRouter);
          RouteSegments := TCrossHttpRouterTree.ParsePath(Request.Path);
          if Request.Path = '/fixed' then ExpectedRouter := StaticRouter
          else if Request.Path = '/files/a/b' then ExpectedRouter := WildcardRouter
          else if Request.Path = '/items/42' then ExpectedRouter := RegexRouter
          else
          begin
            Check(Request.Path = '/missing', 'Unsupported route fixture');
            ExpectedRouter := nil;
          end;
          Valid := RouteTree.MatchRouter(RouteSegments, RequestOwner, MatchedRouter);
          Valid := (Valid = (ExpectedRouter <> nil)) and (MatchedRouter = ExpectedRouter);
          if ExpectedRouter = WildcardRouter then
            Valid := Valid and (Request.Params['*'] = 'a/b?x=1');
          if ExpectedRouter = RegexRouter then
            Valid := Valid and (Request.Params['id'] = '42');
        end;
      bmStatusText:
        begin
          LookupStatusCode := StrToInt(Copy(ExpectedPath, 2, MaxInt));
          case LookupStatusCode of
            200: ExpectedLookupText := 'OK';
            511: ExpectedLookupText := 'Network Authentication Required';
            999: ExpectedLookupText := '999';
          else raise Exception.Create('Unsupported status fixture'); end;
          Valid := TCrossHttpUtils.GetHttpStatusText(LookupStatusCode) = ExpectedLookupText;
        end;
      bmMimeType:
        begin
          LookupFileName := ExpectedPath;
          if ExtractFileName(ParamStr(2)) = 'mime-extension-only.bin' then
            LookupFileName := Copy(ExpectedPath, 2, MaxInt);
          if LookupFileName = '/file.ez' then ExpectedLookupText := 'application/andrew-inset'
          else if LookupFileName = '/file.txt' then ExpectedLookupText := 'text/plain'
          else if LookupFileName = '.txt' then
          begin
            if ExtractFileExt(LookupFileName) = '.txt' then ExpectedLookupText := 'text/plain'
            else ExpectedLookupText := 'application/octet-stream';
          end else
          begin
            Check(LookupFileName = '/file.unknown-extension', 'Unsupported MIME fixture');
            ExpectedLookupText := 'application/octet-stream';
          end;
          Valid := TCrossHttpUtils.GetFileMIMEType(LookupFileName) = ExpectedLookupText;
        end;
      bmClientEncode:
        begin
          {$IFDEF CLIENT_ENCODE_BENCH}
          // 构造、URL解析和首次默认字段补齐不计时；循环只测真实完整请求头生成。
          CookieLiveBefore := HttpSendTestLiveConnections;
          CookieClientObj := TCrossHttpClient.Create(1);
          CookieClientOwner := CookieClientObj;
          StopHttpTimeoutTimerForTest(CookieClientObj);
          Check(CookieClientObj.AutoUrlEncode, 'Client encoding must be enabled');
          CookieConnection := NewHttpSendTestConnection(CookieClientObj);
          ClientConnectionObj := CookieConnection as TCrossHttpClientConnection;
          ClientTarget := Copy(FirstLine, Pos(' ', FirstLine) + 1, MaxInt);
          ClientTarget := Copy(ClientTarget, 1, Pos(' ', ClientTarget) - 1);
          ClientRequestObj := TCrossHttpClientRequest.Create('GET',
            'http://127.0.0.1' + ClientTarget, Expected, Pointer(nil), 0, nil, nil, nil);
          ClientRequestOwner := ClientRequestObj;
          Check(SameParams(ClientRequestObj.Query, ExpectedQuery), 'Client query seed mismatch');
          // 不从被测输出反推请求行；沿用未修改的编码器组合独立预期。
          ExpectedClientLine := 'GET ' + TCrossHttpUtils.UrlEncode(ExpectedPath, ['/', ':', '@'], True);
          if ExpectedQuery.Count > 0 then
            ExpectedClientLine := ExpectedClientLine + '?' + ExpectedQuery.Encode;
          ExpectedClientLine := ExpectedClientLine + ' HTTP/1.1'#13#10;
          ValidatedHeader := HttpHeaderBenchClientCreate(ClientConnectionObj, ClientRequestObj);
          FieldText := TEncoding.UTF8.GetString(ValidatedHeader);
          Valid := Pos(ExpectedClientLine, FieldText) = 1;
          Valid := Valid and Header.Decode(Copy(FieldText, Length(ExpectedClientLine) + 1, MaxInt));
          Valid := Valid and SameParams(Header, ClientRequestObj.Header);
          Check(Copy(FieldText, Length(FieldText) - 3, 4) = #13#10#13#10,
            'Client header missing final empty line');
          {$ELSE}
          raise Exception.Create('Client header benchmark requires existing test factory');
          {$ENDIF}
        end;
      bmResponseCookies:
        begin
          {$IFDEF COOKIE_RESPONSE_BENCH}
          // 复用已有测试连接工厂：具有真实FHost，定时器及构造均在计时外。
          CookieLiveBefore := HttpSendTestLiveConnections;
          CookieClientObj := TCrossHttpClient.Create(1);
          CookieClientOwner := CookieClientObj;
          StopHttpTimeoutTimerForTest(CookieClientObj);
          CookieConnection := NewHttpSendTestConnection(CookieClientObj);
          Response.Free;
          Response := nil;
          Response := TResponseAccess.Create(CookieConnection);
          Valid := Response.ParseHeader(Pointer(ResponseData), Length(ResponseData))
            and SameParams(Response.Header, Expected) and (Response.StatusCode = 200);
          CookieFixture := ExtractFileName(ParamStr(2));
          if CookieFixture = 'response-none.bin' then ExpectedCookieCount := 0
          else if CookieFixture = 'response-many.bin' then ExpectedCookieCount := 16
          else
          begin
            Check((CookieFixture = 'response-one.bin') or (CookieFixture = 'response-early.bin'), 'Unsupported response Cookie fixture');
            ExpectedCookieCount := 1;
          end;
          Check(Response.Cookies.Count = ExpectedCookieCount, 'Response Cookie count mismatch');
          for I := 0 to ExpectedCookieCount - 1 do
          begin
            if ExpectedCookieCount = 1 then
            begin CookieName := 'session'; CookieValue := 'one'; end
            else
            begin CookieName := 'c' + IntToStr(I mod 4); CookieValue := 'value-' + IntToStr(I); end;
            Check((Response.Cookies[I].Name = CookieName) and (Response.Cookies[I].Value = CookieValue)
              and (Response.Cookies[I].Domain = '127.0.0.1'), 'Response Cookie value/order/domain mismatch');
          end;
          {$ELSE}
          raise Exception.Create('Response Cookie benchmark requires existing test factory');
          {$ENDIF}
        end;
      bmDockKey:
        begin
          {$IFDEF DOCK_KEY_BENCH}
          CookieLiveBefore := HttpSendTestLiveConnections;
          CookieClientObj := TCrossHttpClient.Create(1);
          CookieClientOwner := CookieClientObj;
          StopHttpTimeoutTimerForTest(CookieClientObj);
          CookieConnection := NewHttpSendTestConnection(CookieClientObj);
          DockSocket := CookieConnection.Owner as TCrossHttpClientSocket;
          Writeln('DOCK_CHECKS ', RunDockKeyChecks(DockSocket));
          DockProtocol := Expected['X-Key-Protocol'];
          DockHost := Expected['X-Key-Host'];
          DockPort := StrToInt(Expected['X-Key-Port']);
          ExpectedDockKey := TStrUtils.Format('%s://%s:%d', [DockProtocol, DockHost, DockPort]);
          Valid := HttpHeaderBenchDockKey(DockSocket, DockProtocol, DockHost, DockPort) = ExpectedDockKey;
          {$ELSE}
          raise Exception.Create('Dock key benchmark requires existing test factory');
          {$ENDIF}
        end;
      bmServerCookies:
        begin
          Check(Request.ParseHeader(@EncodeRequest[1], Length(EncodeRequest)), 'Server Cookie seed invalid');
          ServerResponse := TCrossHttpResponse.Create(ConnectionObj, RequestOwner, nil);
          ServerResponseOwner := ServerResponse;
          ServerResponseOwner.Header.Assign(Expected);
          ServerCookieFixture := ExtractFileName(ParamStr(2));
          ServerCookiePrefix := '';
          if ServerCookieFixture = 'server-cookie-none.bin' then ServerCookieCount := 0
          else if ServerCookieFixture = 'server-cookie-mixed4.bin' then ServerCookieCount := 4
          else if ServerCookieFixture = 'server-cookie-mixed17.bin' then ServerCookieCount := 17
          else
          begin
            Check((ServerCookieFixture = 'server-cookie-bare.bin') or
              (ServerCookieFixture = 'server-cookie-full.bin') or
              (ServerCookieFixture = 'server-cookie-long.bin'), 'Unsupported server Cookie fixture');
            ServerCookieCount := 1;
          end;
          for I := 0 to ServerCookieCount - 1 do
          begin
            if ServerCookieCount > 1 then
            begin
              ServerCookieName := 'c' + IntToStr(I mod 3);
              if I mod 3 = 0 then ServerCookieValue := '' else ServerCookieValue := 'v' + IntToStr(I);
            end else
            begin
              ServerCookieName := 'session';
              if ServerCookieFixture = 'server-cookie-long.bin' then
                ServerCookieValue := StringOfChar('x', 8192)
              else ServerCookieValue := 'value';
            end;
            if (ServerCookieFixture = 'server-cookie-full.bin') or
              ((ServerCookieCount > 1) and (I mod 2 = 1)) then
            begin
              ServerCookie := TResponseCookie.Create(ServerCookieName, ServerCookieValue, 60,
                '/scope', 'example.test', True, True);
              FieldText := ServerCookieName + '=' + ServerCookieValue
                + '; Max-Age=60; Path=/scope; Domain=example.test; HttpOnly; Secure';
            end else if ServerCookieFixture = 'server-cookie-bare.bin' then
            begin
              ServerCookie := TResponseCookie.Create(ServerCookieName, ServerCookieValue, 0, '', '', False, False);
              FieldText := ServerCookieName + '=' + ServerCookieValue;
            end else
            begin
              ServerCookie := TResponseCookie.Create(ServerCookieName, ServerCookieValue, 0, '/', '', False, False);
              FieldText := ServerCookieName + '=' + ServerCookieValue + '; Path=/';
            end;
            ServerResponseOwner.Cookies.Add(ServerCookie);
            // 固定属性预期独立拼装，不调用被测Cookie.Encode。
            ServerCookiePrefix := ServerCookiePrefix + 'Set-Cookie: ' + FieldText + #13#10;
          end;
          ValidatedHeader := HttpHeaderBenchCreate(ServerResponse, 165);
          Valid := (ServerResponseOwner.Cookies.Count = ServerCookieCount)
            and (TEncoding.UTF8.GetString(ValidatedHeader) = 'HTTP/1.1 200 OK'#13#10
              + ServerCookiePrefix + ServerResponseOwner.Header.Encode);
        end;
      bmHtmlEncode:
        begin
          HtmlInput := Expected['X-Html-Text'];
          if ExtractFileName(ParamStr(2)) = 'html-plain.bin' then
            ExpectedHtml := 'File_2026-report.txt'
          else if ExtractFileName(ParamStr(2)) = 'html-markup.bin' then
            ExpectedHtml := '&lt;a href=&quot;x&amp;y&quot;&gt;tag&lt;/a&gt;'
          else
          begin
            Check(ExtractFileName(ParamStr(2)) = 'html-long-tail.bin', 'Unsupported HTML fixture');
            ExpectedHtml := StringOfChar('x', 8192) + '&amp;';
          end;
          Valid := TCrossHttpUtils.HtmlEncode(HtmlInput) = ExpectedHtml;
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
    else if Mode in [bmWire, bmAssign] then
      for I := 0 to Header.Count-1 do Inc(RawBytes, Length(Header.Items[I].RawValue));
    {$ENDIF}
    if Mode = bmServerCookies then
      Writeln('VALID response_cookies=', ServerCookieCount, ' input_fields=', Expected.Count)
    else if Mode = bmClientEncode then
      Writeln('VALID client_query_fields=', ExpectedQuery.Count, ' encoded_bytes=', Length(ValidatedHeader), ' input_bytes=', Length(Data))
    else if Mode = bmQueryEncode then
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
    else if Mode = bmCompressChoice then UnitChecksum := Ord(ExpectedCompressType) + 1
    else if Mode in [bmServerEncode, bmServerCookies] then UnitChecksum := Length(HttpHeaderBenchCreate(ServerResponse, 165))
    {$IFDEF CLIENT_ENCODE_BENCH}
    else if Mode = bmClientEncode then UnitChecksum := Length(ValidatedHeader)
    {$ENDIF}
    else if Mode = bmRoutePath then UnitChecksum := Length(ExpectedSegments)
    else if Mode = bmRouteMatch then
    begin
      if ExpectedRouter = nil then UnitChecksum := 1 else UnitChecksum := 2;
    end
    else if Mode in [bmStatusText, bmMimeType] then UnitChecksum := Length(ExpectedLookupText)
    else if Mode = bmHtmlEncode then UnitChecksum := Length(ExpectedHtml)
    {$IFDEF DOCK_KEY_BENCH}
    else if Mode = bmDockKey then UnitChecksum := Length(ExpectedDockKey)
    {$ENDIF}
    ;
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
    RouteTree.Free;
    ServerResponseOwner := nil; ServerResponse := nil; Response.Free;
    {$IFDEF CLIENT_ENCODE_BENCH}
    // 包装在每次调用的finally中清掉借用指针，先释放请求再关闭测试连接。
    ClientRequestOwner := nil;
    ClientRequestObj := nil;
    ClientConnectionObj := nil;
    {$ENDIF}
    {$IFDEF COOKIE_RESPONSE_BENCH}
    if CookieConnection <> nil then
    begin
      CookieConnection.Close;
      CookieConnection := nil;
      Check(HttpSendTestLiveConnections = CookieLiveBefore, 'Response Cookie connection leaked');
    end;
    CookieClientOwner := nil;
    CookieClientObj := nil;
    {$ENDIF}
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
