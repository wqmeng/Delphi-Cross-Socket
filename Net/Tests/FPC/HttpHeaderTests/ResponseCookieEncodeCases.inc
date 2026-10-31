// 固定预期验证响应Cookie输出；不复用生产拼接和私有字符校验。
function RunResponseCookieEncodeTests: Integer;
const
  MaxAges: array[0..5] of Integer = (-1, 0, 1, 9, 10, High(Integer));
  MaxAgeOutputs: array[0..5] of string = (
    'session=abc', 'session=abc', 'session=abc; Max-Age=1',
    'session=abc; Max-Age=9', 'session=abc; Max-Age=10',
    'session=abc; Max-Age=2147483647');
var
  C, LOther: TResponseCookie;
  I, LChecks: Integer;
  LOutput, LCopy, LExpected: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('响应Cookie编码: ' + AMessage);
  end;

  procedure CheckError(const AExpected: string);
  var
    LClass, LMessage, LUnused: string;
  begin
    LClass := '';
    LMessage := '';
    try
      LUnused := C.Encode;
    except
      on Error: Exception do
      begin
        LClass := Error.ClassName;
        LMessage := Error.Message;
      end;
    end;
    // 非法字段及预期均为ASCII，避免异常消息的编码转换影响比较。
    Check(LClass = 'Exception', '错误类别');
    Check(LMessage = AExpected, '错误消息及顺序');
  end;

  function ExpectedAliasCookie: string;
  begin
    // 先独立生成完整预期，不保留被测记录Name或Value的额外引用。
    Result := StringOfChar('n', 12) + '=' + StringOfChar('v', 24)
      + '; Max-Age=10; Path=/scope; Domain=example.test; HttpOnly; Secure';
  end;

begin
  LChecks := 0;
  C := TResponseCookie.Create('session', 'abc', 0);
  Check(C.Encode = 'session=abc', '无属性');
  C.Value := '';
  Check(C.Encode = 'session=', '空值保留等号');

  C := TResponseCookie.Create('session', 'abc', 60);
  Check(C.Encode = 'session=abc; Max-Age=60', '单独Max-Age');
  C := TResponseCookie.Create('session', 'abc', 0, '/scope');
  Check(C.Encode = 'session=abc; Path=/scope', '单独Path');
  C := TResponseCookie.Create('session', 'abc', 0, '', 'example.test');
  Check(C.Encode = 'session=abc; Domain=example.test', '单独Domain');
  C := TResponseCookie.Create('session', 'abc', 0, '', '', True, False);
  Check(C.Encode = 'session=abc; HttpOnly', '单独HttpOnly');
  C := TResponseCookie.Create('session', 'abc', 0, '', '', False, True);
  Check(C.Encode = 'session=abc; Secure', '单独Secure');
  C := TResponseCookie.Create('session', 'abc', 60,
    '/scope', 'example.test', True, True);
  Check(C.Encode = 'session=abc; Max-Age=60; Path=/scope; Domain=example.test; HttpOnly; Secure',
    '全部属性顺序及分隔符');

  C := TResponseCookie.Create('session', 'abc', 0);
  for I := Low(MaxAges) to High(MaxAges) do
  begin
    C.MaxAge := MaxAges[I];
    Check(C.Encode = MaxAgeOutputs[I], 'Max-Age边界' + IntToStr(I));
  end;
  C := TResponseCookie.Create('long', StringOfChar('v', 65536), 0);
  Check(C.Encode = 'long=' + StringOfChar('v', 65536), '长Value完整输出');

  C := TResponseCookie.Create(StringOfChar('n', 12), StringOfChar('v', 24), 0);
  LOutput := C.Encode;
  LCopy := LOutput;
  LCopy[1] := 'x';
  Check(LOutput = StringOfChar('n', 12) + '=' + StringOfChar('v', 24),
    '输出副本写时复制');
  Check(C.Encode = LOutput, '修改输出副本不污染记录');
  LOther := C;
  LOther.Name[1] := 'y';
  LOther.Value[1] := 'z';
  Check(C.Encode = LOutput, '记录副本写时复制');
  C.Value[1] := 'q';
  Check(LOutput = StringOfChar('n', 12) + '=' + StringOfChar('v', 24),
    '修改来源不污染已返回输出');

  LExpected := ExpectedAliasCookie;
  C := TResponseCookie.Create(StringOfChar('n', 12), StringOfChar('v', 24),
    10, '/scope', 'example.test', True, True);
  UniqueString(C.Name);
  UniqueString(C.Value);
  C.Name := C.Encode;
  Check(C.Name = LExpected, '动态唯一Name接收编码结果');
  Check(C.Value = StringOfChar('v', 24), 'Name自赋值保留Value');

  LExpected := ExpectedAliasCookie;
  C := TResponseCookie.Create(StringOfChar('n', 12), StringOfChar('v', 24),
    10, '/scope', 'example.test', True, True);
  UniqueString(C.Name);
  UniqueString(C.Value);
  C.Value := C.Encode;
  Check(C.Value = LExpected, '动态唯一Value接收编码结果');
  Check(C.Name = StringOfChar('n', 12), 'Value自赋值保留Name');

  // 逐项修正多个同时无效的字段，保留Name、Value、Path、Domain的异常先后。
  C := TResponseCookie.Create('bad name', 'bad value', 0, 'bad;path');
  C.Domain := 'bad;domain';
  CheckError('Invalid cookie name: bad name');
  C.Name := 'name';
  CheckError('Invalid cookie value: bad value');
  C.Value := 'good';
  CheckError('Invalid cookie path: name');
  // Path字符和首字符检查共用相同错误文本，分别覆盖两种失败条件。
  C.Path := 'badpath';
  CheckError('Invalid cookie path: name');
  C.Path := '/good';
  CheckError('Invalid cookie domain: name');
  C.Name := '';
  CheckError('Invalid cookie name: ');

  Result := LChecks;
end;
