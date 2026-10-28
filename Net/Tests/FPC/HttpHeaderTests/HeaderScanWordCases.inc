// 只用 Header 公开 API；编码结果/异常以未修改的 GetString 为独立转换参考。
function RunHeaderScanWordTests: Integer;
var
  H: THttpHeader;
  LChecks, V, J, N: Integer;
  B, LValue: TBytes;
  LExpected, LActual: Boolean;

  procedure Check(ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then
      raise Exception.CreateFmt('Header scan check %d: %s', [LChecks, AMessage]);
  end;

  procedure CheckBytes(const A, B: TBytes);
  var
    K: Integer;
  begin
    Check(Length(A) = Length(B), 'raw length');
    for K := 0 to Length(A) - 1 do Check(A[K] = B[K], 'raw byte');
  end;

  procedure CheckValue(const AValue: TBytes; AValid: Boolean);
  var
    LBuffer: TBytes;
    LText, LExpectedException, LActualException: string;
    LOK: Boolean;
    E: TParamEntry;
    K: Integer;
  begin
    // 调用方使用首尾 a/Z 锚点，故值没有需要裁掉的外侧 OWS。
    SetLength(LBuffer, Length(AValue) + 6);
    LBuffer[0] := Ord('X'); LBuffer[1] := Ord(':');
    if Length(AValue) > 0 then Move(AValue[0], LBuffer[2], Length(AValue));
    K := Length(AValue) + 2;
    LBuffer[K] := 13; LBuffer[K + 1] := 10;
    LBuffer[K + 2] := 13; LBuffer[K + 3] := 10;
    LExpectedException := '';
    if AValid then
      try
        LText := TUtils.GetString(PByte(AValue), Length(AValue));
      except
        on X: Exception do LExpectedException := X.ClassName;
      end;
    LActualException := '';
    LOK := False;
    try
      LOK := H.Decode(Pointer(LBuffer), Length(LBuffer));
    except
      on X: Exception do LActualException := X.ClassName;
    end;
    Check(LActualException = LExpectedException, 'GetString exception equivalence');
    if LActualException <> '' then
    begin
      Check(H.Count = 0, 'conversion failure rollback');
      Exit;
    end;
    Check(LOK = AValid, 'value byte acceptance');
    if not AValid then
    begin
      Check(H.Count = 0, 'invalid byte rollback');
      Exit;
    end;
    Check(H.Count = 1, 'valid value count');
    E := H.Items[0];
    Check(E.Name = 'X', 'valid value name');
    Check(E.Value = LText, 'GetString value equivalence');
    Check(E.HasRawValue, 'valid value raw source');
    CheckBytes(E.RawValue, AValue);
    for K := 0 to Length(LBuffer) - 1 do LBuffer[K] := $CC;
    CheckBytes(H.Items[0].RawValue, AValue);
  end;

  {$IFDEF MSWINDOWS}
  procedure CheckGuardPages;
  var
    LInfo: TSystemInfo;
    LBase, P: PByte;
    LOldProtect: DWORD;
    LBuffer: TBytes;
    LSuffix, K: Integer;
    LEndText: string;
  begin
    GetSystemInfo(LInfo);
    LBase := VirtualAlloc(nil, LInfo.dwPageSize * 2,
      MEM_RESERVE or MEM_COMMIT, PAGE_READWRITE);
    if LBase = nil then RaiseLastOSError;
    try
      if not VirtualProtect(LBase + LInfo.dwPageSize, LInfo.dwPageSize,
        PAGE_NOACCESS, LOldProtect) then RaiseLastOSError;
      Check(H.Decode(LBase + LInfo.dwPageSize, 0), 'zero length guard pointer');
      Check(H.Count = 0, 'zero length guard count');
      // 扫描末端紧贴不可读页；0..33 覆盖不足8、整8、余数与全部对齐偏移。
      for K := 0 to 33 do
        for LSuffix := 0 to 3 do
        begin
          case LSuffix of
            0: LEndText := '';
            1: LEndText := #13;
            2: LEndText := #13#10;
          else
            LEndText := #13#10#13#10;
          end;
          LBuffer := TEncoding.ASCII.GetBytes('X:' + StringOfChar('a', K) + LEndText);
          P := LBase + LInfo.dwPageSize - Length(LBuffer);
          Move(LBuffer[0], P^, Length(LBuffer));
          Check(H.Decode(P, Length(LBuffer)) = (LSuffix <> 1), 'guard result');
          if LSuffix = 1 then
            Check(H.Count = 0, 'guard bare CR rollback')
          else
          begin
            Check(H.Count = 1, 'guard field count');
            Check(H.Items[0].Name = 'X', 'guard name');
            Check(H.Items[0].Value = StringOfChar('a', K), 'guard value');
          end;
        end;
    finally
      VirtualFree(LBase, 0, MEM_RELEASE);
    end;
  end;
  {$ENDIF}

begin
  LChecks := 0;
  H := THttpHeader.Create;
  try
    // 一字节名称避免 ':' 被误解释为非空名称中的分隔符。
    B := TEncoding.ASCII.GetBytes('X:ok'#13#10#13#10);
    for V := 0 to 255 do
    begin
      B[0] := Byte(V);
      LExpected := TCrossHttpUtils.IsTokenChar(Char(V));
      LActual := H.Decode(Pointer(B), Length(B));
      Check(LActual = LExpected, 'token byte equivalence');
      if LExpected then
      begin
        Check(H.Count = 1, 'token field count');
        Check(H.Items[0].Name = Char(V), 'token name');
      end else
        Check(H.Count = 0, 'invalid token rollback');
    end;
    // 第一块保证快路已经进入；第二块的8个位置分别注入所有256种字节。
    // 首尾锚点让 HTAB/SP 保留在值内部，CR 后接 a/Z，不会偶然成为合法 CRLF。
    for V := 0 to 255 do
    begin
      for J := 0 to 7 do
      begin
        SetLength(LValue, 17);
        for N := 0 to 15 do LValue[N] := Ord('a');
        LValue[16] := Ord('Z');
        LValue[8 + J] := Byte(V);
        CheckValue(LValue, TCrossHttpUtils.IsHeaderValueChar(Char(V)));
      end;
      // 包含全$FF、全$7F等极值，检查加法在Q+下也不溢出、不跨字节进位。
      for N := 0 to 7 do LValue[N] := Ord('a');
      for N := 8 to 15 do LValue[N] := Byte(V);
      LValue[16] := Ord('Z');
      CheckValue(LValue, TCrossHttpUtils.IsHeaderValueChar(Char(V)));
    end;
    {$IFDEF MSWINDOWS}CheckGuardPages;{$ENDIF}
  finally
    H.Free;
  end;
  Result := LChecks;
end;
