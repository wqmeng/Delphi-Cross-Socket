// 覆盖无 query、空 query 及原始字节分段；普通查询参数组合继续由 TestRequest 覆盖。
function RunRequestPathTests: Integer;
var
  LChecks, LRawCases, LOracleExceptions: Integer;
  LBomPath, LBomQuery, LOpaquePath, LOpaqueQuery: TBytes;

  procedure CheckPath(const ATarget, APath: string);
  var
    F: TFixture;
    B, LSnapshot: TBytes;
    LRawCopy, LPathCopy, LSavedRaw, LSavedPath: string;

    procedure Check(const ACondition: Boolean; const AMessage: string);
    begin
      Inc(LChecks);
      if not ACondition then
        raise Exception.Create('请求路径 ' + ATarget + ': ' + AMessage);
    end;

  begin
    F := TFixture.Create;
    try
      B := Bytes('GET ' + ATarget + ' HTTP/1.1'#13#10'Host: localhost'#13#10#13#10);
      LSnapshot := Copy(B);
      Check(TRequestAccess(F.Request as TCrossHttpRequest).ParseHeader(
        Pointer(B), Length(B)), '解析成功');
      Check(F.Request.RawPathAndParams = ATarget, '原始目标保留');
      Check(F.Request.Path = APath, '路径只做一次百分号解码');
      Check(F.Request.PathAndQuery = APath, '空查询不追加问号');
      Check(F.Request.QueryText = '', '查询文本为空');
      Check(F.Request.Query.Count = 0, '没有查询参数');

      // 正常字符串索引赋值必须写时复制，不以内部指针是否共享作为断言。
      LRawCopy := F.Request.RawPathAndParams;
      LRawCopy[1] := 'X';
      Check(LRawCopy = 'X' + Copy(ATarget, 2, MaxInt), '原始目标副本可修改');
      Check(F.Request.RawPathAndParams = ATarget, '副本修改不污染原始目标');
      Check(F.Request.Path = APath, '副本修改不污染已解码路径');
      LPathCopy := F.Request.Path;
      LPathCopy[1] := 'Y';
      Check(LPathCopy = 'Y' + Copy(APath, 2, MaxInt), '路径副本可修改');
      Check(F.Request.Path = APath, '路径副本修改不污染请求');
      Check(F.Request.PathAndQuery = APath, '路径副本修改不污染组合路径');
      Check(F.Request.RawPathAndParams = ATarget, '路径副本修改不污染原始目标');

      // 输入借用结束后，原始快照和路径字符串仍独立有效。
      FillChar(B[0], Length(B), $CC);
      Check(SameBytes(F.Request.RawRequestHeader, LSnapshot), '输入覆写不污染快照');
      Check(F.Request.RawPathAndParams = ATarget, '输入覆写不污染原始目标');
      Check(F.Request.Path = APath, '输入覆写不污染路径');
      LSavedRaw := F.Request.RawPathAndParams;
      LSavedPath := F.Request.Path;
    finally
      F.Free;
    end;
    Check(LSavedRaw = ATarget, '请求释放后原始目标副本仍有效');
    Check(LSavedPath = APath, '请求释放后路径副本仍有效');
  end;

  procedure AppendBytes(var ATarget: TBytes; const ASource: TBytes);
  var
    N: Integer;
  begin
    N := Length(ATarget);
    SetLength(ATarget, N + Length(ASource));
    if Length(ASource) > 0 then
      Move(ASource[0], ATarget[N], Length(ASource));
  end;

  procedure CheckRawTarget(const ALabel: string; const APath, AQuery: TBytes;
    const AHasQuery: Boolean);
  var
    F: TFixture;
    LTarget, LLine, B: TBytes;
    LRawTarget, LPath, LQueryText, LQueryName, LQueryValue, LCombined: string;
    LCommandLine, LError, LStage: string;
    I, LQueryCount: Integer;
    LParsed: Boolean;

    procedure Check(const ACondition: Boolean; const AMessage: string);
    begin
      Inc(LChecks);
      if not ACondition then
        raise Exception.Create('原始请求目标 ' + ALabel + ': ' + AMessage);
    end;

  begin
    Inc(LRawCases);
    LTarget := Copy(APath);
    if AHasQuery then
    begin
      AppendBytes(LTarget, TBytes.Create(Ord('?')));
      AppendBytes(LTarget, AQuery);
    end;
    LLine := Bytes('GET ');
    AppendBytes(LLine, LTarget);
    AppendBytes(LLine, Bytes(' HTTP/1.1'));
    B := Copy(LLine);
    AppendBytes(B, Bytes(#13#10'Host: localhost'#13#10#13#10));

    // 每个区间各自调用 GetString 作平台预期，绝不切分已转换的完整目标。
    // 首行也可能先遇到默认编码的转换异常；记录实际阶段，不能声称未触发的异常已覆盖。
    LError := '';
    LQueryText := '';
    LQueryCount := 0;
    try
      LStage := 'request-line';
      LCommandLine := TUtils.GetString(Pointer(LLine), Length(LLine));
      LStage := 'target';
      LRawTarget := TUtils.GetString(Pointer(LTarget), Length(LTarget));
      LStage := 'path';
      LPath := TUtils.GetString(Pointer(APath), Length(APath));
      if AHasQuery then
      begin
        LStage := 'query';
        LQueryText := TCrossHttpUtils.UrlDecode(
          TUtils.GetString(Pointer(AQuery), Length(AQuery)));
        if Length(AQuery) > 0 then
        begin
          // 本组只有一个 q=value 参数，等号边界仍来自原始字节。
          I := 0;
          while I < Length(AQuery) do
          begin
            if AQuery[I] = Ord('=') then Break;
            Inc(I);
          end;
          LStage := 'query-name';
          LQueryName := TCrossHttpUtils.UrlDecode(TUtils.GetString(Pointer(AQuery), I));
          LQueryValue := '';
          if I < Length(AQuery) then
          begin
            LStage := 'query-value';
            LQueryValue := TCrossHttpUtils.UrlDecode(TUtils.GetString(
              PByte(Pointer(AQuery)) + I + 1, Length(AQuery) - I - 1));
          end;
          LQueryCount := 1;
        end;
      end;
      LStage := 'path-url-decode';
      LPath := TCrossHttpUtils.UrlDecode(LPath);
      LCombined := LPath;
      if LQueryText <> '' then LCombined := LCombined + '?' + LQueryText;
    except
      on E: Exception do LError := E.ClassName;
    end;

    F := TFixture.Create;
    try
      LParsed := TRequestAccess(F.Request as TCrossHttpRequest).ParseHeader(
        Pointer(B), Length(B));
      if LError <> '' then
      begin
        Inc(LOracleExceptions);
        Writeln('request-path oracle exception: ', ALabel, '/', LStage, '/', LError);
        Check(not LParsed, '直接转换抛错时 ParseHeader 必须失败');
        Exit;
      end;
      Check(LParsed, '原始字节解析成功');
      Check(F.Request.RequestCmdLine = LCommandLine, '首行按自身字节区间转换');
      Check(F.Request.RawPathAndParams = LRawTarget, '完整目标遵循 GetString');
      Check(F.Request.Path = LPath, '路径遵循自身区间 GetString 后一次 UrlDecode');
      Check(F.Request.QueryText = LQueryText, '查询遵循自身字节区间');
      Check(F.Request.PathAndQuery = LCombined, '组合路径保持空查询规则');
      Check(F.Request.Query.Count = LQueryCount, '查询参数数量');
      if LQueryCount > 0 then
      begin
        Check(F.Request.Query.Items[0].Name = LQueryName, '参数名称按原始分界转换');
        Check(F.Request.Query.Items[0].Value = LQueryValue, '参数值按原始分界转换');
      end;
    finally
      F.Free;
    end;
  end;

begin
  LChecks := 0;
  LRawCases := 0;
  LOracleExceptions := 0;
  CheckPath('/中文/%252F', '/中文/%2F');
  CheckPath('/a%3Fb', '/a?b');
  CheckPath('/a?', '/a');
  LBomPath := Bytes(#$FEFF'/中文/%252F');
  LBomQuery := Bytes(#$FEFF'q=中文');
  CheckRawTarget('BOM/no-query', LBomPath, nil, False);
  CheckRawTarget('BOM/empty-query', LBomPath, nil, True);
  CheckRawTarget('BOM/query', LBomPath, LBomQuery, True);
  // FE 不是合法 UTF-8 单字节；保留各编译器/系统默认编码的回退或异常行为。
  LOpaquePath := Bytes('/opaque/');
  AppendBytes(LOpaquePath, TBytes.Create($FE));
  LOpaqueQuery := Bytes('q=');
  AppendBytes(LOpaqueQuery, TBytes.Create($FE));
  CheckRawTarget('opaque/no-query', LOpaquePath, nil, False);
  CheckRawTarget('opaque/empty-query', LOpaquePath, nil, True);
  CheckRawTarget('opaque/query', LOpaquePath, LBomQuery, True);
  // 完整目标需要回退，路径自身仍为合法 UTF-8，不能用完整转换结果的子串作路径。
  CheckRawTarget('UTF8-path/opaque-query', Bytes('/中文'), LOpaqueQuery, True);
  Writeln('request-path raw cases=', LRawCases, ', oracle-exceptions=', LOracleExceptions);
  Result := LChecks;
end;
