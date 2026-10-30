// 走真实路由和自动Send选择，只检查线上编码声明及发送结果，不另建解压器。
function RunCompressionChoiceTests: Integer;
var
  LChecks: Integer;
  LCase: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('压缩选择 ' + LCase + ': ' + AMessage);
  end;

  procedure RunCase(const AContentType, AAccept, AExpected: string;
    const AEnabled: Boolean; const ASize, AMinSize: Integer; const AEncoded: Boolean);
  var
    F: TFixture;
    H: THttpHeader;
    LBody: TBytes;
    LCalls, LHeadEnd, LLineEnd: Integer;
    LSuccess: Boolean;
    LRequest, LHeaderText: string;
    LActualBody, LExpectedBody: AnsiString;
  begin
    LCase := AContentType + '/' + AAccept + '/' + IntToStr(Ord(AEnabled))
      + '/' + IntToStr(ASize) + '/' + IntToStr(AMinSize) + '/' + IntToStr(Ord(AEncoded));
    F := TFixture.Create;
    H := THttpHeader.Create;
    LCalls := 0;
    LSuccess := False;
    LBody := Bytes(StringOfChar('a', ASize));
    if AEncoded then
      // 固定的gzip("a"重复1024次)；验证已经编码的字节不会被再次压缩。
      LBody := TBytes.Create($1F, $8B, $08, $00, $00, $00, $00, $00, $02, $FF,
        $4B, $4C, $1C, $05, $A3, $60, $14, $8C, $54, $00, $00, $B9, $97,
        $55, $7C, $00, $04, $00, $00);
    try
      F.Server.Compressible := AEnabled;
      F.Server.MinCompressSize := AMinSize;
      F.Server.Get('/compression', TCrossHttpRouterProc(
        procedure(const R: ICrossHttpRequest; const S: ICrossHttpResponse;
          var Handled: Boolean)
        begin
          Handled := True;
          S.ContentType := AContentType;
          if AEncoded then S.Header['Content-Encoding'] := 'gzip';
          S.Send(LBody,
            procedure(const C: ICrossConnection; const OK: Boolean)
            begin Inc(LCalls); LSuccess := OK; end);
        end));
      LRequest := 'GET /compression HTTP/1.1'#13#10'Host: localhost'#13#10;
      if AAccept <> '' then LRequest := LRequest + 'Accept-Encoding: ' + AAccept + #13#10;
      F.ConnectionObj.Feed(Wire(LRequest + #13#10));
      Check((LCalls = 1) and LSuccess, '发送恰好一次成功回调');
      Check(Pos(AnsiString('HTTP/1.1 200 OK'#13#10), F.ConnectionObj.Sent) = 1, '成功状态行');
      LHeadEnd := Pos(AnsiString(#13#10#13#10), F.ConnectionObj.Sent);
      Check(LHeadEnd > 0, '响应头结束');
      // 只把ASCII头转换成string，不让压缩正文经过默认代码页转换。
      LHeaderText := string(Copy(F.ConnectionObj.Sent, 1, LHeadEnd + 3));
      LLineEnd := Pos(#13#10, LHeaderText);
      Check(H.Decode(Copy(LHeaderText, LLineEnd + 2, MaxInt)), '响应头可解析');
      Check(H['Content-Type'] = AContentType, '选择编码不改写原Content-Type');
      Check(H['Content-Encoding'] = AExpected, '编码选择与门槛规则');
      Check(F.ConnectionObj.Writes > 0, '实际输出响应');
      LActualBody := Copy(F.ConnectionObj.Sent, LHeadEnd + 4, MaxInt);
      if (AExpected = '') or AEncoded then
      begin
        SetString(LExpectedBody, PAnsiChar(Pointer(LBody)), Length(LBody));
        Check(LActualBody = LExpectedBody, '未压缩或已有编码的正文原样输出');
      end else
        Check(Length(LActualBody) > 0, '自动压缩实际输出正文');
    finally
      H.Free;
      F.Free;
    end;
  end;

  procedure CheckType(const AContentType: string);
  begin
    RunCase(AContentType, 'gzip', 'gzip', True, 1024, 512, False);
  end;

begin
  LChecks := 0;
  CheckType('text/plain');
  CheckType('TeXt/PlAiN; charset=utf-8');
  CheckType('application/json');
  CheckType('APPLICATION/JSON');
  CheckType('application/javascript');
  CheckType('Application/JavaScript');
  CheckType('application/xml');
  CheckType('APPLICATION/XML');
  RunCase('application/octet-stream', 'gzip', '', True, 1024, 512, False);
  RunCase('APPLICATION/JSON', 'gzip', '', False, 1024, 512, False);
  RunCase('APPLICATION/JSON', 'gzip', '', True, 511, 512, False);
  // 压缩开关和大小门槛都满足；已有Content-Encoding单独阻止二次压缩。
  RunCase('APPLICATION/JSON', 'gzip, deflate', 'gzip', True, 1024, 1, True);
  RunCase('APPLICATION/JSON', 'deflate, gzip', 'gzip', True, 1024, 512, False);
  RunCase('APPLICATION/JSON', 'gzip;q=0, deflate;q=1', 'deflate', True, 1024, 512, False);
  RunCase('APPLICATION/JSON', 'gzip;q=0, deflate;q=0', '', True, 1024, 512, False);
  // 整数q不依赖系统小数点；重复项仍取原来的最大权重。
  RunCase('APPLICATION/JSON', 'GZIP; Q=0, gzip;q=1, deflate;q=0', 'gzip', True, 1024, 512, False);
  Result := LChecks;
end;
