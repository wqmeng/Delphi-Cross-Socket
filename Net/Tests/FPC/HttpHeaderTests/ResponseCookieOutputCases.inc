// 走真实路由/响应队列/SendNoCompress，只由现有内存连接收集线上字节。
// Cookie和Header同时非法时只能从公开发送接口观察整块失败；精确异常先后另由源码审查核对。
function RunResponseCookieOutputTests: Integer;
const
  CookieCounts: array[0..3] of Integer = (0, 1, 4, 17);
var
  LChecks, I: Integer;
  LCase: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('响应Cookie ' + LCase + ': ' + AMessage);
  end;

  function CountCookieLines(const AData: AnsiString): Integer;
  var
    LRest: AnsiString;
    P: Integer;
  begin
    Result := 0;
    LRest := AData;
    repeat
      P := Pos(AnsiString('Set-Cookie: '), LRest);
      if P = 0 then Break;
      Inc(Result);
      Delete(LRest, 1, P + Length('Set-Cookie: ') - 1);
    until False;
  end;

  procedure RunCase(const ACookieCount, ABadKind: Integer);
  var
    F: TFixture;
    LCalls, LCookieCount: Integer;
    LSuccess: Boolean;
    LExpected: string;
    LHeld: ICrossHttpResponse;
  begin
    LCase := IntToStr(ACookieCount) + '/' + IntToStr(ABadKind);
    F := TFixture.Create;
    LCalls := 0;
    LSuccess := False;
    LCookieCount := 0;
    LExpected := 'HTTP/1.1 200 OK'#13#10;
    try
      F.Server.Get('/cookies', TCrossHttpRouterProc(
        procedure(const R: ICrossHttpRequest; const S: ICrossHttpResponse;
          var Handled: Boolean)
        var
          J: Integer;
          LName, LValue: string;
          LCookie: TResponseCookie;
        begin
          Handled := True;
          LHeld := S;
          // 集合外的手工Set-Cookie必须仍跟随普通Header输出，不受FCookies.Count分支影响。
          S.Header.Add('Set-Cookie', 'manual=last', True);
          S.Header.Add('X-After', 'done', True);
          for J := 0 to ACookieCount - 1 do
          begin
            LName := 'dup' + IntToStr(J mod 2);
            if J mod 3 = 0 then LValue := '' else LValue := 'v' + IntToStr(J);
            S.Cookies.Add(TResponseCookie.Create(LName, LValue, 60,
              '/scope', 'example.test', True, True));
            // 独立拼出已知属性和顺序，不调用Cookie.Encode作为预期生成器。
            LExpected := LExpected + 'Set-Cookie: ' + LName + '=' + LValue
              + '; Max-Age=60; Path=/scope; Domain=example.test; HttpOnly; Secure'#13#10;
          end;
          case ABadKind of
            1: S.Cookies.Add(TResponseCookie.Create('bad name', 'ok', 0));
            2: S.Cookies.Add(TResponseCookie.Create('bad', #13#10, 0));
            3: S.Cookies.Add(TResponseCookie.Create('bad', 'ok', 0, '/bad;path'));
            4: begin
                 // 构造器会规范化Domain；直接修改公开记录字段才能测试发送端拒绝非法Domain。
                 LCookie := TResponseCookie.Create('bad', 'ok', 0, '/');
                 LCookie.Domain := 'bad'#0'domain';
                 S.Cookies.Add(LCookie);
               end;
            5: S.Header.Add('Bad Name', 'bad', True);
            6: begin
                 S.Cookies.Add(TResponseCookie.Create('bad', #13, 0));
                 S.Header.Add('Bad Name', #0, True);
               end;
          end;
          LCookieCount := S.Cookies.Count;
          S.SendNoCompress('body',
            procedure(const C: ICrossConnection; const OK: Boolean)
            begin
              Inc(LCalls);
              LSuccess := OK;
            end);
        end));
      F.ConnectionObj.Feed('GET /cookies HTTP/1.1'#13#10'Host: localhost'#13#10#13#10);
      Check(LCalls = 1, '发送回调恰好一次');
      Check(LSuccess = (ABadKind = 0), '合法输出成功、非法组合失败');
      Check(LHeld <> nil, '路由响应仍可核对');
      Check(LHeld.Cookies.Count = LCookieCount, '序列化不删除或合并Cookie项');
      if ABadKind = 0 then
      begin
        LExpected := LExpected + 'Set-Cookie: manual=last'#13#10'X-After: done'#13#10;
        Check(Pos(Wire(LExpected), F.ConnectionObj.Sent) = 1,
          '状态行、独立Cookie、手工Cookie、后续字段的精确顺序');
        Check(CountCookieLines(F.ConnectionObj.Sent) = ACookieCount + 1,
          '重复名称逐条输出且无额外Cookie');
        Check(Copy(F.ConnectionObj.Sent, Length(F.ConnectionObj.Sent) - 7, 8)
          = AnsiString(#13#10#13#10'body'), '最终空行和正文保持');
        Check(F.ConnectionObj.Writes > 0, '成功响应实际发送');
      end else
      begin
        Check(F.ConnectionObj.Writes = 0, '整块校验失败前不发送前面的合法Cookie');
        Check(F.ConnectionObj.Sent = '', '非法Cookie或Header不泄漏部分报文');
      end;
      LHeld := nil;
    finally
      LHeld := nil;
      F.Free;
    end;
  end;

begin
  LChecks := 0;
  for I := Low(CookieCounts) to High(CookieCounts) do
    RunCase(CookieCounts[I], 0);
  for I := 1 to 6 do
    RunCase(2, I);
  Result := LChecks;
end;
