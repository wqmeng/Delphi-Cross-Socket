// 通过编译副本中的薄包装调用真实私有方法，旧格式化表达式只作独立预期。
function RunDockKeyChecks(const ASocket: TCrossHttpClientSocket): Integer;
type
  TKeyCase = record
    Protocol, Host: string;
  end;
const
  Cases: array[0..9] of TKeyCase = (
    (Protocol: 'http'; Host: 'example.test'),
    (Protocol: 'HTTPS'; Host: 'MiXeD.Example'),
    (Protocol: ''; Host: ''),
    (Protocol: 'http'; Host: ''),
    (Protocol: ''; Host: 'host'),
    (Protocol: 'http'; Host: '::1'),
    (Protocol: 'http'; Host: '[fe80::1%eth0]'),
    (Protocol: '协议'; Host: '例子.测试'#$D83D#$DE00),
    (Protocol: 'h'#0'ttp'; Host: 'ho'#0'st'),
    (Protocol: 'x'#$D800'y'; Host: 'z'#$DC00'end'));
  Ports: array[0..5] of Word = (0, 1, 80, 443, 32768, 65535);
var
  I, J, LChecks: Integer;
  LProtocol, LHost, LExpected, LActual, LCopy: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('连接池键: ' + AMessage);
  end;

  function ReferenceKey(const AProtocol, AHost: string; const APort: Word): string;
  begin
    // 独立栈帧让格式化参数临时量在返回时释放，不影响后面的唯一输入别名用例。
    Result := TStrUtils.Format('%s://%s:%d', [AProtocol, AHost, APort]);
  end;

begin
  LChecks := 0;
  for I := Low(Cases) to High(Cases) do
    for J := Low(Ports) to High(Ports) do
    begin
      LExpected := ReferenceKey(Cases[I].Protocol, Cases[I].Host, Ports[J]);
      LActual := HttpHeaderBenchDockKey(ASocket, Cases[I].Protocol, Cases[I].Host, Ports[J]);
      Check(Length(LActual) = Length(LExpected), '完整长度 case=' + IntToStr(I) + ',port=' + IntToStr(Ports[J]));
      Check(LActual = LExpected, '完整内容 case=' + IntToStr(I) + ',port=' + IntToStr(Ports[J]));
    end;
  Check(HttpHeaderBenchDockKey(ASocket, 'HTTP', '[::1]', 0) = 'HTTP://[::1]:0',
    '保留大小写和已有IPv6括号');
  Check(HttpHeaderBenchDockKey(ASocket, 'http', '::1', 65535) = 'http://::1:65535',
    '不为原始IPv6补括号或省略端口');

  LProtocol := StringOfChar('p', 17);
  LHost := StringOfChar('h', 257) + '.test';
  LExpected := ReferenceKey(LProtocol, LHost, 443);
  LActual := HttpHeaderBenchDockKey(ASocket, LProtocol, LHost, 443);
  LCopy := LActual;
  LCopy[1] := #1;
  Check(LActual = LExpected, '修改输出副本不改变原结果');
  Check((LProtocol = StringOfChar('p', 17)) and (LHost = StringOfChar('h', 257) + '.test'),
    '输出不修改输入');
  LProtocol[1] := #2;
  LHost[1] := #3;
  Check(LActual = LExpected, '修改输入不改变既有输出');

  // 动态创建输入，不另存输入别名；Expected是完整新键，不与输入共享字符串缓冲区。
  LProtocol := StringOfChar('p', 23);
  LHost := StringOfChar('h', 1025) + '.test';
  LExpected := ReferenceKey(LProtocol, LHost, 80);
  LHost := HttpHeaderBenchDockKey(ASocket, LProtocol, LHost, 80);
  Check(Length(LHost) = Length(LExpected), 'Host自赋值长度');
  Check(LHost = LExpected, 'Host自赋值完整内容');
  Check(LProtocol = StringOfChar('p', 23), 'Host自赋值不改变协议');

  LProtocol := StringOfChar('s', 33);
  LHost := StringOfChar('h', 513) + '.test';
  LExpected := ReferenceKey(LProtocol, LHost, 65535);
  LProtocol := HttpHeaderBenchDockKey(ASocket, LProtocol, LHost, 65535);
  Check(Length(LProtocol) = Length(LExpected), 'Protocol自赋值长度');
  Check(LProtocol = LExpected, 'Protocol自赋值完整内容');
  Check(LHost = StringOfChar('h', 513) + '.test', 'Protocol自赋值不改变主机');
  Result := LChecks;
end;
