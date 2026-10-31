// 通过公开接口核对完整序列化输出；预期由输入片段拼出，不复用Encode内部的长度/指针算法。
function RunHeaderEncodeTests: Integer;
const
  FieldCounts: array[0..7] of Integer = (0, 1, 2, 3, 31, 32, 33, 127);
var
  H: THttpHeader;
  I, J, LChecks: Integer;
  LName, LValue, LExpected, LEncoded, LCopy, LSaved: string;
  B, LRaw: TBytes;
  E: TParamEntry;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('Header输出: ' + AMessage);
  end;

  procedure CheckError(const AExpected: string);
  var
    LActual, LDiscarded: string;
  begin
    LActual := '';
    try
      LDiscarded := H.Encode;
    except
      on E: EConvertError do LActual := E.Message;
    end;
    Check(LActual = AExpected, '异常类别及消息: ' + AExpected);
  end;

begin
  LChecks := 0;
  H := THttpHeader.Create;
  try
    for J := Low(FieldCounts) to High(FieldCounts) do
    begin
      H.Clear;
      LExpected := '';
      for I := 0 to FieldCounts[J] - 1 do
      begin
        // 任意名称和重复项；不能依赖固定名称、数量、长度或字段排序。
        LName := 'x-Custom!' + IntToStr(I mod 11);
        case I mod 4 of
          0: LValue := '';
          1: LValue := StringOfChar('a', (I * 37) mod 251);
          2: LValue := #9'中文'#$D83D#$DE00' end'#9;
        else
          LValue := StringOfChar('b', 8193) + '尾';
        end;
        H.Add(LName, LValue, True);
        LExpected := LExpected + LName + ': ' + LValue + #13#10;
      end;
      LExpected := LExpected + #13#10;
      LEncoded := H.Encode;
      Check(Length(LEncoded) = Length(LExpected), 'UTF16长度及末尾空行');
      Check(LEncoded = LExpected, '空集合/多字段/重复项/Unicode/长值完整输出');
      Check(H.Count = FieldCounts[J], 'Encode不修改字段数量');
      LCopy := LEncoded;
      LCopy[1] := 'Z';
      Check(LEncoded = LExpected, '返回字符串副本写时复制');
      Check(H.Encode = LExpected, '修改输出副本不污染后续Encode');
      LSaved := LEncoded;
      H.Clear;
      Check(LSaved = LExpected, '清空容器不污染既有输出');
    end;

    // 完整记录允许Value/raw由调用方维护；发送必须以当前Value为准而非raw。
    B := Bytes('旧值');
    E := TParamEntry.Create('X-Source', Pointer(B), Length(B));
    LRaw := E.RawValue;
    E.Value := '现值'#$D83D#$DE00;
    H.Add(E);
    H.Add('x-source', '', True);
    LExpected := 'X-Source: 现值'#$D83D#$DE00#13#10'x-source: '#13#10#13#10;
    Check(H.Encode = LExpected, '当前Value、大小写及重复项顺序');
    Check(H.Items[0].HasRawValue and SameBytes(H.Items[0].RawValue, LRaw),
      'Encode保留原始字节与来源');
    Check(H.Items[0].Value = E.Value, 'Encode不改写Value');

    // 前面的坏代理不能遮蔽后续字段错误；Name/Value错误均先于最终UTF16错误。
    H.Clear;
    H.Add('X-First', #$D800);
    H.Add('X-Last', #0);
    CheckError('Invalid HTTP header field');
    H['X-Last'] := 'ok';
    H.Add('Bad Name', 'ok');
    CheckError('Invalid HTTP header field');
    H.Remove('Bad Name');
    CheckError('Invalid UTF-16 in HTTP header');
    H['X-First'] := #$D83D#$DE00;
    Check(H.Encode = 'X-First: '#$D83D#$DE00#13#10'X-Last: ok'#13#10#13#10,
      '失败后修复输入可正常再次Encode');
    H['X-Last'] := #$DC00;
    CheckError('Invalid UTF-16 in HTTP header');
    H['X-Last'] := #$DC00#127;
    CheckError('Invalid HTTP header field');
  finally
    H.Free;
  end;
  Result := LChecks;
end;
