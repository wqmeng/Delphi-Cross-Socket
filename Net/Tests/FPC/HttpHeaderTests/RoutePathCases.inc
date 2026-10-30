// 用原ParsePath调用通用Split作为独立对照，避免测试重复候选的计数/填充算法。
function RunRoutePathTests: Integer;
const
  Cases: array[0..27] of string = (
    '', '/', '//', '/////', 'a', '/a', 'a/', '/a/', '//a///b//',
    'a/b/c', 'a//b', '\', 'a\b/c', 'a?x=1/b#part', '%2F/%00',
    #0, '/'#0'/', 'a'#0'/b', '/a/'#0'b', '中文/文档',
    'é/É', #$FEFF'/a/'#$FEFF, #$D83D#$DE00'/x', #$D800,
    #$DC00'/'#$D800, 'a/'#$D800'/b', ' '#9'/'#10#13, './..');
  Alphabet: array[0..4] of Char = ('/', 'a', #0, #$4E2D, #$D800);
var
  I, J, LLength, LNumber, LValue, LLimit, LChecks: Integer;
  LInput, LOriginal, LSaved: string;
  LParts: TArray<string>;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('路由路径拆分: ' + AMessage);
  end;

  function ReferenceParsePath(const APath: string): TArray<string>;
  begin
    if (APath = '/') or (APath = '') then
    begin
      Result := [''];
      Exit;
    end;
    Result := APath.Split(['/'], TStringSplitOptions.ExcludeEmpty);
    if Result = nil then Result := [''];
  end;

  procedure CheckPath(const APath, ALabel: string);
  var
    K: Integer;
    LActual, LExpected: TArray<string>;
  begin
    LExpected := ReferenceParsePath(APath);
    LActual := TCrossHttpRouterTree.ParsePath(APath);
    Check(Length(LActual) = Length(LExpected), ALabel + '片段数量');
    for K := 0 to High(LExpected) do
      Check(LActual[K] = LExpected[K], ALabel + '片段 ' + IntToStr(K));
  end;

begin
  LChecks := 0;
  for I := Low(Cases) to High(Cases) do
    CheckPath(Cases[I], '固定用例 ' + IntToStr(I) + ' ');

  // 固定预期单独保护全空路径及连续分隔符规则。
  LParts := TCrossHttpRouterTree.ParsePath('////');
  Check(Length(LParts) = 1, '全斜杠保留一个根片段');
  Check(LParts[0] = '', '根片段为空串');
  LParts := TCrossHttpRouterTree.ParsePath('//a///b//');
  Check(Length(LParts) = 2, '连续及首尾斜杠不产生空片段');
  Check((LParts[0] = 'a') and (LParts[1] = 'b'), '连续分隔符固定内容');
  LParts := TCrossHttpRouterTree.ParsePath('a'#0'b/c');
  Check(Length(LParts) = 2, '内嵌NUL不截断路径');
  Check((LParts[0] = 'a'#0'b') and (LParts[1] = 'c'), 'NUL属于片段内容');

  // 穷举0至5个码元，覆盖分隔符相邻、NUL及不成对代理项的各种位置。
  LLimit := 1;
  for LLength := 0 to 5 do
  begin
    for LNumber := 0 to LLimit - 1 do
    begin
      SetLength(LInput, LLength);
      LValue := LNumber;
      for J := 1 to LLength do
      begin
        LInput[J] := Alphabet[LValue mod Length(Alphabet)];
        LValue := LValue div Length(Alphabet);
      end;
      CheckPath(LInput, '穷举长度 ' + IntToStr(LLength)
        + ' 序号 ' + IntToStr(LNumber) + ' ');
    end;
    LLimit := LLimit * Length(Alphabet);
  end;

  CheckPath(StringOfChar('x', 8192), '长单片段 ');
  CheckPath('/' + StringOfChar('x', 8192) + '//尾部/', '长片段带首尾斜杠 ');
  LInput := '';
  for I := 1 to 128 do LInput := LInput + '/片段' + IntToStr(I);
  CheckPath(LInput, '多片段 ');
  CheckPath(StringOfChar('/', 8192), '长全斜杠 ');

  // 单片段可能复用输入字符串；只验证写时复制合同，不要求具体指针身份。
  LInput := StringOfChar('a', 32);
  LOriginal := LInput;
  LParts := TCrossHttpRouterTree.ParsePath(LInput);
  LSaved := LParts[0];
  LParts[0][1] := 'b';
  Check(LInput = LOriginal, '修改返回片段不污染输入');
  Check(LSaved = LOriginal, '修改返回片段不污染已有字符串副本');
  LInput[2] := 'c';
  Check(LParts[0] = 'b' + StringOfChar('a', 31), '修改输入不污染返回片段');
  Check(LOriginal = StringOfChar('a', 32), '输入副本保持独立');
  LInput := '';
  Check(LParts[0] = 'b' + StringOfChar('a', 31), '输入释放后返回片段仍有效');

  // 输入来自即将被覆盖的结果数组，检查托管参数/结果槽别名。
  LParts := ['alpha/beta'];
  LParts := TCrossHttpRouterTree.ParsePath(LParts[0]);
  Check(Length(LParts) = 2, '结果数组元素作为下一次输入的数量');
  Check((LParts[0] = 'alpha') and (LParts[1] = 'beta'), '结果数组元素别名内容');
  LParts := TCrossHttpRouterTree.ParsePath('/literal/文档');
  LParts[0][1] := 'L';
  LParts := TCrossHttpRouterTree.ParsePath('/literal/文档');
  Check(LParts[0] = 'literal', '修改返回结果不污染字面量或后续调用');
  Result := LChecks;
end;
