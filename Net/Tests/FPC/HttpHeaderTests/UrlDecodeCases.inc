// 保留原UTF8字节往返算法作为独立预期，不调用被测UrlDecode来生成预期。
function RunUrlDecodeTests: Integer;
const
  Cases: array[0..36] of string = (
    '', '/a/b?x=1&y=2#frag', 'plain path'#9, #0, 'a'#0'b', 'a'#0'%41+b',
    '%', '%0', '%XY', '%G0', '%41%2f', '%26%3D', '%252F', '+', '%2B+',
    'a+b%20c', '%00%7F', '中文', '前%E4%B8%AD后', #$D83D#$DE00,
    '%F0%9F%98%80', #$FEFF'abc', 'abc'#$FEFF, '%EF%BB%BFabc',
    #$D800, #$DC00, #$D800'a', 'a'#$DC00, #$D800#$D800, #$DC00#$D800,
    'a'#0#$D800, '%FF', '%C0%AF', '%E4%B8', '%ED%A0%80', '%F4%90%80%80',
    '%'#$D800);
var
  I, LChecks, LOracleExceptions: Integer;
  LInput, LOutput, LOriginal: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('URL解码: ' + AMessage);
  end;

  function ReferenceDecode(const S: string): string;
  var
    LSrcBytes, LDstBytes: TBytes;
    LSrcLen, LSrcIdx, LDstIdx: Integer;
    H, L, C: Byte;
  begin
    if S = '' then Exit('');
    LSrcBytes := TEncoding.UTF8.GetBytes(S);
    LSrcLen := Length(LSrcBytes);
    SetLength(LDstBytes, LSrcLen);
    LSrcIdx := 0;
    LDstIdx := 0;
    while LSrcIdx < LSrcLen do
    begin
      C := LSrcBytes[LSrcIdx];
      case C of
        Ord('+'):
          begin
            LDstBytes[LDstIdx] := Ord(' ');
            Inc(LSrcIdx);
          end;
        Ord('%'):
          begin
            if (LSrcIdx + 2 < LSrcLen)
              and TUtils.HexCharToByte(Char(LSrcBytes[LSrcIdx + 1]), H)
              and TUtils.HexCharToByte(Char(LSrcBytes[LSrcIdx + 2]), L) then
            begin
              LDstBytes[LDstIdx] := L + (H shl 4);
              Inc(LSrcIdx, 3);
            end else
            begin
              LDstBytes[LDstIdx] := Ord('%');
              Inc(LSrcIdx);
            end;
          end;
      else
        LDstBytes[LDstIdx] := C;
        Inc(LSrcIdx);
      end;
      Inc(LDstIdx);
    end;
    SetLength(LDstBytes, LDstIdx);
    Result := TEncoding.UTF8.GetString(LDstBytes);
  end;

  procedure CheckDecode(const S, ALabel: string; const AIdentity: Boolean);
  var
    LExpected, LActual, LExpectedError, LActualError: string;
    LExpectedMessage, LActualMessage: string;
  begin
    LExpectedError := '';
    LActualError := '';
    try LExpected := ReferenceDecode(S);
    except on E: Exception do
      begin LExpectedError := E.ClassName; LExpectedMessage := E.Message; end;
    end;
    try LActual := TCrossHttpUtils.UrlDecode(S);
    except on E: Exception do
      begin LActualError := E.ClassName; LActualMessage := E.Message; end;
    end;
    Check(LActualError = LExpectedError, ALabel + ' 异常类别保持');
    if LExpectedError = '' then
      Check(LActual = LExpected, ALabel + ' 与旧算法结果相同')
    else
    begin
      Inc(LOracleExceptions);
      Check(LActualMessage = LExpectedMessage, ALabel + ' 异常消息保持');
    end;
    if AIdentity then
    begin
      Check(LActualError = '', ALabel + ' ASCII不触发编码异常');
      Check(LActual = S, ALabel + ' 保留完整ASCII内容及长度');
    end;
  end;

begin
  LChecks := 0;
  LOracleExceptions := 0;
  for I := Low(Cases) to High(Cases) do
    CheckDecode(Cases[I], 'fixed-' + IntToStr(I), False);
  // 包含NUL、控制字符和DEL；此函数不替代URL/HTTP语法校验，也不能按零字符截断。
  for I := 0 to 127 do
    CheckDecode('prefix' + Char(I) + 'tail', 'ascii-' + IntToStr(I),
      (I <> Ord('%')) and (I <> Ord('+')));
  CheckDecode(StringOfChar('x', 1024), 'long-ascii', True);
  CheckDecode(StringOfChar('x', 1024) + '%20z', 'long-percent-tail', False);
  CheckDecode(StringOfChar('x', 1024) + '中', 'long-unicode-tail', False);

  // 正常字符串索引写入应写时复制；不要求返回值和输入的内部指针不同。
  LInput := StringOfChar('x', 32);
  LOriginal := LInput;
  LOutput := TCrossHttpUtils.UrlDecode(LInput);
  LOutput[1] := 'y';
  Check(LInput = LOriginal, '修改输出不污染输入');
  Check(LOutput = 'y' + Copy(LOriginal, 2, MaxInt), '输出可正常修改');
  LOutput := TCrossHttpUtils.UrlDecode(LInput);
  LInput[2] := 'z';
  Check(LOutput = LOriginal, '修改输入不污染已有输出');
  Check(LInput = 'xz' + Copy(LOriginal, 3, MaxInt), '输入写时复制');
  LInput := '';
  Check(LOutput = LOriginal, '输入释放后输出仍有效');
  LOutput := TCrossHttpUtils.UrlDecode('literal');
  LOutput[1] := 'L';
  Check(TCrossHttpUtils.UrlDecode('literal') = 'literal', '修改返回值不污染常量');
  LInput := 'self'#0'tail';
  LInput := TCrossHttpUtils.UrlDecode(LInput);
  Check(LInput = 'self'#0'tail', 'ASCII自赋值保留NUL及后续内容');
  LInput := 'a+b%2Bc';
  LInput := TCrossHttpUtils.UrlDecode(LInput);
  Check(LInput = 'a b+c', '转义路径自赋值');
  // 字节缓冲可以原位更新，但输入字符串及其别名必须保持原样。
  LInput := 'a%41+中';
  LOriginal := LInput;
  LOutput := TCrossHttpUtils.UrlDecode(LInput);
  Check(LInput = 'a%41+中', '混合转义解码不修改输入');
  Check(LOriginal = 'a%41+中', '混合转义解码不修改输入别名');
  Check(LOutput = 'aA 中', '混合转义与Unicode结果');
  Writeln('url-decode oracle-exceptions=', LOracleExceptions);
  Result := LChecks;
end;
