type
  TMemoryConnection = class(TCrossHttpConnection)
  private
    // INVALID_SOCKET 的 Close 可只改变状态，不能用 csClosed 推断 InternalClose 已执行。
    FCleaned: Boolean;
  protected
    procedure DirectSend(const ABuffer: Pointer; const ACount: Integer;
      const ACallback: TCrossConnectionCallback); override;
  public
    Sent: AnsiString;
    Writes: Integer;
    constructor Create(const AServer: TCrossHttpServer); reintroduce;
    procedure Feed(const AData: AnsiString);
    procedure Cleanup;
  end;

  TFixture = class
  public
    Server: ICrossHttpServer;
    Connection: ICrossHttpConnection;
    ConnectionObj: TMemoryConnection;
    Request: ICrossHttpRequest;
    Response: ICrossHttpResponse;
    constructor Create;
    destructor Destroy; override;
  end;

constructor TMemoryConnection.Create(const AServer: TCrossHttpServer);
begin
  inherited Create(AServer, INVALID_SOCKET, ctAccept, '', nil);
  ConnectStatus := csConnected;
end;

procedure TMemoryConnection.DirectSend(const ABuffer: Pointer; const ACount: Integer;
  const ACallback: TCrossConnectionCallback);
var LPart: AnsiString;
begin
  Inc(Writes);
  SetString(LPart, PAnsiChar(ABuffer), ACount);
  Sent := Sent + LPart;
  if Assigned(ACallback) then ACallback(Self, True);
end;

procedure TMemoryConnection.Feed(const AData: AnsiString);
var P: Pointer; LLength, LBefore: Integer;
begin
  P := PAnsiChar(AData);
  LLength := Length(AData);
  _LockRecv;
  try
    while LLength > 0 do
    begin
      LBefore := LLength;
      ParseRecvData(P, LLength);
      Check(LLength < LBefore, '解析器没有消费输入');
    end;
  finally _UnlockRecv; end;
end;

procedure TMemoryConnection.Cleanup;
begin
  if FCleaned then Exit;
  FCleaned := True;
  ConnectStatus := csClosed;
  // 显式执行一次正常关闭清理，释放队列、当前请求和上传文件等拥有资源。
  InternalClose;
end;

constructor TFixture.Create;
begin
  Server := TCrossHttpServer.Create(1, False);
  ConnectionObj := TMemoryConnection.Create(Server as TCrossHttpServer);
  Connection := ConnectionObj;
  Request := TCrossHttpRequest.Create(ConnectionObj);
  Response := TCrossHttpResponse.Create(ConnectionObj, Request, nil);
end;

destructor TFixture.Destroy;
begin
  Response := nil;
  Request := nil;
  ConnectionObj.Cleanup;
  Connection := nil;
  Server := nil;
  inherited;
end;
