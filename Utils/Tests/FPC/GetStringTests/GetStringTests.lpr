program GetStringTests;

{$I zLib.inc}
{$IFNDEF FPC}{$APPTYPE CONSOLE}{$ENDIF}

uses
  SysUtils, Classes, Windows, Utils.Utils, GetStringReference;

type
  TOutcome = record
    Value, ErrorClass, ErrorMessage: string;
    Position: Int64;
  end;

  TCustomEncoding = class(TUTF8Encoding)
  protected
    function GetCharCount(Bytes: PByte; ByteCount: Integer): Integer; override;
    function GetChars(Bytes: PByte; ByteCount: Integer; Chars: PChar;
      CharCount: Integer): Integer; override;
  public
    function GetPreamble: TBytes; override;
  end;

  // 通过 TStream 包装内存流，覆盖非 TCustomMemoryStream 分支及读取失败。
  TWrappedStream = class(TStream)
  private
    FData: TMemoryStream;
    FFailRead: Boolean;
  public
    constructor Create(const ABytes: TBytes; AFailRead: Boolean = False);
    destructor Destroy; override;
    function Read(var Buffer; Count: Longint): Longint; override;
    function Write(const Buffer; Count: Longint): Longint; override;
    function Seek(const Offset: Int64; Origin: TSeekOrigin): Int64; override;
  end;

  {$IFDEF FPC}
  TBenchManager = TMemoryManager;
  TBenchSize = PtrUInt;
  {$ELSE}
  TBenchManager = TMemoryManagerEx;
  TBenchSize = NativeInt;
  {$ENDIF}

var
  Checks: Integer;
  OriginalManager, CountingManager: TBenchManager;
  AllocationCalls, AllocationBytes: Int64;
  Encodings: array of TEncoding;
threadvar
  Tracking: Boolean;

procedure Check(ACondition: Boolean; const AMessage: string);
begin
  Inc(Checks);
  if not ACondition then raise Exception.Create(AMessage);
end;

function TCustomEncoding.GetPreamble: TBytes;
begin
  Result := TBytes.Create($AA, $BB);
end;

function TCustomEncoding.GetCharCount(Bytes: PByte; ByteCount: Integer): Integer;
begin
  if (ByteCount > 0) and (Bytes[0] = $FD) then
    raise EConvertError.Create('自定义计数失败');
  Result := ByteCount;
end;

function TCustomEncoding.GetChars(Bytes: PByte; ByteCount: Integer;
  Chars: PChar; CharCount: Integer): Integer;
var I: Integer;
begin
  if (ByteCount > 0) and (Bytes[0] = $FC) then
    raise EConvertError.Create('自定义转换失败');
  Result := ByteCount;
  for I := 0 to Result - 1 do Chars[I] := Char($100 + Bytes[I]);
end;

constructor TWrappedStream.Create(const ABytes: TBytes; AFailRead: Boolean);
begin
  inherited Create;
  FFailRead := AFailRead;
  FData := TMemoryStream.Create;
  if Length(ABytes) > 0 then FData.WriteBuffer(ABytes[0], Length(ABytes));
  FData.Position := 0;
end;

destructor TWrappedStream.Destroy;
begin
  FData.Free;
  inherited;
end;

function TWrappedStream.Read(var Buffer; Count: Longint): Longint;
begin
  if FFailRead then raise EReadError.Create('读取失败');
  Result := FData.Read(Buffer, Count);
end;

function TWrappedStream.Write(const Buffer; Count: Longint): Longint;
begin
  Result := FData.Write(Buffer, Count);
end;

function TWrappedStream.Seek(const Offset: Int64; Origin: TSeekOrigin): Int64;
begin
  Result := FData.Seek(Offset, Origin);
end;

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

procedure BeginCounting;
begin
  GetMemoryManager(OriginalManager);
  CountingManager := OriginalManager;
  CountingManager.GetMem := CountGetMem;
  CountingManager.AllocMem := CountAllocMem;
  CountingManager.ReallocMem := CountReallocMem;
  AllocationCalls := 0;
  AllocationBytes := 0;
  SetMemoryManager(CountingManager);
  Tracking := True;
end;

procedure EndCounting;
begin
  Tracking := False;
  SetMemoryManager(OriginalManager);
end;

function Outcome(AReference: Boolean; P: PByte; ASize: Integer;
  AEncoding: TEncoding; AHelper: Boolean = False): TOutcome;
begin
  Result.Value := '';
  Result.ErrorClass := '';
  Result.ErrorMessage := '';
  Result.Position := 0;
  try
    if AHelper then
    begin
      if AReference then Result.Value := ReferenceDecode(AEncoding, P, ASize)
      else Result.Value := AEncoding.GetString(P, ASize);
    end else
    if AReference then Result.Value := TReferenceUtils.GetString(P, ASize, AEncoding)
    else Result.Value := TUtils.GetString(P, ASize, AEncoding);
  except
    on E: Exception do
    begin
      Result.ErrorClass := E.ClassName;
      Result.ErrorMessage := E.Message;
    end;
  end;
end;

procedure SameOutcome(const AExpected, AActual: TOutcome; const AName: string);
begin
  Check((AExpected.Value = AActual.Value) and
    (AExpected.ErrorClass = AActual.ErrorClass) and
    (AExpected.ErrorMessage = AActual.ErrorMessage) and
    (AExpected.Position = AActual.Position),
    AName + ': expected=' + AExpected.ErrorClass + '/' + AExpected.ErrorMessage +
    ' len=' + IntToStr(Length(AExpected.Value)) + ' actual=' + AActual.ErrorClass +
    '/' + AActual.ErrorMessage + ' len=' + IntToStr(Length(AActual.Value)));
end;

procedure Same(P: PByte; ASize: Integer; AEncoding: TEncoding;
  const AName: string; AHelper: Boolean = False);
begin
  SameOutcome(Outcome(True, P, ASize, AEncoding, AHelper),
    Outcome(False, P, ASize, AEncoding, AHelper), AName);
end;

procedure CheckBuffer(const B: TBytes; const AName: string; AAll: Boolean);
var
  I, J, K, N1, N2: Integer;
  E1, E2: TEncoding;
  B1, B2: Boolean;
  Work: TBytes;
begin
  if AAll then K := High(Encodings) else K := 0;
  for I := 0 to K do
  begin
    Same(PByte(B), Length(B), Encodings[I], AName + '/' + IntToStr(I));
    if I > 0 then Same(PByte(B), Length(B), Encodings[I], AName + '/helper', True);
    E1 := Encodings[I]; E2 := E1;
    N1 := TReferenceUtils.GetBufEncoding(PByte(B), Length(B), E1, nil);
    N2 := TUtils.GetBufEncoding(PByte(B), Length(B), E2, nil);
    Check((N1 = N2) and (E1 = E2), AName + '/encoding');
  end;
  B1 := TReferenceUtils.IsUTF8(PByte(B), Length(B));
  B2 := TUtils.IsUTF8(PByte(B), Length(B));
  Check(B1 = B2, AName + '/IsUTF8');
  if AAll then
  begin
    SetLength(Work, Length(B) + 16);
    for J := 0 to 7 do
    begin
      if Length(B) > 0 then Move(B[0], Work[J], Length(B));
      Same(@Work[J], Length(B), nil, AName + '/offset');
    end;
    for I := 0 to High(Encodings) do
    begin
      E1 := nil; E2 := nil;
      N1 := TReferenceUtils.GetBufEncoding(PByte(B), Length(B), E1, Encodings[I]);
      N2 := TUtils.GetBufEncoding(PByte(B), Length(B), E2, Encodings[I]);
      Check((N1 = N2) and (E1 = E2), AName + '/default');
    end;
  end;
end;

procedure CheckBytes(const AValues: array of Byte);
var B: TBytes; I: Integer;
begin
  SetLength(B, Length(AValues));
  for I := 0 to High(AValues) do B[I] := AValues[I];
  CheckBuffer(B, 'boundary', True);
end;

function StreamOutcome(AReference: Boolean; AStream: TStream;
  AEncoding: TEncoding): TOutcome;
begin
  Result.Value := ''; Result.ErrorClass := ''; Result.ErrorMessage := '';
  try
    if AReference then Result.Value := TReferenceUtils.GetString(AStream, AEncoding)
    else Result.Value := TUtils.GetString(AStream, AEncoding);
  except
    on E: Exception do
    begin Result.ErrorClass := E.ClassName; Result.ErrorMessage := E.Message; end;
  end;
  if AStream = nil then Result.Position := -1 else Result.Position := AStream.Position;
end;

procedure TestStreams;
var B: TBytes; S: TStream; A, C: TOutcome; I, J, K: Integer;
begin
  SameOutcome(StreamOutcome(True, nil, nil), StreamOutcome(False, nil, nil), 'nil stream');
  for I := 0 to 3 do
  begin
    case I of
      0: B := nil;
      1: B := TBytes.Create(65, 0, 66, $E4, $B8, $AD);
      2: B := TBytes.Create($EF, $BB, $BF, $C0, $80);
      3: B := TBytes.Create($FF, $FE, 65);
    end;
    for J := 0 to 2 do
    begin
      if J = 0 then
      begin
        S := TMemoryStream.Create;
        if Length(B) > 0 then S.WriteBuffer(B[0], Length(B));
      end else S := TWrappedStream.Create(B, J = 2);
      try
        for K := 0 to Length(B) + 1 do
        begin
          S.Position := K; A := StreamOutcome(True, S, nil);
          S.Position := K; C := StreamOutcome(False, S, nil);
          SameOutcome(A, C, 'stream/position');
        end;
      finally S.Free; end;
    end;
  end;
end;

procedure TestGuard;
var
  LInfo: TSystemInfo;
  LBase, P: PByte;
  LOldProtect: DWORD;
  N, I, K, N1, N2: Integer;
  E, E1, E2: TEncoding;
begin
  GetSystemInfo(LInfo);
  LBase := VirtualAlloc(nil, LInfo.dwPageSize * 2, MEM_RESERVE or MEM_COMMIT, PAGE_READWRITE);
  if LBase = nil then RaiseLastOSError;
  try
    if not VirtualProtect(LBase + LInfo.dwPageSize, LInfo.dwPageSize, PAGE_NOACCESS, LOldProtect) then
      RaiseLastOSError;
    for N := 1 to 80 do
    begin
      P := LBase + LInfo.dwPageSize - N;
      for K := 0 to 3 do
      begin
        for I := 0 to N - 1 do P[I] := 65;
        case K of
          1: P[0] := $EF;
          2: P[0] := $FF;
          3:
            if N >= 4 then
            begin
              P[N-4] := $F0; P[N-3] := $9F; P[N-2] := $98; P[N-1] := $80;
            end else P[N-1] := $F0;
        end;
        for E in Encodings do
        begin
          Same(P, N, E, 'guard/' + IntToStr(N) + '/' + IntToStr(K));
          E1 := E; E2 := E;
          N1 := TReferenceUtils.GetBufEncoding(P, N, E1, nil);
          N2 := TUtils.GetBufEncoding(P, N, E2, nil);
          Check((N1 = N2) and (E1 = E2), 'guard encoding');
        end;
        Check(TReferenceUtils.IsUTF8(P, N) = TUtils.IsUTF8(P, N), 'guard IsUTF8');
      end;
    end;
  finally VirtualFree(LBase, 0, MEM_RELEASE); end;
end;

procedure TestAll;
var B: TBytes; S: string; I, J, K: Integer; E: TEncoding;
begin
  CheckBytes([]); CheckBytes([0]); CheckBytes([65, 0, 66]);
  CheckBytes([$EF]); CheckBytes([$EF, $BB]); CheckBytes([$EF, $BB, $BF]);
  CheckBytes([$EF, $BB, $BF, 65]); CheckBytes([$FF, $FE]); CheckBytes([$FE, $FF]);
  CheckBytes([$FF, $FE, 65]); CheckBytes([$FE, $FF, 0, 65, 0]);
  CheckBytes([$AA, $BB, 65]); CheckBytes([$FD]); CheckBytes([$FC]);
  CheckBytes([$C0, $80]); CheckBytes([$C1, $BF]); CheckBytes([$C2, $80]);
  CheckBytes([$E0, $80, $80]); CheckBytes([$E0, $A0, $80]);
  CheckBytes([$ED, $A0, $80]); CheckBytes([$ED, $9F, $BF]);
  CheckBytes([$EF, $BF, $BD]); CheckBytes([$EF, $BF, $BE]); CheckBytes([$EF, $BF, $BF]);
  CheckBytes([$F0, $80, $80, $80]); CheckBytes([$F0, $90, $80, $80]);
  CheckBytes([$F4, $8F, $BF, $BF]); CheckBytes([$F4, $90, $80, $80]);
  CheckBytes([$F5, $80, $80, $80]); CheckBytes([$F8, $80, $80, $80, $80]);
  CheckBytes([$FC, $80, $80, $80, $80, $80]); CheckBytes([$F0, $9F, $98]);
  for E in Encodings do
  begin
    Same(nil, 8, E, 'nil'); Same(nil, -1, E, 'nil negative');
    if E <> nil then Same(nil, 0, E, 'helper nil', True);
  end;
  B := TBytes.Create(65);
  for E in Encodings do
  begin
    Same(PByte(B), 0, E, 'zero'); Same(PByte(B), -1, E, 'negative');
    if E <> nil then
    begin Same(PByte(B), 0, E, 'helper zero', True); Same(PByte(B), -1, E, 'helper negative', True); end;
  end;
  for I := 0 to 255 do
  begin
    B := TBytes.Create(Byte(I)); CheckBuffer(B, 'one/' + IntToStr(I), True);
    SetLength(B, 2); B[0] := Byte(I);
    for J := 0 to 255 do
    begin
      B[1] := Byte(J);
      CheckBuffer(B, 'two/' + IntToStr(I) + '/' + IntToStr(J), False);
      Same(PByte(B), 2, TEncoding.UTF8, 'two explicit');
    end;
  end;
  RandSeed := 20261027;
  for I := 1 to 4000 do
  begin
    SetLength(B, Random(97));
    for J := 0 to High(B) do B[J] := Byte(Random(256));
    CheckBuffer(B, 'random/' + IntToStr(I), I mod 8 = 0);
  end;
  for I := 1 to 1000 do
  begin
    S := '';
    for J := 1 to Random(80) + 1 do
    begin
      K := Random($110000);
      if (K >= $D800) and (K <= $DFFF) then K := 65;
      if K < $10000 then S := S + Char(K)
      else begin Dec(K, $10000); S := S + Char($D800 + (K shr 10)) + Char($DC00 + (K and $3FF)); end;
    end;
    B := TEncoding.UTF8.GetBytes(S);
    CheckBuffer(B, 'unicode/' + IntToStr(I), I mod 10 = 0);
  end;
  B := TEncoding.UTF8.GetBytes('prefix 中间 ' + #0 + ' suffix');
  for I := 0 to Length(B) do
    for J := 0 to Length(B) - I do
      SameOutcome(Outcome(True, PByte(B) + I, J, nil),
        Outcome(False, PByte(B) + I, J, nil), 'slice pointer');
  // 数组重载结果包括嵌入 NUL 与空切片，非法地址不作为测试输入。
  for I := 0 to 6 do
  begin
    Check(TUtils.GetString(B, I, 6 - I) = TReferenceUtils.GetString(B, I, 6 - I), 'array slice');
    Check(TUtils.GetString(B, I, 0) = '', 'empty slice');
  end;
  Check(TUtils.GetString(B) = TReferenceUtils.GetString(B), 'whole array');
  Check(TUtils.GetString(B, 0, -1) = '', 'negative slice');
  TestStreams;
  TestGuard;
  Writeln('PASS checks=', Checks);
end;

procedure Benchmark(const AName: string; ACount: Integer; AAlloc: Boolean);
var
  B, Pattern: TBytes;
  S: string;
  E: TEncoding;
  Stream: TStream;
  N, I, J: Integer;
  Start, Finish, Frequency, Checksum: Int64;
  Elapsed: Double;
begin
  if not SetProcessAffinityMask(GetCurrentProcess, 4) then
    RaiseLastOSError;
  E := nil; Stream := nil; Pattern := nil; N := 1024;
  if AName = 'ascii8' then N := 8
  else if (AName = 'ascii32') or (AName = 'explicit32') then N := 32
  else if AName = 'ascii128' then N := 128
  else if (AName = 'ascii64k') or (AName = 'mixed64k') then N := 65536;
  if AName = 'chinese1k' then Pattern := TEncoding.UTF8.GetBytes('中文编码转换测试')
  else if (AName = 'mixed1k') or (AName = 'mixed64k') then Pattern := TEncoding.UTF8.GetBytes('Header 中文 ' + #$D83D#$DE00)
  else if AName = 'fallback' then Pattern := TBytes.Create($D6, $D0, $CE, $C4, $FF, 65)
  else Pattern := TBytes.Create(65, 66, 67, 68, 32, 48, 49, 50);
  // 完整重复，避免截断合法多字节样本。
  N := (N div Length(Pattern)) * Length(Pattern);
  SetLength(B, N);
  for I := 0 to N - 1 do B[I] := Pattern[I mod Length(Pattern)];
  if (AName = 'explicit32') or (AName = 'explicit1k') then E := TEncoding.UTF8;
  if AName = 'custom' then E := Encodings[High(Encodings)];
  if AName = 'bom1k' then
  begin
    SetLength(B, N + 3); Move(B[0], B[3], N);
    B[0] := $EF; B[1] := $BB; B[2] := $BF;
  end;
  if (AName = 'utf16le') or (AName = 'utf16be') then
  begin
    S := StringOfChar('A', 256) + '中文' + #$D83D#$DE00;
    if AName = 'utf16le' then E := TEncoding.Unicode else E := TEncoding.BigEndianUnicode;
    B := E.GetBytes(S);
  end;
  if AName = 'memory' then
  begin
    Stream := TMemoryStream.Create;
    Stream.WriteBuffer(B[0], Length(B));
  end else if AName = 'stream' then Stream := TWrappedStream.Create(B);
  try
    for J := 0 to 99 do S := TUtils.GetString(B, E);
    S := ''; Checksum := 0;
    QueryPerformanceFrequency(Frequency);
    if AAlloc then BeginCounting;
    try
      QueryPerformanceCounter(Start);
      for I := 1 to ACount do
      begin
        if Stream <> nil then S := TUtils.GetString(Stream, E)
        else S := TUtils.GetString(PByte(B), Length(B), E);
        Inc(Checksum, Length(S));
        if Length(S) > 0 then Inc(Checksum, Ord(S[Length(S)]));
        S := '';
      end;
      QueryPerformanceCounter(Finish);
    finally if AAlloc then EndCounting; end;
    Elapsed := (Finish - Start) * 1000000.0 / Frequency;
    Writeln('BENCH ', AName, ' us=', Elapsed:0:3, ' calls=', AllocationCalls,
      ' bytes=', AllocationBytes, ' checksum=', Checksum);
  finally Stream.Free; end;
end;

procedure InitEncodings;
begin
  SetLength(Encodings, 11);
  Encodings[0] := nil;
  Encodings[1] := TEncoding.UTF8;
  Encodings[2] := TEncoding.Unicode;
  Encodings[3] := TEncoding.BigEndianUnicode;
  Encodings[4] := TEncoding.Default;
  Encodings[5] := TEncoding.ASCII;
  Encodings[6] := TUTF8Encoding.Create;
  Encodings[7] := TUnicodeEncoding.Create;
  {$IFDEF DELPHI}
  Encodings[8] := TUTF8Encoding.Create(False);
  Encodings[9] := TUnicodeEncoding.Create(False);
  {$ELSE}
  Encodings[8] := TUTF8Encoding.Create(65001, 8, 0);
  Encodings[9] := TBigEndianUnicodeEncoding.Create;
  {$ENDIF}
  Encodings[10] := TCustomEncoding.Create;
end;

var I: Integer;
begin
  try
    InitEncodings;
    try
      if ParamStr(1) = 'bench' then Benchmark(ParamStr(2), StrToInt(ParamStr(3)), ParamStr(4) = 'alloc')
      else if ParamStr(1) = 'guard' then
      begin TestGuard; Writeln('PASS checks=', Checks); end
      else TestAll;
    finally
      for I := 6 to High(Encodings) do Encodings[I].Free;
    end;
  except
    on E: Exception do
    begin Writeln('FAIL ', E.ClassName, ': ', E.Message); Halt(1); end;
  end;
end.
