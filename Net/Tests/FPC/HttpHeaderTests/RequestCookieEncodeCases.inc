// 固定预期验证Cookie输出，不复用生产拼接或字符校验函数。
function RunRequestCookieEncodeTests: Integer;
const
  BadNames: array[0..7] of string = (
    '', 'bad name', 'bad=name', 'bad;name', #0, #9, '中文', #$D800);
  BadValues: array[0..11] of string = (
    ' ', #9, #0, #13#10, '"', ',', ';', '\', #127, #128, '中文', #$DC00);
  TokenName = '!#$%&''*+-.^_`|~AZaz09';
  CookieValue = '!#+-:<[]>~AZaz09';
var
  C: TRequestCookies;
  I, LChecks: Integer;
  LName, LValue, LOutput, LCopy: string;
  B, LRaw: TBytes;
  Entry: TParamEntry;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('请求Cookie输出: ' + AMessage);
  end;

  procedure CheckError(const AExpected: string);
  var
    LClass, LMessage, LUnused, LExpectedMessage: string;
    LExpectedError: Exception;
    LCount: Integer;
  begin
    // FPC的异常消息会转换字符编码；按异常类自身的转换比较孤立代理等输入。
    // 固定错误文本和字段顺序仍由用例给出，不调用被测Cookie校验生成预期。
    LExpectedError := Exception.Create(AExpected);
    try
      LExpectedMessage := LExpectedError.Message;
    finally
      LExpectedError.Free;
    end;
    LClass := '';
    LMessage := '';
    LCount := C.Count;
    try
      LUnused := C.Encode;
    except
      on Error: Exception do
      begin
        LClass := Error.ClassName;
        LMessage := Error.Message;
      end;
    end;
    Check(LClass = 'Exception', '错误类别');
    Check(LMessage = LExpectedMessage, '错误消息及顺序');
    Check(C.Count = LCount, '校验失败不改变集合数量');
    // 不检查异常后隐藏Result中的部分输出，调用方不依赖未完成的编码结果。
  end;

begin
  LChecks := 0;
  C := TRequestCookies.Create;
  try
    Check(C.Encode = '', '空集合');
    C.Add('a', '1', True);
    Check(C.Encode = 'a=1', '单项无分隔符');
    C.Clear;
    C.Add('a', '', True);
    Check(C.Encode = 'a=', '空值仍有等号');
    C.Add('a', '2', True);
    C.Add('A', '3', True);
    Check(C.Encode = 'a=; a=2; A=3', '重复项和大小写保持原顺序');
    C.Add('tail', '', True);
    Check(C.Encode = 'a=; a=2; A=3; tail=', '末项空值无尾分隔符');

    C.Clear;
    C.Add(TokenName, CookieValue, True);
    Check(C.Encode = TokenName + '=' + CookieValue, '合法token及Cookie字符边界');
    C.Clear;
    C.Add('literal', '%41+a=b', True);
    Check(C.Encode = 'literal=%41+a=b', 'Cookie值不进行URL编码或解码');
    C.Add('long', StringOfChar('x', 2048), True);
    Check(C.Encode = 'literal=%41+a=b; long=' + StringOfChar('x', 2048), '长值完整追加');

    for I := Low(BadNames) to High(BadNames) do
    begin
      C.Clear;
      // 名称和值都不合法时，仍首先报告名称错误。
      C.Add(BadNames[I], 'bad value', True);
      CheckError('Invalid cookie name: ' + BadNames[I]);
    end;
    for I := Low(BadValues) to High(BadValues) do
    begin
      C.Clear;
      C.Add('value', BadValues[I], True);
      CheckError('Invalid cookie value: value');
    end;

    C.Clear;
    C.Add('first', 'bad value', True);
    C.Add('Bad Name', 'x', True);
    CheckError('Invalid cookie value: first');
    Check(C.Items[0].Value = 'bad value', '后项错误不提前覆盖前项值错误');
    C.Clear;
    C.Add('first', 'ok', True);
    C.Add('Bad Name', 'bad value', True);
    C.Add('third', 'bad value', True);
    CheckError('Invalid cookie name: Bad Name');
    Check(C.Items[0].Value = 'ok', '后项校验失败不修改已读首项');

    C.Clear;
    B := Bytes('old-value');
    Entry := TParamEntry.Create('raw', Pointer(B), Length(B));
    LRaw := Entry.RawValue;
    Entry.Value := 'current-value';
    C.Add(Entry);
    Check(C.Encode = 'raw=current-value', '字节来源仍按当前Value输出');
    Entry := C.Items[0];
    Check(Entry.HasRawValue, '编码不清除raw来源标记');
    Check(SameBytes(Entry.RawValue, LRaw), '编码不改写raw内容');
    Check(Pointer(Entry.RawValue) = Pointer(LRaw), '编码不替换raw数组');

    C.Clear;
    LName := StringOfChar('n', 12);
    LValue := StringOfChar('v', 24);
    C.Add(LName, LValue, True);
    LOutput := C.Encode;
    LCopy := LOutput;
    LCopy[1] := 'x';
    Check(LOutput = LName + '=' + LValue, '输出副本写时复制');
    Check(C.Encode = LOutput, '修改输出副本不污染再次编码');
    LName[1] := 'x';
    LValue[1] := 'y';
    Check(C.Encode = LOutput, '修改调用者输入不污染集合');
    C.Clear;
    Check(LOutput = StringOfChar('n', 12) + '=' + StringOfChar('v', 24),
      '集合清空后输出仍有效');
  finally
    C.Free;
  end;
  Result := LChecks;
end;
