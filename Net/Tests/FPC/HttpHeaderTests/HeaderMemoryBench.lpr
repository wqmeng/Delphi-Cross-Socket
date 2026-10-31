program HeaderMemoryBench;
{$I zLib.inc}
{$IFNDEF FPC}{$APPTYPE CONSOLE}{$ENDIF}
uses SysUtils, Classes, Net.CrossHttpParams, Net.CrossHttpServer,
  Net.CrossSocket.Base, Net.SocketAPI;
type TRequestAccess = class(TCrossHttpRequest);

function Allocated: Int64;
var S: THeapStatus;
begin S := GetHeapStatus; Result := S.TotalAllocated; end;

procedure Check(const OK: Boolean);
begin if not OK then raise Exception.Create('Invalid output'); end;

procedure Measure;
const N = 5000;
var
  Data, Fields: TBytes;
  Headers: array of THttpHeader;
  Requests: array of TRequestAccess;
  S: TFileStream;
  I, FirstEnd, HeaderEnd: Integer;
  Baseline, Live, Released: Int64;
  Server: ICrossHttpServer;
  Connection: ICrossHttpConnection;
  Conn: TCrossHttpConnection;
  WarmHeader: THttpHeader;
  WarmRequest: TRequestAccess;

  function Decode(const H: THttpHeader): Boolean;
  {$IFNDEF HEADER_BYTES}var Text: string;{$ENDIF}
  begin
    {$IFDEF HEADER_BYTES}
    Result := H.Decode(Pointer(Fields), Length(Fields));
    {$ELSE}
    SetString(Text, MarshaledAString(Pointer(Fields)), Length(Fields));
    Result := H.Decode(Text);
    {$ENDIF}
    Result := Result and (H.Count = 10);
  end;
begin
  S := TFileStream.Create(ParamStr(1), fmOpenRead or fmShareDenyNone);
  try SetLength(Data, S.Size); S.ReadBuffer(Data[0], Length(Data)); finally S.Free; end;
  FirstEnd := -1; HeaderEnd := -1;
  for I := 0 to Length(Data)-2 do
    if (Data[I]=13) and (Data[I+1]=10) then begin FirstEnd := I+2; Break; end;
  for I := 0 to Length(Data)-4 do
    if (Data[I]=13) and (Data[I+1]=10) and (Data[I+2]=13) and (Data[I+3]=10) then
    begin HeaderEnd := I+4; Break; end;
  Check((FirstEnd>0) and (HeaderEnd>FirstEnd));
  SetLength(Data, HeaderEnd); Fields := Copy(Data, FirstEnd, HeaderEnd-FirstEnd);
  SetLength(Headers, N); SetLength(Requests, N);
  Server := TCrossHttpServer.Create(1, False);
  Conn := TCrossHttpConnection.Create(Server as TCrossHttpServer, INVALID_SOCKET, ctAccept, '', nil);
  Connection := Conn;
  // 共享上下文、数组与类型初始化都在计量基线之前，避免摊入单个对象成本。
  WarmHeader := THttpHeader.Create; WarmRequest := TRequestAccess.Create(Conn);
  Check(Decode(WarmHeader));
  Check(WarmRequest.ParseHeader(Pointer(Data), Length(Data)) and (WarmRequest.Header.Count=10));
  WarmRequest.Free; WarmHeader.Free;
  try
    Baseline := Allocated;
    for I := 0 to N-1 do begin Headers[I] := THttpHeader.Create; Check(Decode(Headers[I])); end;
    Live := Allocated;
    for I := 0 to N-1 do FreeAndNil(Headers[I]);
    Released := Allocated;
    Writeln(Format('MEM kind=header n=%d live_bytes=%d per_object=%.3f released_delta=%d',
      [N, Live-Baseline, (Live-Baseline)/N, Released-Baseline]));
    Baseline := Allocated;
    for I := 0 to N-1 do
    begin
      Requests[I] := TRequestAccess.Create(Conn);
      Check(Requests[I].ParseHeader(Pointer(Data), Length(Data)) and (Requests[I].Header.Count=10));
    end;
    Live := Allocated;
    for I := 0 to N-1 do FreeAndNil(Requests[I]);
    Released := Allocated;
    Writeln(Format('MEM kind=request n=%d live_bytes=%d per_object=%.3f released_delta=%d',
      [N, Live-Baseline, (Live-Baseline)/N, Released-Baseline]));
  finally
    for I := 0 to N-1 do begin Headers[I].Free; Requests[I].Free; end;
    Connection := nil; Server := nil;
  end;
end;
begin
  try FormatSettings.DecimalSeparator := '.'; Measure;
  except on E: Exception do begin Writeln(E.ClassName, ': ', E.Message); Halt(1); end; end;
end.
