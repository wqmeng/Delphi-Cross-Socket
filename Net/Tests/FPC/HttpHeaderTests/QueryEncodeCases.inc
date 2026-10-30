// 旧版逐步拼接作为对照；UrlEncode本身不变，只验证参数集合的组装语义。
function RunQueryEncodeTests: Integer;
var
  Q: THttpUrlParams;
  I, J, K, LChecks, LOracleExceptions: Integer;
  LName, LValue, LSaved, LExpected: string;
  B, LRaw: TBytes;
  Entry: TParamEntry;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('Query输出: ' + AMessage);
  end;

  function ReferenceEncode(const AParams: THttpUrlParams): string;
  var
    N: Integer;
    AName, AValue: string;
  begin
    // 保留旧版分隔符、名称和值的处理先后；不复用新组装分支。
    Result := '';
    for N := 0 to AParams.Count - 1 do
    begin
      if N > 0 then Result := Result + '&';
      if AParams.EncodeName then
        AName := TCrossHttpUtils.UrlEncode(AParams.Items[N].Name)
      else
        AName := AParams.Items[N].Name;
      Result := Result + AName;
      if AParams.EncodeValue then
        AValue := TCrossHttpUtils.UrlEncode(AParams.Items[N].Value)
      else
        AValue := AParams.Items[N].Value;
      if AValue <> '' then Result := Result + '=' + AValue;
    end;
  end;

  procedure CheckCurrent(const ALabel: string);
  var
    LReference, LActual, LCopy: string;
    LReferenceClass, LActualClass, LReferenceError, LActualError: string;
    LCount: Integer;
  begin
    LCount := Q.Count;
    LReferenceClass := '';
    LActualClass := '';
    LReferenceError := '';
    LActualError := '';
    try
      LReference := ReferenceEncode(Q);
    except
      on Error: Exception do
      begin
        LReferenceClass := Error.ClassName;
        LReferenceError := Error.Message;
        Inc(LOracleExceptions);
      end;
    end;
    try
      LActual := Q.Encode;
    except
      on Error: Exception do
      begin
        LActualClass := Error.ClassName;
        LActualError := Error.Message;
      end;
    end;
    Check(LActualClass = LReferenceClass, ALabel + ': 异常类别');
    Check(LActualError = LReferenceError, ALabel + ': 异常消息');
    Check(Q.Count = LCount, ALabel + ': 字段数量不变');
    // 异常时不比较隐藏结果槽中的部分字符串，也不人为制造分配失败。
    if LReferenceClass <> '' then Exit;
    Check(Length(LActual) = Length(LReference), ALabel + ': 完整长度');
    Check(LActual = LReference, ALabel + ': 完整输出');
    LCopy := LActual;
    if LCopy <> '' then
    begin
      if LCopy[1] = #1 then LCopy[1] := #2 else LCopy[1] := #1;
    end;
    Check(LActual = LReference, ALabel + ': 输出副本写时复制');
    Check(Q.Encode = LReference, ALabel + ': 修改副本不污染再次输出');
  end;

begin
  LChecks := 0;
  LOracleExceptions := 0;
  Q := THttpUrlParams.Create;
  try
    // 名称和值编码开关的全部四种组合；False仍允许调用者直接写入保留字符。
    for J := 0 to 3 do
    begin
      Q.EncodeName := (J and 1) <> 0;
      Q.EncodeValue := (J and 2) <> 0;
      for I := 0 to 13 do
      begin
        Q.Clear;
        case I of
          0: ;
          1: Q.Add('', '', True);
          2: Q.Add('name', '', True);
          3: Q.Add('', 'value', True);
          4:
            begin
              Q.Add('', '', True);
              Q.Add('a', '1', True);
            end;
          5:
            begin
              Q.Add('a', '1', True);
              Q.Add('', '', True);
            end;
          6:
            begin
              Q.Add('dup', 'one', True);
              Q.Add('DUP', '', True);
              Q.Add('dup', 'three', True);
            end;
          7: Q.Add('k&=+', 'v&=+ %中', True);
          8: Q.Add('中文', '值'#$D83D#$DE00, True);
          9: Q.Add('n'#0#9, 'v'#0#13#10, True);
          10: Q.Add('n'#$D800, 'value', True);
          // 部分RTL将孤立代理编码为空；是否输出等号必须看编码后的值。
          11: Q.Add('name', #$DC00, True);
          12:
            begin
              Q.Add(#$D800, #$DC00, True);
              Q.Add('after', 'ok', True);
            end;
          13:
            for K := 0 to 31 do
            begin
              LName := 'key' + IntToStr(K mod 7);
              case K mod 4 of
                0: LValue := '';
                1: LValue := 'plain-' + IntToStr(K);
                2: LValue := '中文 &+'#$D83D#$DE00;
              else
                LValue := StringOfChar('x', 1025) + '%';
              end;
              Q.Add(LName, LValue, True);
            end;
        end;
        CheckCurrent('flags=' + IntToStr(J) + ',case=' + IntToStr(I));
      end;

      // 固定预期独立于旧版oracle，防止把空输出误当作没有前一项。
      Q.Clear;
      Q.Add('', '', True);
      Q.Add('a', '1', True);
      Check(Q.Encode = '&a=1', '空首项仍占据一个分隔位置');
      Q.Add('', '', True);
      Check(Q.Encode = '&a=1&', '空尾项保留分隔符');
      Q.Clear;
      Q.Add('', '', True);
      Q.Add('', '', True);
      Check(Q.Encode = '&', '两个空项');
      Q.Clear;
      Q.Add('dup', 'one', True);
      Q.Add('DUP', '', True);
      Q.Add('dup', 'three', True);
      Check(Q.Encode = 'dup=one&DUP&dup=three', '重复项顺序及名称大小写');

      // 字节来源记录也使用当前Value发送，不能回用保存的原始字节。
      Q.Clear;
      B := Bytes('旧值');
      Entry := TParamEntry.Create('source', Pointer(B), Length(B));
      LRaw := Entry.RawValue;
      Entry.Value := '新值 &';
      Q.Add(Entry);
      CheckCurrent('raw,flags=' + IntToStr(J));
      Entry := Q.Items[0];
      Check(Entry.HasRawValue, 'Encode不清除原始字节标记');
      Check(SameBytes(Entry.RawValue, LRaw), 'Encode不改写原始字节');
    end;

    Q.Clear;
    Q.EncodeName := False;
    Q.EncodeValue := False;
    LName := StringOfChar('n', 12);
    LValue := StringOfChar('v', 24);
    Q.Add(LName, LValue, True);
    LExpected := LName + '=' + LValue;
    LSaved := Q.Encode;
    LName[1] := 'x';
    LValue[1] := 'y';
    Check(LSaved = LExpected, '修改调用者输入不污染既有输出');
    Check(Q.Encode = LExpected, '修改调用者输入不污染容器');
    Q.Clear;
    Check(LSaved = LExpected, '清空容器后输出仍有效');
  finally
    Q.Free;
  end;
  Writeln('query-encode assembly oracle-exceptions=', LOracleExceptions);
  Result := LChecks;
end;
