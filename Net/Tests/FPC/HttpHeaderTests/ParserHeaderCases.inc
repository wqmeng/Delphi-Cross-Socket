// 通过公开Parser回调检查完整头和分片路径的可观察结果，不检查私有流或指针身份。
type
  TParserHeaderProbe = class
  private
    FParser: ICrossHttpParser;
    FFields: THttpHeader;
    procedure HeaderData(const AData: Pointer; const ASize: Integer);
    function HeaderValues(const AName: string; out AValues: TArray<string>): Boolean;
    procedure BodyBegin;
    procedure BodyData(const AData: Pointer; const ASize: Integer);
    procedure BodyEnd;
    procedure Succeeded;
    procedure Failed(const ACode: Integer; const AError: string);
  public
    Headers: TArray<TBytes>;
    Body: TBytes;
    Begins, Ends, Successes, Failures, FailureCode: Integer;
    constructor Create(const AMode: TCrossHttpParseMode);
    destructor Destroy; override;
    function DecodePart(const AData: TBytes; const AOffset, ASize: Integer): Integer;
    procedure SetHeaderLimit(const ASize: Integer);
  end;

constructor TParserHeaderProbe.Create(const AMode: TCrossHttpParseMode);
begin
  inherited Create;
  FFields := THttpHeader.Create;
  FParser := TCrossHttpParser.Create(AMode);
  FParser.OnHeaderData := HeaderData;
  FParser.OnGetHeaderValue := HeaderValues;
  FParser.OnBodyBegin := BodyBegin;
  FParser.OnBodyData := BodyData;
  FParser.OnBodyEnd := BodyEnd;
  FParser.OnParseSuccess := Succeeded;
  FParser.OnParseFailed := Failed;
end;

destructor TParserHeaderProbe.Destroy;
begin
  FParser := nil;
  FFields.Free;
  inherited;
end;

procedure TParserHeaderProbe.HeaderData(const AData: Pointer; const ASize: Integer);
var
  LText: AnsiString;
  LLineEnd, LIndex: Integer;
begin
  LIndex := Length(Headers);
  SetLength(Headers, LIndex + 1);
  SetLength(Headers[LIndex], ASize);
  if ASize > 0 then Move(AData^, Headers[LIndex][0], ASize);
  // 首行只用于找字段起点；字段语义仍交给生产Header.Decode，不复刻候选的收集逻辑。
  SetString(LText, PAnsiChar(AData), ASize);
  LLineEnd := Pos(AnsiString(#13#10), LText);
  if (LLineEnd = 0) or not FFields.Decode(PByte(AData) + LLineEnd + 1,
    ASize - LLineEnd - 1) then
    (FParser as TCrossHttpParser).Fail(400, '测试收到无效Header');
end;

function TParserHeaderProbe.HeaderValues(const AName: string;
  out AValues: TArray<string>): Boolean;
begin
  Result := FFields.GetHeaderValues(AName, AValues);
end;

procedure TParserHeaderProbe.BodyBegin;
begin
  Inc(Begins);
end;

procedure TParserHeaderProbe.BodyData(const AData: Pointer; const ASize: Integer);
var
  LOldSize: Integer;
begin
  LOldSize := Length(Body);
  SetLength(Body, LOldSize + ASize);
  if ASize > 0 then Move(AData^, Body[LOldSize], ASize);
end;

procedure TParserHeaderProbe.BodyEnd;
begin
  Inc(Ends);
end;

procedure TParserHeaderProbe.Succeeded;
begin
  Inc(Successes);
end;

procedure TParserHeaderProbe.Failed(const ACode: Integer; const AError: string);
begin
  Inc(Failures);
  FailureCode := ACode;
end;

function TParserHeaderProbe.DecodePart(const AData: TBytes;
  const AOffset, ASize: Integer): Integer;
var
  P: Pointer;
  LRemaining: Integer;
begin
  // 零长度片段代表不调用Decode；本组只比较真实到达字节的分片边界。
  if ASize = 0 then Exit(0);
  P := @AData[AOffset];
  LRemaining := ASize;
  FParser.Decode(P, LRemaining);
  Result := ASize - LRemaining;
  if PByte(P) <> PByte(@AData[AOffset]) + Result then
    raise Exception.Create('Parser返回指针与剩余长度不一致');
end;

procedure TParserHeaderProbe.SetHeaderLimit(const ASize: Integer);
begin
  FParser.MaxHeaderSize := ASize;
end;

function RunParserHeaderTests: Integer;
const
  BadHeaders: array[0..3] of string = (
    'GET / HTTP/1.1'#10'Host: localhost'#13#10#13#10,
    'GET / HTTP/1.1'#13'X: value'#13#10#13#10,
    'GET / HTTP/1.1'#13#13#10'Host: localhost'#13#10#13#10,
    'GET / HTTP/1.1'#13#10#13'X: value'#13#10#13#10);
var
  LChecks, I, J, LMode: Integer;
  LHeader, LBody, LWire, LSecond, LExpected: TBytes;
  LProbe: TParserHeaderProbe;
  LConsumed: Integer;
  LCase: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('Parser Header ' + LCase + ': ' + AMessage);
  end;

  function Join(const A, B: TBytes): TBytes;
  begin
    SetLength(Result, Length(A) + Length(B));
    if Length(A) > 0 then Move(A[0], Result[0], Length(A));
    if Length(B) > 0 then Move(B[0], Result[Length(A)], Length(B));
  end;

  procedure RunSplit(const AMode: TCrossHttpParseMode; const AHeader,
    ABody: TBytes; const ASplit, ALimit: Integer; const AValid: Boolean);
  var
    P: TParserHeaderProbe;
    B: TBytes;
  begin
    B := Join(AHeader, ABody);
    P := TParserHeaderProbe.Create(AMode);
    try
      P.SetHeaderLimit(ALimit);
      Check(P.DecodePart(B, 0, ASplit) = ASplit, '首片输入全部消费');
      Check(P.DecodePart(B, ASplit, Length(B) - ASplit) = Length(B) - ASplit,
        '后片输入全部消费');
      if AValid then
      begin
        Check((P.Failures = 0) and (P.Successes = 1), '仅发布一次成功');
        Check(Length(P.Headers) = 1, '仅回调一份完整Header');
        Check(SameBytes(P.Headers[0], AHeader), 'Header字节完全一致，不混入正文');
        Check(SameBytes(P.Body, ABody), '正文内容与NUL/CRLF字节保持');
        Check((P.Begins = Ord(Length(ABody) > 0)) and
          (P.Ends = Ord(Length(ABody) > 0)), '正文开始/结束通知次数');
      end else
      begin
        Check((P.Failures = 1) and (P.FailureCode = 400) and (P.Successes = 0),
          '非法输入仅失败一次');
        Check((Length(P.Headers) = 0) and (Length(P.Body) = 0),
          'CRLF或尺寸拒绝发生在发布Header之前');
      end;
    finally
      P.Free;
    end;
  end;

  procedure CheckOwnedRequest(const ASplit: Boolean);
  var
    F: TFixture;
    LHeld: ICrossHttpRequest;
    LInput, LFirst, LLast: AnsiString;
    LOriginal, LSnapshot: TBytes;
    LText: string;
    LCalls: Integer;
  begin
    LText := 'GET /borrow HTTP/1.1'#13#10'Host: localhost'#13#10
      + 'X-Text: 中文'#$D83D#$DE00#13#10#13#10;
    LOriginal := Bytes(LText);
    LInput := Wire(LText);
    LCalls := 0;
    F := TFixture.Create;
    try
      F.Server.Get('/borrow', TCrossHttpRouterProc(
        procedure(const R: ICrossHttpRequest; const S: ICrossHttpResponse;
          var Handled: Boolean)
        begin
          Handled := True;
          Inc(LCalls);
          LHeld := R;
          S.SendNoCompress('ok');
        end));
      if ASplit then
      begin
        LFirst := Copy(LInput, 1, Length(LInput) - 1);
        LLast := Copy(LInput, Length(LInput), 1);
        F.ConnectionObj.Feed(LFirst);
        F.ConnectionObj.Feed(LLast);
        FillChar(PAnsiChar(LFirst)^, Length(LFirst), Ord('x'));
        FillChar(PAnsiChar(LLast)^, Length(LLast), Ord('x'));
      end else
        F.ConnectionObj.Feed(LInput);
      FillChar(PAnsiChar(LInput)^, Length(LInput), Ord('x'));
      Check((LCalls = 1) and (LHeld <> nil), '真实Server只发布一次请求');
      Check(SameBytes(LHeld.RawRequestHeader, LOriginal), '覆写接收输入后raw快照仍自有');
      Check(LHeld.Header['X-Text'] = '中文'#$D83D#$DE00, '输入覆写不污染已解码字段');
      LSnapshot := LHeld.RawRequestHeader;
      LSnapshot[0] := Ord('x');
      Check(SameBytes(LHeld.RawRequestHeader, LOriginal), 'raw属性副本仍保持防御性复制');
    finally
      LHeld := nil;
      F.Free;
    end;
  end;

begin
  LChecks := 0;
  LBody := TBytes.Create(Ord('A'), 0, Ord('B'), 13, 10);
  // 两种模式都覆盖同包正文，以及每个Header/正文分界处的两片输入。
  for LMode := 0 to 1 do
  begin
    if LMode = 0 then LCase := 'server' else LCase := 'client';
    if LMode = 0 then
      LHeader := Bytes('POST /upload HTTP/1.1'#13#10'Host: localhost'#13#10
        + 'X-Text: 中文'#$D83D#$DE00#13#10'Content-Length: 5'#13#10#13#10)
    else
      LHeader := Bytes('HTTP/1.1 200 OK'#13#10'X-Text: 中文'#$D83D#$DE00#13#10
        + 'Content-Length: 5'#13#10#13#10);
    for I := 0 to Length(LHeader) + Length(LBody) do
      RunSplit(TCrossHttpParseMode(LMode), LHeader, LBody, I, 0, True);
  end;

  LCase := 'header-limit';
  LHeader := Bytes('GET /limits HTTP/1.1'#13#10'Host: localhost'#13#10#13#10);
  for I := 0 to Length(LHeader) do
  begin
    RunSplit(pmServer, LHeader, nil, I, Length(LHeader), True);
    RunSplit(pmServer, LHeader, nil, I, Length(LHeader) - 1, False);
  end;
  for J := Low(BadHeaders) to High(BadHeaders) do
  begin
    LCase := 'CRLF ' + IntToStr(J);
    LHeader := Bytes(BadHeaders[J]);
    for I := 0 to Length(LHeader) do
      RunSplit(pmServer, LHeader, nil, I, 0, False);
  end;

  // Decode一次只结束一条消息；同包下一条必须由调用者再次提交剩余区间。
  LCase := 'pipeline';
  LHeader := Bytes('POST /first HTTP/1.1'#13#10'Content-Length: 5'#13#10#13#10);
  LSecond := Bytes('GET /second HTTP/1.1'#13#10'Host: localhost'#13#10#13#10);
  LWire := Join(Join(LHeader, LBody), LSecond);
  LProbe := TParserHeaderProbe.Create(pmServer);
  try
    LConsumed := LProbe.DecodePart(LWire, 0, Length(LWire));
    Check(LConsumed = Length(LHeader) + Length(LBody), '首条消费位置停在下一条之前');
    Check((LProbe.Successes = 1) and (Length(LProbe.Headers) = 1), '首轮只发布第一条');
    LExpected := Copy(LWire, LConsumed, Length(LWire) - LConsumed);
    Check(SameBytes(LExpected, LSecond), '未消费尾字节保持完整');
    Check(LProbe.DecodePart(LWire, LConsumed, Length(LWire) - LConsumed) = Length(LSecond),
      '再次提交后消费第二条');
    Check((LProbe.Successes = 2) and (LProbe.Failures = 0)
      and (Length(LProbe.Headers) = 2), '两条消息各成功一次');
    Check(SameBytes(LProbe.Headers[0], LHeader) and SameBytes(LProbe.Headers[1], LSecond),
      '两份Header保持各自精确范围');
    Check(SameBytes(LProbe.Body, LBody) and (LProbe.Begins = 1) and (LProbe.Ends = 1),
      '下一条不污染首条正文或重复通知');
  finally
    LProbe.Free;
  end;

  LCase := 'owned-complete';
  CheckOwnedRequest(False);
  LCase := 'owned-split';
  CheckOwnedRequest(True);
  Result := LChecks;
end;
