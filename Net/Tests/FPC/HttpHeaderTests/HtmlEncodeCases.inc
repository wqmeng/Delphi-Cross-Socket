// 按码元拼接独立预期，保留现有NUL停止规则，不依赖被测缓冲区或ANSI转换。
function RunHtmlEncodeTests: Integer;
const
  Cases: array[0..22] of string = (
    '', 'plain AZaz09_-.', '&', '<', '>', '"', '''', '&<>"''',
    '&amp; &quot;', #0, 'prefix'#0'&<>"', '&'#0'"',
    '中文', #$00E9#$00FF, #$FEFF'a', 'a'#$FEFF, #$D83D#$DE00,
    #$D800, #$DC00, #$D800'&', '"'#$DC00, '中文&<>"', 'a'#9#10#13'b');
  Escaped: array[0..3] of Char = ('&', '<', '>', '"');
var
  I, LChecks: Integer;
  LInput, LOriginal, LOutput, LExpected: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('HTML编码: ' + AMessage);
  end;

  function ReferenceEncode(const S: string): string;
  var
    I: Integer;
  begin
    Result := '';
    for I := 1 to Length(S) do
    begin
      if S[I] = #0 then Break;
      case S[I] of
        '&': Result := Result + '&amp;';
        '<': Result := Result + '&lt;';
        '>': Result := Result + '&gt;';
        '"': Result := Result + '&quot;';
      else
        Result := Result + S[I];
      end;
    end;
  end;

  procedure CheckEncode(const S, ALabel: string);
  begin
    Check(TCrossHttpUtils.HtmlEncode(S) = ReferenceEncode(S), ALabel);
  end;

begin
  LChecks := 0;
  for I := Low(Cases) to High(Cases) do
    CheckEncode(Cases[I], '固定用例 ' + IntToStr(I));
  Check(TCrossHttpUtils.HtmlEncode('&<>"''') = '&amp;&lt;&gt;&quot;''',
    '四种实体及单引号固定预期');
  Check(TCrossHttpUtils.HtmlEncode('ab&'#0'"<>') = 'ab&amp;',
    '首个NUL后的码元不输出');
  CheckEncode(StringOfChar('x', 4096), '长普通文本');
  for I := Low(Escaped) to High(Escaped) do
    CheckEncode(StringOfChar('x', 4095) + Escaped[I], '长文本末尾转义 ' + IntToStr(I));

  LInput := StringOfChar('"', 4096);
  LExpected := '';
  for I := 1 to 4096 do LExpected := LExpected + '&quot;';
  LOutput := TCrossHttpUtils.HtmlEncode(LInput);
  Check(LOutput = LExpected, '全双引号达到最坏上界');
  Check(Length(LOutput) = 4096 * 6, '上界为每码元六字符');

  LInput := StringOfChar('x', 32) + '&';
  LOriginal := LInput;
  LExpected := StringOfChar('x', 32) + '&amp;';
  LOutput := TCrossHttpUtils.HtmlEncode(LInput);
  LOutput[1] := 'y';
  Check(LInput = LOriginal, '修改输出不污染输入');
  Check(LOutput = 'y' + Copy(LExpected, 2, MaxInt), '输出可独立修改');
  LOutput := TCrossHttpUtils.HtmlEncode(LInput);
  LInput[2] := 'z';
  Check(LOutput = LExpected, '修改输入不污染输出');
  Check(LOriginal = StringOfChar('x', 32) + '&', '共享输入保持写时复制');
  LInput := '';
  Check(LOutput = LExpected, '输入释放后输出保持');
  LOutput := TCrossHttpUtils.HtmlEncode('literal&');
  LOutput[1] := 'L';
  Check(TCrossHttpUtils.HtmlEncode('literal&') = 'literal&amp;', '修改结果不污染常量');

  // 预期先独立生成；不保留输入的额外引用，允许隐藏结果槽复用调用方变量。
  LOriginal := '';
  LOutput := '';
  LExpected := StringOfChar('x', 4096);
  LInput := StringOfChar('x', 4096);
  UniqueString(LInput);
  LInput := TCrossHttpUtils.HtmlEncode(LInput);
  Check(LInput = LExpected, '动态唯一普通文本自赋值');
  LExpected := StringOfChar('x', 4095) + '&quot;';
  LInput := StringOfChar('x', 4095) + '"';
  UniqueString(LInput);
  LInput := TCrossHttpUtils.HtmlEncode(LInput);
  Check(LInput = LExpected, '动态唯一长尾转义自赋值');
  LExpected := StringOfChar('x', 32) + '&amp;';
  LInput := StringOfChar('x', 32) + '&'#0'"<';
  UniqueString(LInput);
  LInput := TCrossHttpUtils.HtmlEncode(LInput);
  Check(LInput = LExpected, '动态唯一NUL输入自赋值');
  LExpected := '';
  for I := 1 to 4096 do LExpected := LExpected + '&quot;';
  LInput := StringOfChar('"', 4096);
  UniqueString(LInput);
  LInput := TCrossHttpUtils.HtmlEncode(LInput);
  Check(LInput = LExpected, '动态唯一全双引号自赋值');
  Result := LChecks;
end;
