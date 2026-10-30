// 在原连接工厂上登记独占socket连接表，扫描始终调用生产_ProcTimeout的既有测试入口。
type
  TClientTimeoutSnapshot = record
    ConnectStatus: TConnectStatus;
    RequestStatus: TRequestStatus;
    Pending: Integer;
  end;
  TClientTimeoutFixture = class(TClientPoolFixture)
  private
    FSnapshots: TArray<TClientTimeoutSnapshot>;
  public
    Client: TCrossHttpClient;
    constructor Create(const ACount: Integer);
    destructor Destroy; override;
    procedure CaptureState;
    procedure CheckStable;
  end;

constructor TClientTimeoutFixture.Create(const ACount: Integer);
var
  LConnections: TCrossConnections;
  I: Integer;
begin
  inherited Create(ACount);
  Client := FClientOwner as TCrossHttpClient;
  Client.RequestTimeout := MaxInt;
  Client.ConnectTimeout := MaxInt;
  Client.Idleout := MaxInt;
  LConnections := FSocketOwner.LockConnections;
  try
    Check(LConnections.Count = 0, 'Timeout fixture socket is not exclusive');
    for I := 0 to High(Connections) do
      LConnections.Add(Connections[I].UID, Connections[I]);
  finally
    FSocketOwner.UnlockConnections;
  end;
end;

destructor TClientTimeoutFixture.Destroy;
var
  LConnections: TCrossConnections;
begin
  // 注册表中的接口先在同一锁下撤下，再由原fixture释放dock、连接及client。
  if FSocketOwner <> nil then
  begin
    LConnections := FSocketOwner.LockConnections;
    try LConnections.Clear; finally FSocketOwner.UnlockConnections; end;
  end;
  Client := nil;
  inherited;
end;

procedure TClientTimeoutFixture.CaptureState;
var
  I: Integer;
begin
  SetLength(FSnapshots, Length(Connections));
  for I := 0 to High(Connections) do
  begin
    FSnapshots[I].ConnectStatus := Connections[I].ConnectStatus;
    FSnapshots[I].RequestStatus := Connections[I].RequestStatus;
    FSnapshots[I].Pending := HttpSendTestPendingRequests(Connections[I]);
  end;
end;

procedure TClientTimeoutFixture.CheckStable;
var
  I: Integer;
  LConnections: TCrossConnections;
  LConnection: ICrossConnection;
begin
  // 检查在计时外进行，不调整阈值/状态来掩盖扫描触发的动作。
  Check((Client.RequestTimeout = MaxInt) and (Client.ConnectTimeout = MaxInt)
    and (Client.Idleout = MaxInt), 'Timeout benchmark thresholds changed');
  Check(Length(FSnapshots) = Length(Connections), 'Timeout snapshot count mismatch');
  for I := 0 to High(Connections) do
  begin
    Check(Connections[I].ConnectStatus = FSnapshots[I].ConnectStatus, 'Timeout scan changed connection status');
    Check(Connections[I].RequestStatus = FSnapshots[I].RequestStatus, 'Timeout scan changed request status');
    Check(HttpSendTestPendingRequests(Connections[I]) = FSnapshots[I].Pending, 'Timeout scan changed reservation');
  end;
  LConnections := FSocketOwner.LockConnections;
  try
    Check(LConnections.Count = Length(Connections), 'Timeout scan changed connection table');
    for I := 0 to High(Connections) do
    begin
      Check(LConnections.TryGetValue(Connections[I].UID, LConnection), 'Timeout scan lost a connection');
      // 同一对象的派生/基接口可以有不同接口地址；这里核对对象身份。
      Check((LConnection as TCrossHttpClientConnection)
        = (Connections[I] as TCrossHttpClientConnection), 'Timeout scan replaced a connection');
    end;
  finally
    FSocketOwner.UnlockConnections;
  end;
end;

function RunClientTimeoutChecks: Integer;
var
  F: TClientTimeoutFixture;
  C: TCrossHttpClientConnection;
  LChecks: Integer;

  procedure CheckTimeout(const AValue: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    Check(AValue, 'Client timeout: ' + AMessage);
  end;

  procedure RunCase(const ALabel: string; const AConnectionStatus: TConnectStatus;
    const ARequestStatus: TRequestStatus; const ASendFinished: Boolean;
    const ARequestTimeout, AConnectTimeout, AIdleout: Integer;
    const AExpectedHook, AKeepConnectEnabled: Boolean);
  var
    LHookCalls, LPendingBefore: Integer;
  begin
    C.ConnectStatus := AConnectionStatus;
    HttpHeaderBenchPoolState(C, ARequestStatus);
    HttpHeaderBenchTimeoutSent(C, ASendFinished);
    F.Client.RequestTimeout := ARequestTimeout;
    F.Client.ConnectTimeout := AConnectTimeout;
    F.Client.Idleout := AIdleout;
    LPendingBefore := HttpSendTestPendingRequests(F.Connections[0]);
    LHookCalls := 0;
    ProcHttpTimeoutForTest(F.Client,
      procedure
      begin
        Inc(LHookCalls);
        // 只验证候选收集；禁用阈值让原动作重验退出，不关闭没有真实请求的测试连接。
        F.Client.RequestTimeout := 0;
        if not AKeepConnectEnabled then F.Client.ConnectTimeout := 0;
        F.Client.Idleout := 0;
      end);
    CheckTimeout(LHookCalls = Ord(AExpectedHook), ALabel + ' candidate hook count');
    CheckTimeout(C.ConnectStatus = AConnectionStatus, ALabel + ' unexpected close');
    CheckTimeout(C.RequestStatus = ARequestStatus, ALabel + ' unexpected request state change');
    CheckTimeout(HttpSendTestPendingRequests(F.Connections[0]) = LPendingBefore,
      ALabel + ' unexpected reservation change');
  end;

begin
  LChecks := 0;
  F := TClientTimeoutFixture.Create(1);
  try
    C := F.Connections[0] as TCrossHttpClientConnection;
    CheckTimeout(HttpSendTestPendingRequests(F.Connections[0]) = 1, 'factory initial reservation');
    // 只等一次真实时钟；后面的状态setter和_EndRequest均不重置FWatch，不伪造时间。
    Sleep(1100);
    RunCase('disabled', csConnecting, rsSending, False, 0, 0, 0, False, False);
    RunCase('request-sending', csConnected, rsSending, False, 1, 0, 0, True, False);
    RunCase('request-responding', csConnected, rsResponding, False, 1, 0, 0, True, False);
    RunCase('connect', csConnecting, rsIdle, False, 0, 1, 0, True, False);
    RunCase('idle-reserved', csConnected, rsIdle, False, 1, 1, 1, False, False);
    RunCase('upgrade-unfinished', csConnected, rsUpgraded, False, 1, 0, 0, True, False);
    RunCase('upgrade-finished', csConnected, rsUpgraded, True, 1, 0, 0, False, False);
    // 两种时限同时满足，hook仅禁用Request；错选Connect将被原重验关闭，从而明确失败。
    RunCase('request-before-connect', csConnecting, rsSending, False, 1, 1, 1, True, True);
    HttpHeaderBenchPoolRelease(C);
    CheckTimeout(HttpSendTestPendingRequests(F.Connections[0]) = 0, 'release initial reservation');
    RunCase('idle', csConnected, rsIdle, True, 1, 1, 1, True, False);
    RunCase('closed', csClosed, rsSending, False, 1, 1, 1, False, False);
  finally
    C := nil;
    F.Free;
  end;
  Result := LChecks;
end;
