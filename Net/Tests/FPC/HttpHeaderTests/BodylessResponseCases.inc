// 通过真实请求路由和响应队列检查无正文响应，不访问_CreateHeader私有方法。
function RunBodylessResponseTests: Integer;
var
  LChecks: Integer;
  LCase: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('无正文响应 ' + LCase + ': ' + AMessage);
  end;

  procedure RunCase(const AMethod: string; const AStatus, ALengthKind: Integer;
    const AUnknownLength: Boolean; const AExpectedLength: string);
  var
    F: TFixture;
    H: THttpHeader;
    LCalls, LProviderCalls, LHeadEnd, LLineEnd: Integer;
    LSuccess: Boolean;
    LOutput, LFields: string;
    LValues: TArray<string>;
    LCallback: TCrossConnectionCallback;
    // Server/Client都声明了同名匿名方法类型，这里明确使用响应端类型。
    LSource: Net.CrossHttpServer.TCrossHttpChunkDataFunc;
  begin
    LCase := AMethod + '/' + IntToStr(AStatus) + '/' + IntToStr(ALengthKind)
      + '/' + IntToStr(Ord(AUnknownLength));
    F := TFixture.Create;
    H := THttpHeader.Create;
    LCalls := 0;
    LProviderCalls := 0;
    LSuccess := False;
    try
      LCallback := procedure(const C: ICrossConnection; const OK: Boolean)
        begin Inc(LCalls); LSuccess := OK; end;
      LSource := function(const P: PPointer; const N: PNativeInt): Boolean
        begin Inc(LProviderCalls); P^ := nil; N^ := 0; Result := False; end;
      F.Server.All('/bodyless', TCrossHttpRouterProc(
        procedure(const R: ICrossHttpRequest; const S: ICrossHttpResponse;
          var Handled: Boolean)
        begin
          Handled := True;
          S.StatusCode := AStatus;
          S.Header.Add('X-Before', 'one', True);
          case ALengthKind of
            1: begin
                 S.Header.Add('Content-Length', '9', True);
                 S.Header.Add('content-length', '9', True);
               end;
            2: begin
                 S.Header.Add('Content-Length', '9', True);
                 S.Header.Add('content-length', '8', True);
               end;
            3: S.Header.Add('Content-Length', 'bad', True);
            4: S.Header.Add('Content-Length', '', True);
            5: S.Header.Add('Content-Length', '+9', True);
            6: begin
                 S.Header.Add('Content-Length', '09', True);
                 S.Header.Add('content-length', '9', True);
               end;
          end;
          S.Header.Add('Transfer-Encoding', 'chunked', True);
          S.Header.Add('transfer-encoding', 'identity', True);
          S.Header.Add('X-After', 'two', True);
          if AUnknownLength then
            S.SendNoCompress(LSource, LCallback)
          else
            S.SendNoCompress('body', LCallback);
        end));
      // 此处CONNECT仅覆盖现有发送策略，并不建立真实代理隧道。
      F.ConnectionObj.Feed(Wire(AMethod + ' /bodyless HTTP/1.1'#13#10
        + 'Host: localhost'#13#10#13#10));
      Check((LCalls = 1) and LSuccess, '响应恰好一次成功回调');
      Check(LProviderCalls = 0, '无正文响应不读取未知长度provider');
      LOutput := string(F.ConnectionObj.Sent); // 本组所有线上字符均为ASCII。
      Check(Pos('HTTP/1.1 ' + IntToStr(AStatus) + ' ', LOutput) = 1, '状态码保持');
      LHeadEnd := Pos(#13#10#13#10, LOutput);
      LLineEnd := Pos(#13#10, LOutput);
      Check((LLineEnd > 0) and (LHeadEnd > LLineEnd), '完整响应头');
      Check(Length(LOutput) = LHeadEnd + 3, '末尾空行之后没有正文或chunk终止块');
      LFields := Copy(LOutput, LLineEnd + 2, LHeadEnd - LLineEnd + 2);
      Check(H.Decode(LFields), '响应字段可解析');
      Check(not H.GetHeaderValues('Transfer-Encoding', LValues), '删除所有TE副本');
      if AExpectedLength = '' then
        Check(not H.GetHeaderValues('Content-Length', LValues), '不发送不允许或非法的CL')
      else
      begin
        Check(H.GetHeaderValues('Content-Length', LValues), '存在需要保留的CL');
        Check(Length(LValues) = 1, 'CL副本合并为一项');
        Check(LValues[0] = AExpectedLength, 'CL值正确');
      end;
      Check((H['X-Before'] = 'one') and (H['X-After'] = 'two')
        and (Pos('X-Before: one', LOutput) < Pos('X-After: two', LOutput)),
        '非控制字段内容和相对顺序保持');
      if AExpectedLength <> '' then
        Check(Pos('X-After: two', LOutput) < Pos('Content-Length: ', LOutput),
          'CL删除后按原顺序重建在已有业务字段后');
    finally
      LSource := nil;
      LCallback := nil;
      H.Free;
      F.Free;
    end;
  end;

begin
  LChecks := 0;
  // HEAD的已知表示长度覆盖冲突元数据；未知长度只接受非空、全数字、完全相同的副本。
  RunCase('HEAD', 200, 2, False, '4');
  RunCase('HEAD', 200, 1, True, '9');
  RunCase('HEAD', 200, 0, True, '');
  RunCase('HEAD', 200, 2, True, '');
  RunCase('HEAD', 200, 3, True, '');
  RunCase('HEAD', 200, 4, True, '');
  RunCase('HEAD', 200, 5, True, '');
  RunCase('HEAD', 200, 6, True, '');
  // 304不借用传入正文的实际长度；合法表示元数据可保留，非法/冲突则全部去除。
  RunCase('GET', 304, 1, False, '9');
  RunCase('GET', 304, 2, True, '');
  RunCase('GET', 304, 3, False, '');
  RunCase('HEAD', 304, 1, False, '9');
  RunCase('GET', 205, 2, False, '0');
  RunCase('HEAD', 205, 1, True, '0');
  // 禁止长度字段的状态/CONNECT优先于HEAD表示长度和205的零长度规则。
  RunCase('GET', 100, 1, False, '');
  RunCase('HEAD', 103, 1, False, '');
  RunCase('GET', 204, 1, False, '');
  RunCase('HEAD', 204, 1, False, '');
  RunCase('CONNECT', 200, 1, False, '');
  RunCase('CONNECT', 205, 1, True, '');
  Result := LChecks;
end;
