// 保留旧编码算法作独立预期；不调用被测UrlEncode生成oracle。
function RunUrlEncodeTests: Integer;
const
  Cases: array[0..23] of string = (
    '', 'AZaz09-_.~', '/a/b?x=1&v=2#frag', 'a b+c', #0, 'a'#0'b',
    'a'#0'%2f', '%', '%0', '%XY', '%2f', '%2F%41', '%252F', 'plain%2ftext',
    '中文', #$00E9#$00FF, #$FEFF'abc', 'abc'#$FEFF, #$D83D#$DE00,
    #$D800, #$DC00, #$D800'a', 'a'#$DC00, '%'#$D800);
  NoConversions: array[0..3] of TSysCharSet = (
    [], ['/', ':', '@'], ['%', '+', ' ', #0], [#128..#255]);
var
  I, J, K, LChecks, LOracleExceptions: Integer;
  LInput, LOriginal, LOutput: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('URL编码: ' + AMessage);
  end;

  function ReferenceEncode(const S: string; const ANoConversion: TSysCharSet;
    const APreserveEncoded: Boolean): string;
  const
    Hex: array[0..15] of Char = ('0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'A', 'B', 'C', 'D', 'E', 'F');
  var
    B: TBytes;
    N, Index: Integer;
    C: Byte;
    P: PChar;

    function IsHex(const AByte: Byte): Boolean;
    begin
      case AByte of
        Ord('0')..Ord('9'), Ord('a')..Ord('f'), Ord('A')..Ord('F'): Result := True;
      else Result := False; end;
    end;

  begin
    if S = '' then Exit('');
    B := TEncoding.UTF8.GetBytes(S);
    N := Length(B);
    SetLength(Result, N * 3);
    P := PChar(Result);
    Index := 0;
    while Index < N do
    begin
      C := B[Index];
      case C of
        Ord('0')..Ord('9'), Ord('a')..Ord('z'), Ord('A')..Ord('Z'),
        Ord('-'), Ord('_'), Ord('.'), Ord('~'):
          begin P^ := Char(C); Inc(P); end;
      else
        if APreserveEncoded and (C = Ord('%')) and (Index + 2 < N)
          and IsHex(B[Index + 1]) and IsHex(B[Index + 2]) then
        begin
          P^ := '%'; Inc(P);
          P^ := Char(B[Index + 1]); Inc(P);
          P^ := Char(B[Index + 2]); Inc(P);
          Inc(Index, 2);
        end else
        if CharInSet(Char(C), ANoConversion) then
        begin P^ := Char(C); Inc(P); end else
        begin
          P^ := '%'; Inc(P);
          P^ := Hex[C shr 4]; Inc(P);
          P^ := Hex[C and $F]; Inc(P);
        end;
      end;
      Inc(Index);
    end;
    SetLength(Result, P - PChar(Result));
  end;

  procedure CheckEncode(const S: string; const ANoConversion: TSysCharSet;
    const APreserveEncoded: Boolean; const ALabel: string);
  var
    LExpected, LActual, LExpectedError, LActualError: string;
    LExpectedMessage, LActualMessage: string;
  begin
    LExpectedError := '';
    LActualError := '';
    try LExpected := ReferenceEncode(S, ANoConversion, APreserveEncoded);
    except on E: Exception do
      begin LExpectedError := E.ClassName; LExpectedMessage := E.Message; end;
    end;
    try LActual := TCrossHttpUtils.UrlEncode(S, ANoConversion, APreserveEncoded);
    except on E: Exception do
      begin LActualError := E.ClassName; LActualMessage := E.Message; end;
    end;
    Check(LActualError = LExpectedError, ALabel + ' 异常类别保持');
    if LExpectedError = '' then
      Check(LActual = LExpected, ALabel + ' 与旧算法结果一致')
    else
    begin
      Inc(LOracleExceptions);
      Check(LActualMessage = LExpectedMessage, ALabel + ' 异常消息保持');
    end;
  end;

begin
  LChecks := 0;
  LOracleExceptions := 0;
  // 包括UTF8高字节白名单；不能把ANoConversion当成Unicode字符白名单。
  for J := Low(NoConversions) to High(NoConversions) do
    for K := 0 to 1 do
      for I := Low(Cases) to High(Cases) do
        CheckEncode(Cases[I], NoConversions[J], K <> 0,
          'fixed-' + IntToStr(J) + '/' + IntToStr(K) + '/' + IntToStr(I));
  for I := 0 to 127 do
    CheckEncode('p' + Char(I) + 't', [], False, 'ascii-' + IntToStr(I));
  CheckEncode(StringOfChar('x', 1024), [], False, 'long-plain');
  CheckEncode(StringOfChar('x', 1024) + '%2f', [], True, 'long-percent-tail');
  CheckEncode(StringOfChar('x', 1024) + '中', [], False, 'long-unicode-tail');
  CheckEncode(StringOfChar('x', 1024) + '/', ['/', ':', '@'], True, 'path-option');

  // 返回字符串可以共享存储，但正常索引写入必须保持COW及独立生命周期。
  LInput := StringOfChar('x', 32);
  LOriginal := LInput;
  LOutput := TCrossHttpUtils.UrlEncode(LInput);
  LOutput[1] := 'y';
  Check(LInput = LOriginal, '修改输出不污染输入');
  Check(LOutput = 'y' + Copy(LOriginal, 2, MaxInt), '输出可修改');
  LOutput := TCrossHttpUtils.UrlEncode(LInput);
  LInput[2] := 'z';
  Check(LOutput = LOriginal, '修改输入不污染输出');
  Check(LInput = 'xz' + Copy(LOriginal, 3, MaxInt), '输入写时复制');
  LInput := '';
  Check(LOutput = LOriginal, '输入释放后输出保持');
  LOutput := TCrossHttpUtils.UrlEncode('literal');
  LOutput[1] := 'L';
  Check(TCrossHttpUtils.UrlEncode('literal') = 'literal', '修改返回值不污染常量');
  LInput := 'AZaz09-_.~';
  LInput := TCrossHttpUtils.UrlEncode(LInput, [], True);
  Check(LInput = 'AZaz09-_.~', '未保留字符自赋值');
  LInput := 'a%2fb c';
  LInput := TCrossHttpUtils.UrlEncode(LInput, [], True);
  Check(LInput = 'a%2fb%20c', '保留已有转义时的自赋值');
  // 不额外保活输入：允许函数隐藏结果槽复用同一调用方变量。
  LInput := StringOfChar('x', 1024) + ' ';
  LInput := TCrossHttpUtils.UrlEncode(LInput);
  Check(LInput = StringOfChar('x', 1024) + '%20', '动态长前缀自赋值');
  Writeln('url-encode oracle-exceptions=', LOracleExceptions);
  Result := LChecks;
end;
