// 使用真实dock和测试连接工厂；所有连接由接口持有，选择算法直接调用生产方法。
type
  TClientPoolFixture = class
  private
    FLiveBefore: Integer;
    FClientOwner: ICrossHttpClient;
    FSeed: ICrossHttpClientConnection;
    FSocketOwner: ICrossHttpClientSocket;
    FDockOwner: IServerDock;
  public
    Dock: TServerDock;
    Connections: TArray<ICrossHttpClientConnection>;
    constructor Create(const ACount: Integer);
    destructor Destroy; override;
  end;

constructor TClientPoolFixture.Create(const ACount: Integer);
var
  LClient: TCrossHttpClient;
  I: Integer;
begin
  inherited Create;
  FLiveBefore := HttpSendTestLiveConnections;
  LClient := TCrossHttpClient.Create(1);
  FClientOwner := LClient;
  StopHttpTimeoutTimerForTest(LClient);
  // 空池也借工厂连接取得真实socket；该seed不加入空池，创建和释放均在计时外。
  FSeed := NewHttpSendTestConnection(LClient);
  FSocketOwner := FSeed.Owner as ICrossHttpClientSocket;
  Dock := TServerDock.Create(FSocketOwner as TCrossHttpClientSocket, HTTP, '127.0.0.1', 80, 0);
  FDockOwner := Dock;
  SetLength(Connections, ACount);
  for I := 0 to ACount - 1 do
  begin
    if I = 0 then Connections[I] := FSeed
    else Connections[I] := NewHttpSendTestConnection(LClient);
    // 工厂的构造器已预占一次，rsIdle+pending=1仍不可被再次选择。
    Dock.AddConnection(Connections[I]);
  end;
end;

destructor TClientPoolFixture.Destroy;
var
  I: Integer;
begin
  // 独立测试dock没有登记进socket字典；先释放其列表，再关闭仍由数组持有的连接。
  FDockOwner := nil;
  Dock := nil;
  if FSeed <> nil then FSeed.Close;
  for I := 1 to High(Connections) do
    if Connections[I] <> nil then Connections[I].Close;
  Connections := nil;
  FSeed := nil;
  FSocketOwner := nil;
  FClientOwner := nil;
  Check(HttpSendTestLiveConnections = FLiveBefore, 'Client pool fixture leaked connections');
  inherited;
end;

function RunClientPoolChecks: Integer;
var
  LChecks: Integer;
  F: TClientPoolFixture;
  LSelected, LSecond: ICrossHttpClientConnection;
  LKind, LBeforePending: Integer;

  procedure CheckPool(const AValue: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    Check(AValue, 'Client pool: ' + AMessage);
  end;

  procedure MakeIdle(const C: ICrossHttpClientConnection);
  begin
    CheckPool((C.RequestStatus = rsIdle) and (HttpSendTestPendingRequests(C) = 1),
      'factory must start with one reservation');
    HttpHeaderBenchPoolRelease(C as TCrossHttpClientConnection);
    CheckPool(HttpSendTestIsIdle(C), 'releasing initial reservation must make connection idle');
  end;

  procedure ConcurrentClaim;
  var
    LFixture: TClientPoolFixture;
    LThreads: array[0..1] of TThread;
    LSelected: array[0..1] of ICrossHttpClientConnection;
    LErrors: array[0..1] of string;
    LGate: THandle;
    I, LClaims: Integer;

    procedure StartWorker(const AIndex: Integer);
    begin
      LThreads[AIndex] := TThread.CreateAnonymousThread(
        procedure
        begin
          try
            if WaitForSingleObject(LGate, 5000) <> WAIT_OBJECT_0 then
              raise Exception.Create('pool claim gate timed out');
            LSelected[AIndex] := LFixture.Dock.GetIdleConnection;
          except
            on E: Exception do LErrors[AIndex] := E.ClassName + ': ' + E.Message;
          end;
        end);
      LThreads[AIndex].FreeOnTerminate := False;
      LThreads[AIndex].Start;
    end;

  begin
    LFixture := nil;
    LThreads[0] := nil;
    LThreads[1] := nil;
    LGate := CreateEvent(nil, True, False, nil);
    CheckPool(LGate <> 0, 'create concurrent gate');
    try
      LFixture := TClientPoolFixture.Create(1);
      MakeIdle(LFixture.Connections[0]);
      StartWorker(0);
      StartWorker(1);
      SetEvent(LGate);
      for I := 0 to 1 do LThreads[I].WaitFor;
      LClaims := 0;
      for I := 0 to 1 do
      begin
        CheckPool(LErrors[I] = '', LErrors[I]);
        if LSelected[I] <> nil then
        begin
          Inc(LClaims);
          CheckPool(LSelected[I] = LFixture.Connections[0], 'concurrent selection identity');
        end;
      end;
      CheckPool(LClaims = 1, 'two simultaneous selectors must claim once');
      CheckPool(HttpSendTestPendingRequests(LFixture.Connections[0]) = 1,
        'concurrent claim must add exactly one reservation');
      HttpHeaderBenchPoolRelease(LFixture.Connections[0] as TCrossHttpClientConnection);
    finally
      // 出错也放行等待者，先等待线程结束，再释放捕获的fixture和门闩。
      SetEvent(LGate);
      for I := 0 to 1 do
        if LThreads[I] <> nil then
        begin
          LThreads[I].WaitFor;
          LThreads[I].Free;
        end;
      LSelected[0] := nil;
      LSelected[1] := nil;
      CloseHandle(LGate);
      LFixture.Free;
    end;
  end;

begin
  LChecks := 0;
  F := TClientPoolFixture.Create(0);
  try
    LSelected := F.Dock.GetIdleConnection;
    CheckPool(LSelected = nil, 'empty pool must miss');
  finally LSelected := nil; F.Free; end;

  F := TClientPoolFixture.Create(1);
  try
    MakeIdle(F.Connections[0]);
    LSelected := F.Dock.GetIdleConnection;
    CheckPool(LSelected = F.Connections[0], 'single idle identity');
    CheckPool(HttpSendTestPendingRequests(LSelected) = 1, 'first claim increments pending');
    LSecond := F.Dock.GetIdleConnection;
    CheckPool(LSecond = nil, 'reserved idle cannot be claimed twice');
    CheckPool(HttpSendTestPendingRequests(LSelected) = 1, 'failed repeat does not change pending');
    HttpHeaderBenchPoolRelease(LSelected as TCrossHttpClientConnection);
    CheckPool(HttpSendTestIsIdle(LSelected), 'release restores availability');
  finally LSecond := nil; LSelected := nil; F.Free; end;

  // 首项分别为sending、responding、closed、connecting、已预占idle；必须选择后面的idle。
  for LKind := 0 to 4 do
  begin
    F := TClientPoolFixture.Create(2);
    try
      case LKind of
        0: HttpHeaderBenchPoolState(F.Connections[0] as TCrossHttpClientConnection, rsSending);
        1: HttpHeaderBenchPoolState(F.Connections[0] as TCrossHttpClientConnection, rsResponding);
        2: F.Connections[0].Close;
        3: (F.Connections[0] as TCrossHttpClientConnection).ConnectStatus := csConnecting;
        4: ; // 保留工厂的rsIdle+pending=1。
      end;
      MakeIdle(F.Connections[1]);
      LBeforePending := HttpSendTestPendingRequests(F.Connections[0]);
      LSelected := F.Dock.GetIdleConnection;
      CheckPool(LSelected = F.Connections[1], 'unavailable first entry must be skipped');
      CheckPool(HttpSendTestPendingRequests(F.Connections[0]) = LBeforePending,
        'skipping first entry does not change its reservation');
      HttpHeaderBenchPoolRelease(LSelected as TCrossHttpClientConnection);
    finally LSelected := nil; F.Free; end;
  end;

  F := TClientPoolFixture.Create(2);
  try
    MakeIdle(F.Connections[0]);
    MakeIdle(F.Connections[1]);
    LSelected := F.Dock.GetIdleConnection;
    LSecond := F.Dock.GetIdleConnection;
    CheckPool((LSelected = F.Connections[0]) and (LSecond = F.Connections[1]),
      'idle connections keep insertion order');
    CheckPool(F.Dock.GetIdleConnection = nil, 'all reserved entries must miss');
    HttpHeaderBenchPoolRelease(LSelected as TCrossHttpClientConnection);
    HttpHeaderBenchPoolRelease(LSecond as TCrossHttpClientConnection);
  finally LSecond := nil; LSelected := nil; F.Free; end;

  ConcurrentClaim;
  Result := LChecks;
end;
