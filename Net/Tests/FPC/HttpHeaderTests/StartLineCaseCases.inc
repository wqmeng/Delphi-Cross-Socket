// 与 StartLineCaseTests.lpr 共用同一组 723 cases/oracle；仅封装成现有测试可调用函数。
function RunStartLineCaseTests: Integer;
type
  // 使用现有测试单元的访问类；FPC 不允许函数内声明 class，局部只建别名。
  TStartLineRequestAccess = TRequestAccess;
var
  Checks, Cases, Accepted, Rejected: Integer;
  Server: ICrossHttpServer;
  Connection: ICrossHttpConnection;
  Conn: TCrossHttpConnection;

procedure Check(const ACondition: Boolean; const AMessage: string);
begin
  Inc(Checks);
  if not ACondition then raise Exception.Create(AMessage);
end;

function UTF8Wire(const S: string): AnsiString;
var B: TBytes;
begin
  B := TEncoding.UTF8.GetBytes(S);
  SetString(Result, PAnsiChar(Pointer(B)), Length(B));
end;

function ByteWire(const ABytes: array of Byte): AnsiString;
begin
  SetLength(Result, Length(ABytes));
  if Length(ABytes) > 0 then Move(ABytes[0], PAnsiChar(Result)^, Length(ABytes));
end;

function SameBytes(const A, B: TBytes): Boolean;
begin
  Result := Length(A) = Length(B);
  if Result and (Length(A) > 0) then Result := CompareMem(Pointer(A), Pointer(B), Length(A));
end;

procedure RunCase(const AMethod, AVersion: AnsiString; const AStructureValid: Boolean);
var
  LLine, LWire: AnsiString;
  LData, LOriginal: TBytes;
  LExpectedMethod, LExpectedVersion, LExpectedLine: string;
  LExpected, LActual: Boolean;
  R: TStartLineRequestAccess;
begin
  Inc(Cases);
  LLine := AMethod + ' / ' + AVersion;
  LWire := LLine + #13#10'Host: localhost'#13#10#13#10;
  SetLength(LData, Length(LWire));
  if Length(LData) > 0 then Move(PAnsiChar(LWire)^, LData[0], Length(LData));
  LOriginal := Copy(LData);
  LExpected := AStructureValid;
  if LExpected then
  begin
    try
      // Oracle 仅使用原 GetString + ToUpper，不调用候选 helper 或复制其白名单。
      LExpectedLine := TUtils.GetString(PByte(PAnsiChar(LLine)), Length(LLine));
      LExpectedMethod := TUtils.GetString(PByte(PAnsiChar(AMethod)), Length(AMethod)).ToUpper;
      LExpectedVersion := TUtils.GetString(PByte(PAnsiChar(AVersion)), Length(AVersion)).ToUpper;
      if LExpectedVersion = '' then LExpectedVersion := 'HTTP/1.0';
    except
      on E: Exception do LExpected := False;
    end;
  end;
  R := TStartLineRequestAccess.Create(Conn);
  try
    LActual := R.ParseHeader(Pointer(LData), Length(LData));
    Check(LActual = LExpected, Format('Case %d 接受/拒绝改变', [Cases]));
    if LActual then
    begin
      Inc(Accepted);
      Check(R.Method = LExpectedMethod, Format('Case %d Method 与原 oracle 不同', [Cases]));
      Check(R.Version = LExpectedVersion, Format('Case %d Version 与原 oracle 不同', [Cases]));
      Check(R.RequestCmdLine = LExpectedLine, Format('Case %d RequestCmdLine 改变', [Cases]));
      Check(SameBytes(R.RawRequestHeader, LOriginal), 'raw 快照内容改变');
      LData[0] := LData[0] xor $20;
      Check(SameBytes(R.RawRequestHeader, LOriginal), 'raw 未独立持有');
    end else
      Inc(Rejected);
  finally
    R.Free;
  end;
end;

procedure RunTests;
const
  Methods: array[0..20] of string = (
    'GET', 'PUT', 'HEAD', 'POST', 'PATCH', 'TRACE', 'DELETE', 'OPTIONS', 'CONNECT',
    'get', 'GeT', 'options', 'i', 'ı', 'İ', 'ß', 'Σ', 'X-TEST', 'COPY', 'GETX', 'GET1');
  Versions: array[0..8] of string = (
    'HTTP/1.0', 'HTTP/1.1', 'http/1.1', 'HtTp/1.0', 'HTTP/1.10', 'HTTP/2.0',
    'HTTP/3', 'İ/1.1', '版本');
var I, J: Integer; B, Bom: AnsiString;
begin
  Bom := ByteWire([$EF, $BB, $BF]);
  for I := Low(Methods) to High(Methods) do
    for J := Low(Versions) to High(Versions) do
      RunCase(UTF8Wire(Methods[I]), UTF8Wire(Versions[J]), True);
  for I := 0 to 8 do
  begin
    RunCase(Bom + UTF8Wire(Methods[I]), 'HTTP/1.1', True);
    RunCase(UTF8Wire(Methods[I]), Bom + 'HTTP/1.1', True);
  end;
  // 空解码结果仍走旧 ToUpper；空原字节段仍由原请求行结构拒绝。
  RunCase(Bom, 'HTTP/1.1', True);
  RunCase('GET', Bom, True);
  RunCase('', 'HTTP/1.1', False);
  RunCase('GET', '', False);
  // 原始 NUL/CR/LF 的拒绝优先于任何 GetString/BOM 解释。
  RunCase(ByteWire([$FF, $FE, $47, 0, $45, 0, $54, 0]), 'HTTP/1.1', False);
  RunCase('GET', ByteWire([$FE, $FF, 0, $48, 0, $54, 0, $54, 0, $50]), False);
  for I := 0 to 255 do
  begin
    if I = 32 then Continue; // 不改变两个 SP 的结构分隔，另测空段即可。
    B := ByteWire([$58, Byte(I), $58]);
    RunCase(B, 'HTTP/1.1', (I <> 0) and (I <> 10) and (I <> 13));
    RunCase('GET', B, (I <> 0) and (I <> 10) and (I <> 13));
  end;
end;

begin
  Checks := 0;
  Cases := 0;
  Accepted := 0;
  Rejected := 0;
  Conn := nil;
  Server := TCrossHttpServer.Create(1, False);
  try
    Conn := TCrossHttpConnection.Create(Server as TCrossHttpServer, INVALID_SOCKET, ctAccept, '', nil);
    Connection := Conn;
    RunTests;
  finally
    Connection := nil;
    Server := nil;
  end;
  Result := Checks;
end;
