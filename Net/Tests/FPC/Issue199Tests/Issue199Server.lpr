program Issue199Server;

{$I zLib.inc}
{$IFNDEF FPC}{$APPTYPE CONSOLE}{$ENDIF}

uses
  SysUtils, Classes, Windows, Utils.SyncObjs, Net.SocketAPI,
  Net.CrossSocket.Base, Net.CrossHttpParams, Net.CrossHttpServer;

type
  TProbeConnection = class(TCrossHttpConnection)
  private
    FReceived: TBytes;
    FHeaderProbed: Boolean;
    FNumber: Integer;
    procedure _ProbeHeader;
  public
    procedure Capture(const ABuf: Pointer; const ALen: Integer);
    property Number: Integer read FNumber write FNumber;
  end;

  TProbeServer = class(TCrossHttpServer)
  private
    FConnections: Integer;
  protected
    procedure LogicReceived(const AConnection: ICrossConnection;
      const ABuf: Pointer; const ALen: Integer); override;
    function CreateConnection(const AOwner: TCrossSocketBase;
      const AClientSocket: TSocket; const AConnectType: TConnectType;
      const AHost: string; const AConnectCb: TCrossConnectionCallback): ICrossConnection; override;
    procedure DoOnRequest(const AConnection: ICrossHttpConnection;
      const ARequest: ICrossHttpRequest; const AResponse: ICrossHttpResponse); override;
  end;

var
  GOutputDir: string;
  GLogLock: ILock;
  GRequests: Integer;

procedure Log(const AText: string);
begin
  GLogLock.Enter;
  try
    Writeln(AText);
    Flush(Output);
  finally
    GLogLock.Leave;
  end;
end;

procedure SaveBytes(const AName: string; const ABytes: TBytes);
var
  LStream: TFileStream;
begin
  LStream := TFileStream.Create(GOutputDir + AName, fmCreate);
  try
    if Length(ABytes) > 0 then LStream.WriteBuffer(ABytes[0], Length(ABytes));
  finally
    LStream.Free;
  end;
end;

function CodeUnits(const AText: string): string;
var
  I: Integer;
begin
  Result := '';
  for I := 1 to Length(AText) do
  begin
    if I > 1 then Result := Result + ' ';
    Result := Result + IntToHex(Ord(AText[I]), 4);
  end;
end;

procedure TProbeConnection._ProbeHeader;
var
  I, LHeaderSize, LReferer: Integer;
  LText: string;
  LHeader: THttpHeader;
  LDecoded: Boolean;
begin
  if FHeaderProbed then Exit;
  LHeaderSize := 0;
  for I := 0 to Length(FReceived) - 4 do
    if (FReceived[I] = 13) and (FReceived[I + 1] = 10)
      and (FReceived[I + 2] = 13) and (FReceived[I + 3] = 10) then
    begin
      LHeaderSize := I + 4;
      Break;
    end;
  if LHeaderSize = 0 then Exit;
  FHeaderProbed := True;
  // 故意重演 #199 修复前的整块默认代码页转换，观察它是否破坏 CRLF。
  // 当前生产 ParseHeader 已改为字节解析；此诊断副本和 HEADER_DECODE 日志不代表生产结果。
  SetString(LText, MarshaledAString(@FReceived[0]), LHeaderSize);
  Log(Format('CONVERT conn=%d raw_bytes=%d chars=%d nul_index=%d',
    [FNumber, LHeaderSize, Length(LText), LText.IndexOf(#0)]));
  LReferer := LText.IndexOf('viewMode=');
  if LReferer >= 0 then
    Log('REFERER_TAIL_UTF16 ' + CodeUnits(LText.Substring(LReferer, 48)));
  LHeader := THttpHeader.Create;
  try
    LDecoded := LHeader.Decode(LText.Substring(LText.IndexOf(#13#10) + 2));
    Log(Format('HEADER_DECODE ok=%d count=%d', [Ord(LDecoded), LHeader.Count]));
  finally
    LHeader.Free;
  end;
end;

procedure TProbeConnection.Capture(const ABuf: Pointer; const ALen: Integer);
var
  LOldSize: Integer;
begin
  // ABuf 只在接收回调期间有效，ALen 为字节数；复制后累计完整线上输入供逐字节对照。
  if ALen > 0 then
  begin
    LOldSize := Length(FReceived);
    SetLength(FReceived, LOldSize + ALen);
    Move(ABuf^, FReceived[LOldSize], ALen);
    SaveBytes(Format('received-%d.bin', [FNumber]), FReceived);
    Log(Format('RECEIVE conn=%d bytes=%d total=%d',
      [FNumber, ALen, Length(FReceived)]));
    _ProbeHeader;
  end;
end;

procedure TProbeServer.LogicReceived(const AConnection: ICrossConnection;
  const ABuf: Pointer; const ALen: Integer);
begin
  // 必须在外层记录；ParseRecvData 会再次收到同一缓冲区的未消费尾部。
  (AConnection as TProbeConnection).Capture(ABuf, ALen);
  inherited;
end;

function TProbeServer.CreateConnection(const AOwner: TCrossSocketBase;
  const AClientSocket: TSocket; const AConnectType: TConnectType;
  const AHost: string; const AConnectCb: TCrossConnectionCallback): ICrossConnection;
var
  LConnection: TProbeConnection;
begin
  LConnection := TProbeConnection.Create(AOwner, AClientSocket, AConnectType, AHost, AConnectCb);
  LConnection.Number := AtomicIncrement(FConnections);
  Result := LConnection;
end;

procedure TProbeServer.DoOnRequest(const AConnection: ICrossHttpConnection;
  const ARequest: ICrossHttpRequest; const AResponse: ICrossHttpResponse);
var
  LNumber, I, LNullIndex: Integer;
  LBody, LRawHeader: TBytes;
  LPosition: Int64;
  LReport: string;
begin
  LNumber := AtomicIncrement(GRequests);
  LBody := nil;
  if ARequest.RawBody <> nil then
  begin
    LPosition := ARequest.RawBody.Position;
    try
      ARequest.RawBody.Position := 0;
      SetLength(LBody, ARequest.RawBody.Size);
      if Length(LBody) > 0 then ARequest.RawBody.ReadBuffer(LBody[0], Length(LBody));
    finally
      ARequest.RawBody.Position := LPosition;
    end;
  end;
  SaveBytes(Format('body-%d.bin', [LNumber]), LBody);
  // 此快照来自生产解析路径且不含正文；NUL 也直接按收到的字节检查，避免二次字符转换。
  LRawHeader := ARequest.RawRequestHeader;
  LNullIndex := -1;
  for I := 0 to Length(LRawHeader) - 1 do
    if LRawHeader[I] = 0 then
    begin
      LNullIndex := I;
      Break;
    end;
  SaveBytes(Format('header-%d.bin', [LNumber]), LRawHeader);
  LReport := Format('REQUEST number=%d method=%s path=%s headers=%d content_length=%d body_type=%d body_nil=%d raw_body_nil=%d body_size=%d raw_nul_index=%d',
    [LNumber, ARequest.Method, ARequest.Path, ARequest.Header.Count,
     ARequest.ContentLength, Ord(ARequest.BodyType), Ord(ARequest.Body = nil),
     Ord(ARequest.RawBody = nil), Length(LBody), LNullIndex]);
  Log(LReport);
  Log('REFERER_UTF16 ' + CodeUnits(ARequest.Header['Referer']));
  // 故意不按 Method 过滤业务回调：旧错误可能把未消费正文拼进下一条 Method，
  // runner 必须检查实际 Method/正文，不能只因响应 200 就判断修复成功。
  AResponse.SendNoCompress(LReport);
end;

procedure Serve;
var
  LServer: TProbeServer;
  LServerRef: ICrossHttpServer;
  LPort: Word;
  LLine: string;
begin
  if ParamCount <> 1 then raise Exception.Create('Usage: Issue199Server <output-directory>');
  GOutputDir := IncludeTrailingPathDelimiter(ExpandFileName(ParamStr(1)));
  if not DirectoryExists(GOutputDir) then raise Exception.Create('Output directory does not exist');
  Log(Format('ENV acp=%d default_codepage=%d pointer_bytes=%d',
    [GetACP, DefaultSystemCodePage, SizeOf(Pointer)]));
  LServer := TProbeServer.Create(1, False);
  LServerRef := LServer;
  try
    LServer.Addr := '127.0.0.1';
    LServer.Port := 0;
    LPort := 0;
    LServer.Start(
      procedure(const AListen: ICrossListen; const ASuccess: Boolean)
      begin
        if ASuccess then LPort := AListen.LocalPort;
      end);
    if LPort = 0 then raise Exception.Create('Listen failed');
    Log('READY ' + IntToStr(LPort));
    Readln(LLine);
    LServer.Stop;
  finally
    LServerRef := nil;
  end;
  Log('TOTAL ' + IntToStr(GRequests));
end;

begin
  GLogLock := TLock.Create;
  try
    Serve;
  except
    on E: Exception do
    begin
      Log('ERROR ' + E.ClassName + ': ' + E.Message);
      ExitCode := 1;
    end;
  end;
end.
