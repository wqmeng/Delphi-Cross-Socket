program HttpClientReceiveTests;

{$I ..\..\..\..\zLib.inc}

uses
  {$IF DEFINED(FPC) AND DEFINED(POSIX)}cthreads,{$ENDIF}
  SysUtils, Classes, Generics.Collections,
  {$IFDEF FPC}DTF.RTL,{$ENDIF}
  Utils.SyncObjs, Net.SocketAPI, Net.CrossSocket.Base,
  Net.CrossHttpUtils, Net.CrossHttpClient;

type
  TTestAction = reference to procedure;

  TReceiveConnection = class(TCrossHttpClientConnection)
  public
    BeforeClose, BeforeSuccess: TTestAction;
    procedure TriggerResponseSuccess; override;
    procedure Close; override;
    procedure Fail;
    procedure Timeout;
    procedure Feed(const AText: string);
  end;

  TReceiveSocket = class(TCrossHttpClientSocket)
  public
    procedure Disconnect(const AConnection: ICrossHttpClientConnection);
  end;

  TPhysicalWrite = record
    Connection: ICrossConnection;
    Callback: TCrossConnectionCallback;
  end;

  TFixture = class
  public
    Client: ICrossHttpClient;
    ClientObj: TCrossHttpClient;
    Socket: ICrossHttpClientSocket;
    SocketObj: TReceiveSocket;
    Connection: ICrossHttpClientConnection;
    ConnectionObj: TReceiveConnection;
    Writes: TQueue<TPhysicalWrite>;
    Responses: TList<ICrossHttpClientResponse>;
    AfterResponse: TTestAction;
    SendCalls, SendFailures, ParseErrors, ParseAborts: Integer;
    constructor Create;
    destructor Destroy; override;
    procedure Start(const ASeparate: Boolean = False; const AStream: TStream = nil);
    procedure CompleteWrites;
    procedure Feed(const AText: string);
    procedure Disconnect;
    function Body(const AIndex: Integer): string;
  end;

  TActionStream = class(TMemoryStream)
  public
    Action: TTestAction;
    function Write(const Buffer; Count: LongInt): LongInt; overload; override;
    {$IFDEF DELPHI}{$IF CompilerVersion >= 37}{$IFDEF CPU64BITS}{$IFDEF MSWINDOWS}
    function Write(const Buffer; Count: NativeInt): NativeInt; overload; override;
    {$ENDIF}{$ENDIF}{$ENDIF}{$ENDIF}
  end;

const
  RESPONSE = 'HTTP/1.1 200 OK'#13#10'Content-Length: 2'#13#10#13#10'ok';

procedure Check(const AValue: Boolean; const AMessage: string);
begin
  if not AValue then raise Exception.Create(AMessage);
end;

procedure TReceiveConnection.Close;
var
  LBeforeClose: TTestAction;
begin
  // 在真正设置 csClosed 前进入接收，确定性覆盖终态清理到物理关闭的窗口。
  LBeforeClose := BeforeClose;
  BeforeClose := nil;
  if Assigned(LBeforeClose) then LBeforeClose;
  inherited;
end;

procedure TReceiveConnection.TriggerResponseSuccess;
var
  LBeforeSuccess: TTestAction;
begin
  LBeforeSuccess := BeforeSuccess;
  BeforeSuccess := nil;
  if Assigned(LBeforeSuccess) then LBeforeSuccess;
  inherited;
end;

procedure TReceiveConnection.Fail;
begin
  TriggerResponseFailed(400, 'test failure');
end;

procedure TReceiveConnection.Timeout;
begin
  TriggerResponseTimeout;
end;

procedure TReceiveConnection.Feed(const AText: string);
var
  LBytes: TBytes;
  P: Pointer;
  N, LPrevious: Integer;
begin
  LBytes := TEncoding.ASCII.GetBytes(AText);
  if Length(LBytes) = 0 then Exit;
  P := @LBytes[0];
  N := Length(LBytes);
  while N > 0 do
  begin
    LPrevious := N;
    ParseRecvData(P, N);
    Check(N < LPrevious, '接收循环未消耗数据');
  end;
end;

procedure TReceiveSocket.Disconnect(const AConnection: ICrossHttpClientConnection);
begin
  AConnection.Close;
  LogicDisconnected(AConnection);
end;

constructor TFixture.Create;
begin
  inherited;
  Writes := TQueue<TPhysicalWrite>.Create;
  Responses := TList<ICrossHttpClientResponse>.Create;
  ClientObj := TCrossHttpClient.Create(1);
  Client := ClientObj;
  StopHttpTimeoutTimerForTest(ClientObj);
  Client.RequestTimeout := 1;
  SocketObj := TReceiveSocket.Create(ClientObj, 1, -1, -1, False, True, True, ctNone);
  Socket := SocketObj;
  ConnectionObj := TReceiveConnection.Create(SocketObj, INVALID_SOCKET, ctConnect, '127.0.0.1', nil);
  Connection := ConnectionObj;
  ConnectionObj.ConnectStatus := csConnected;
  SetHttpSendTestHooks(
    procedure(const C: ICrossConnection; const ABuffer: Pointer;
      const ACount: Integer; const ACallback: TCrossConnectionCallback)
    var
      LWrite: TPhysicalWrite;
    begin
      LWrite.Connection := C;
      LWrite.Callback := ACallback;
      Writes.Enqueue(LWrite);
    end,
    procedure(const AClient: TCrossHttpClient; const ARequest: ICrossHttpClientRequest)
    begin
      StartHttpSendTestRequest(Connection, ARequest);
    end);
end;

destructor TFixture.Destroy;
var
  LWrite: TPhysicalWrite;
begin
  AfterResponse := nil;
  ConnectionObj.BeforeClose := nil;
  ConnectionObj.BeforeSuccess := nil;
  CancelHttpSendTestRequest(Connection);
  while Writes.Count > 0 do
  begin
    LWrite := Writes.Dequeue;
    LWrite.Callback(LWrite.Connection, False);
    LWrite.Callback := nil;
    LWrite.Connection := nil;
  end;
  SetHttpSendTestHooks(nil, nil);
  Responses.Free;
  Writes.Free;
  Connection := nil;
  Socket := nil;
  Client := nil;
  inherited;
end;

procedure TFixture.Start(const ASeparate: Boolean; const AStream: TStream);
var
  LResponse: TCrossHttpResponseProc;
begin
  LResponse :=
    procedure(const R: ICrossHttpClientResponse)
    begin
      Responses.Add(R);
      if Assigned(AfterResponse) then AfterResponse;
    end;
  if ASeparate then
    ClientObj.DoRequestWithSend('GET', 'http://127.0.0.1/test', nil, nil, AStream, nil,
      procedure(const ASuccess: Boolean)
      begin
        Inc(SendCalls);
        if not ASuccess then Inc(SendFailures);
      end, LResponse)
  else
    Client.DoRequest('GET', 'http://127.0.0.1/test', nil, TBytes(nil), AStream, nil, LResponse);
end;

procedure TFixture.CompleteWrites;
var
  LWrite: TPhysicalWrite;
begin
  while Writes.Count > 0 do
  begin
    LWrite := Writes.Dequeue;
    LWrite.Callback(LWrite.Connection, True);
  end;
end;

procedure TFixture.Feed(const AText: string);
begin
  try
    ConnectionObj.Feed(AText);
  except
    on E: EAbort do Inc(ParseAborts);
    on E: Exception do Inc(ParseErrors);
  end;
end;

procedure TFixture.Disconnect;
begin
  SocketObj.Disconnect(Connection);
end;

function TFixture.Body(const AIndex: Integer): string;
var
  LBytes: TBytes;
  LStream: TStream;
begin
  LStream := Responses[AIndex].Content;
  if LStream = nil then Exit('');
  SetLength(LBytes, LStream.Size);
  LStream.Position := 0;
  if Length(LBytes) > 0 then LStream.ReadBuffer(LBytes[0], Length(LBytes));
  Result := TEncoding.ASCII.GetString(LBytes);
end;

function TActionStream.Write(const Buffer; Count: LongInt): LongInt;
begin
  if Assigned(Action) then Action;
  Result := inherited;
end;

{$IFDEF DELPHI}{$IF CompilerVersion >= 37}{$IFDEF CPU64BITS}{$IFDEF MSWINDOWS}
function TActionStream.Write(const Buffer; Count: NativeInt): NativeInt;
begin
  if Assigned(Action) then Action;
  Result := inherited;
end;
{$ENDIF}{$ENDIF}{$ENDIF}{$ENDIF}

procedure TestCase(const AName: string);
var
  F: TFixture;
  LStream: TActionStream;
  LIdleInCallback, LStartedNext: Boolean;
begin
  F := TFixture.Create;
  LStream := nil;
  try
    if (AName = 'normal') or (AName = 'split') then
    begin
      F.Start; F.CompleteWrites;
      if AName = 'split' then
      begin
        F.Feed('HTTP/1.1 200 OK'#13#10'Content-Length: 2'#13#10);
        Check(F.Responses.Count = 0, '半头提前完成');
        F.Feed(#13#10'o');
        Check(F.Responses.Count = 0, '半正文提前完成');
        F.Feed('k');
      end else F.Feed(RESPONSE);
      Check(F.Responses.Count = 1, '正常响应通知错误');
      Check(HttpSendTestIsIdle(F.Connection), '正常响应未回池');
      F.Start; F.CompleteWrites; F.Feed(RESPONSE);
      Check((F.Responses.Count = 2) and (F.Body(1) = 'ok'), '后续请求失败');
      Check(HttpSendTestPendingRequests(F.Connection) = 0, '正常请求占用未归还');
    end else
    if (AName = 'tail-crlf') or (AName = 'tail-response') or (AName = 'idle-data') then
    begin
      F.Start; F.CompleteWrites;
      if AName = 'tail-crlf' then F.Feed(RESPONSE + #13#10)
      else
      if AName = 'tail-response' then F.Feed(RESPONSE + RESPONSE)
      else begin F.Feed(RESPONSE); F.Feed(#13#10); end;
      Check((F.Responses.Count = 1) and (F.Body(0) = 'ok'), '合法响应被尾数据覆盖');
      Check(F.Connection.IsClosed and not HttpSendTestIsIdle(F.Connection), '异常尾部连接仍可复用');
      Check(HttpSendTestPendingRequests(F.Connection) = 0, '尾数据重复归还占用');
    end else
    if (AName = 'callback-tail') or (AName = 'callback-normal') then
    begin
      LIdleInCallback := False;
      LStartedNext := False;
      F.AfterResponse :=
        procedure
        begin
          if F.Responses.Count <> 1 then Exit;
          LIdleInCallback := HttpSendTestIsIdle(F.Connection);
          if LIdleInCallback then
          begin
            LStartedNext := True;
            F.Start; F.CompleteWrites;
            if AName = 'callback-normal' then F.Feed(RESPONSE);
          end;
        end;
      F.Start; F.CompleteWrites;
      if AName = 'callback-tail' then
      begin
        F.Feed(RESPONSE + RESPONSE);
        Check(not LIdleInCallback and not LStartedNext, '尾数据检查前回调已复用连接');
        Check(F.Responses.Count = 1, '旧尾数据被发布给下一请求');
      end else
      begin
        F.Feed(RESPONSE);
        Check(LStartedNext and (F.Responses.Count = 2), '回调内正常重入失败');
      end;
    end else
    if (AName = 'early-normal') or (AName = 'early-tail') or (AName = 'early-idle-data')
      or (AName = 'early-close') or (AName = 'legacy-early') then
    begin
      F.Start(AName <> 'legacy-early');
      if AName = 'early-tail' then F.Feed(RESPONSE + #13#10)
      else
      if AName = 'early-close' then
        F.Feed('HTTP/1.1 200 OK'#13#10'Content-Length: 2'#13#10'Connection: close'#13#10#13#10'ok')
      else F.Feed(RESPONSE);
      if AName = 'early-idle-data' then F.Feed(#13#10);
      if AName = 'legacy-early' then Check(F.Responses.Count = 0, '旧入口提前释放请求正文')
      else Check(F.Responses.Count = 1, '独立响应通知未提前到达');
      Check(not F.Connection.IsClosed, '早响应取消了在途物理写');
      Check(not HttpSendTestIsIdle(F.Connection), '发送未完成时连接已回池');
      Check(HttpSendTestPendingRequests(F.Connection) = 1, '早响应提前归还占用');
      F.CompleteWrites;
      Check((F.Responses.Count = 1) and (F.Body(0) = 'ok'), '早响应通知重复或正文错误');
      if AName <> 'legacy-early' then Check((F.SendCalls = 1) and (F.SendFailures = 0), '完整发送被误报失败');
      if (AName = 'early-tail') or (AName = 'early-idle-data') or (AName = 'early-close') then
        Check(F.Connection.IsClosed, '发送结束后未关闭异常连接')
      else Check(HttpSendTestIsIdle(F.Connection), '正常早响应未恢复复用');
      Check(HttpSendTestPendingRequests(F.Connection) = 0, '早响应占用未归还');
    end else
    if (AName = 'failure-window') or (AName = 'timeout-window') then
    begin
      F.Start; F.CompleteWrites;
      F.ConnectionObj.BeforeClose := procedure begin F.Feed(RESPONSE); end;
      if AName = 'timeout-window' then
      begin
        // 只等待超时阈值，不用等待竞争线程；交错顺序由 BeforeClose 固定。
        TThread.Sleep(1100);
        F.ConnectionObj.Timeout;
        Check((F.Responses.Count = 1) and (F.Responses[0].StatusCode = 408), '超时通知被迟到响应覆盖');
      end else
      begin
        F.ConnectionObj.Fail;
        Check((F.Responses.Count = 1) and (F.Responses[0].StatusCode = 400), '失败通知被迟到响应覆盖');
      end;
      Check(F.Connection.IsClosed, '终态连接未关闭');
      Check(HttpSendTestPendingRequests(F.Connection) = 0, '终态重复归还占用');
    end else
    if AName = 'complete-close' then
    begin
      F.Start; F.CompleteWrites;
      F.ConnectionObj.BeforeSuccess := procedure begin F.Disconnect; end;
      F.Feed(RESPONSE);
      Check((F.Responses.Count = 1) and (F.Responses[0].StatusCode = 200)
        and (F.Body(0) = 'ok'), '完整待发布响应因关闭被改判失败');
      Check(F.Connection.IsClosed and not HttpSendTestIsIdle(F.Connection), '完成后关闭的连接仍可复用');
    end else
    if (AName = 'eof') or (AName = 'truncated') then
    begin
      F.Start; F.CompleteWrites;
      if AName = 'eof' then F.Feed('HTTP/1.1 200 OK'#13#10'Connection: close'#13#10#13#10'ok')
      else F.Feed('HTTP/1.1 200 OK'#13#10'Content-Length: 3'#13#10#13#10'ok');
      Check(F.Responses.Count = 0, 'EOF 前提前完成');
      F.Disconnect;
      Check(F.Responses.Count = 1, 'EOF 未通知结果');
      if AName = 'eof' then Check((F.Responses[0].StatusCode = 200) and (F.Body(0) = 'ok'), '关闭定界被误报失败')
      else Check(F.Responses[0].StatusCode = 400, '截断正文被误报成功');
    end else
    if (AName = 'stream-fail') or (AName = 'stream-close') then
    begin
      LStream := TActionStream.Create;
      LStream.Action :=
        procedure
        begin
          if AName = 'stream-fail' then raise EWriteError.Create('test write failure');
          F.Disconnect;
        end;
      F.Start(False, LStream); F.CompleteWrites; F.Feed(RESPONSE);
      Check((F.Responses.Count = 1) and (F.Responses[0].StatusCode >= 400), '流失败或重入关闭后仍补发成功');
    end else
    if AName = 'interim' then
    begin
      F.Start; F.CompleteWrites;
      F.Feed('HTTP/1.1 100 Continue'#13#10#13#10'HTTP/1.1 103 Early Hints'#13#10#13#10);
      Check((F.Responses.Count = 0) and not HttpSendTestIsIdle(F.Connection), '临时响应结束了请求');
      F.Feed(RESPONSE);
      Check((F.Responses.Count = 1) and (F.Body(0) = 'ok') and HttpSendTestIsIdle(F.Connection), '临时响应后的最终响应失败');
    end else raise Exception.Create('未知用例: ' + AName);
    Check(F.ParseErrors = 0, '接收抛出解析异常或访问异常');
    if AName <> 'stream-fail' then Check(F.ParseAborts = 0, '接收因异常中断');
  finally
    F.Free;
    LStream.Free;
  end;
end;

begin
  try
    TestCase(ParamStr(1));
    Writeln('PASS ', ParamStr(1));
  except
    on E: Exception do
    begin
      Writeln('FAIL ', ParamStr(1), ': ', E.ClassName, ': ', E.Message);
      Halt(1);
    end;
  end;
end.
