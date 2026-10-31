// 只适配Parser的of-object回调；请求对象和输入数据均由Benchmark保持到本对象释放。
// 每次完成仍由真实Parser.Decode执行_Reset，不手工清理/复用它的私有缓冲。
type
  THeaderParserBench = class
  private
    FParser: TCrossHttpParser;
    FRequest: TRequestAccess;
    FBegins, FHeaders, FSuccesses, FFailures, FBodyEvents, FHeaderBytes: Int64;
    FError: string;
    procedure ParseBegin;
    procedure HeaderData(const ADataPtr: Pointer; const ADataSize: Integer);
    function HeaderValues(const AName: string; out AValues: TArray<string>): Boolean;
    procedure BodyEvent;
    procedure BodyData(const ADataPtr: Pointer; const ADataSize: Integer);
    procedure ParseSuccess;
    procedure ParseFailed(const ACode: Integer; const AError: string);
  public
    constructor Create(const ARequest: TRequestAccess);
    destructor Destroy; override;
    procedure ClearCounts;
    procedure CheckCounts(const ARequests, AHeaderSize: Integer);
    procedure Decode(const AData: Pointer; const ASize: Integer; const AMode: TMode);
  end;

constructor THeaderParserBench.Create(const ARequest: TRequestAccess);
begin
  inherited Create;
  FRequest := ARequest;
  FParser := TCrossHttpParser.Create(pmServer);
  FParser.OnParseBegin := ParseBegin;
  FParser.OnHeaderData := HeaderData;
  FParser.OnGetHeaderValue := HeaderValues;
  FParser.OnBodyBegin := BodyEvent;
  FParser.OnBodyData := BodyData;
  FParser.OnBodyEnd := BodyEvent;
  FParser.OnParseSuccess := ParseSuccess;
  FParser.OnParseFailed := ParseFailed;
end;

destructor THeaderParserBench.Destroy;
begin
  FreeAndNil(FParser);
  FRequest := nil;
  inherited;
end;

procedure THeaderParserBench.ParseBegin;
begin
  Inc(FBegins);
end;

procedure THeaderParserBench.HeaderData(const ADataPtr: Pointer; const ADataSize: Integer);
begin
  Inc(FHeaders);
  Inc(FHeaderBytes, ADataSize);
  // 同步走真实请求头解析；保存自有raw的行为仍由生产代码负责。
  if not FRequest.ParseHeader(ADataPtr, ADataSize) then
    FParser.Fail(400, 'Benchmark request header rejected');
end;

function THeaderParserBench.HeaderValues(const AName: string;
  out AValues: TArray<string>): Boolean;
begin
  Result := FRequest.Header.GetHeaderValues(AName, AValues);
end;

procedure THeaderParserBench.BodyEvent;
begin
  Inc(FBodyEvents);
end;

procedure THeaderParserBench.BodyData(const ADataPtr: Pointer; const ADataSize: Integer);
begin
  Inc(FBodyEvents);
end;

procedure THeaderParserBench.ParseSuccess;
begin
  Inc(FSuccesses);
end;

procedure THeaderParserBench.ParseFailed(const ACode: Integer; const AError: string);
begin
  Inc(FFailures);
  FError := IntToStr(ACode) + ': ' + AError;
end;

procedure THeaderParserBench.ClearCounts;
begin
  FBegins := 0;
  FHeaders := 0;
  FSuccesses := 0;
  FFailures := 0;
  FBodyEvents := 0;
  FHeaderBytes := 0;
  FError := '';
end;

procedure THeaderParserBench.CheckCounts(const ARequests, AHeaderSize: Integer);
begin
  if FFailures <> 0 then raise Exception.Create('Parser failure: ' + FError);
  Check((FBegins = ARequests) and (FHeaders = ARequests)
    and (FSuccesses = ARequests), 'Parser begin/header/success count mismatch');
  Check(FHeaderBytes = Int64(ARequests) * AHeaderSize, 'Parser header byte count mismatch');
  Check(FBodyEvents = 0, 'No-body fixture unexpectedly entered body callbacks');
end;

procedure THeaderParserBench.Decode(const AData: Pointer; const ASize: Integer;
  const AMode: TMode);
var
  I, LFirst: Integer;

  procedure Feed(const AOffset, ACount: Integer);
  var
    P: Pointer;
    LRemaining: Integer;
  begin
    P := Pointer(PByte(AData) + AOffset);
    LRemaining := ACount;
    // 每片只调用一次；不得再用空Decode人为触发完成或掩盖未消费输入。
    FParser.Decode(P, LRemaining);
    Check((LRemaining = 0) and (P = Pointer(PByte(AData) + AOffset + ACount)),
      'Parser did not consume the complete input fragment');
  end;

begin
  case AMode of
    bmParserHeader: Feed(0, ASize);
    bmParserHeaderSplit:
      begin
        LFirst := ASize div 2;
        Feed(0, LFirst);
        Feed(LFirst, ASize - LFirst);
      end;
    bmParserHeaderByte:
      for I := 0 to ASize - 1 do Feed(I, 1);
  else
    raise Exception.Create('Unsupported parser feeding mode');
  end;
end;
