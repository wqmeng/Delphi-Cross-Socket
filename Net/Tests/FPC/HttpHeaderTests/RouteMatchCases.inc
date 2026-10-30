// 使用公开路由树匹配真实请求对象，不执行回调，也不依赖节点内部布局。
function RunRouteMatchTests: Integer;
var
  F: TFixture;
  Tree: TCrossHttpRouterTree;
  LStatic, LRegex1, LRegex2, LWildcard: IRouter;
  LChecks: Integer;
  LCase, LUpperSegment, LLowerSegment: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('路由匹配 ' + LCase + ': ' + AMessage);
  end;

  function AddRoute(const AMethod, APath: string): IRouter;
  begin
    Result := TRouter.Create(AMethod);
    Tree.AddRouter(AMethod, APath, Result);
  end;

  procedure Match(const AMethod, ATarget: string; const AExpected: IRouter);
  var
    B: TBytes;
    LFound: IRouter;
    LMatched: Boolean;
  begin
    LCase := AMethod + ' ' + ATarget;
    // 同一个测试请求对象重复使用；清理上次捕获，不人为改变树匹配的写入规则。
    F.Request.Params.Clear;
    B := Bytes(AMethod + ' ' + ATarget + ' HTTP/1.1'#13#10
      + 'Host: localhost'#13#10#13#10);
    Check(TRequestAccess(F.Request as TCrossHttpRequest).ParseHeader(
      Pointer(B), Length(B)), '请求解析成功');
    LFound := nil;
    LMatched := Tree.MatchRouter(F.Request, LFound);
    Check(LMatched = (AExpected <> nil), '命中或未命中结果');
    if AExpected <> nil then
      Check(LFound = AExpected, '路由身份及优先级');
  end;

begin
  LChecks := 0;
  F := TFixture.Create;
  Tree := nil;
  try
    Tree := TCrossHttpRouterTree.Create;
    // 先注册低优先级项，证明结果由静态/正则/通配符规则决定。
    LWildcard := AddRoute('GET', '/items/*');
    LRegex1 := AddRoute('GET', '/items/:id([0-9]+)');
    LRegex2 := AddRoute('GET', '/items/:word([a-z]+)');
    LStatic := AddRoute('GET', '/items/42');
    Match('GET', '/items/42', LStatic);
    Check(F.Request.Params.Count = 0, '静态命中不执行后面的正则捕获');
    Match('GET', '/items/73', LRegex1);
    Check(F.Request.Params['id'] = '73', '数字路径参数保留');
    Match('GET', '/items/letters', LRegex2);
    Check(F.Request.Params['word'] = 'letters', '首个正则不匹配后继续检查');
    Match('GET', '/items/a/b?mode=x%2By', LWildcard);
    Check(F.Request.Params['*'] = 'a/b?mode=x+y', '通配符保留剩余路径及原有解码后query');
    Match('GET', '/items?empty=', LWildcard);
    Check(F.Request.Params['*'] = '?empty=', '路径已结束时通配符子节点仍匹配');
    Match('POST', '/items/73', nil);

    Tree.Clear;
    // 路径相同，方法依次检查静态、正则、通配符；正则失败后继续下一项。
    LWildcard := AddRoute('*', '/method');
    LRegex1 := AddRoute('(GET|POST)', '/method');
    LRegex2 := AddRoute('(GET|PUT)', '/method');
    LStatic := AddRoute('GET', '/method');
    Match('GET', '/method', LStatic);
    Match('POST', '/method', LRegex1);
    Match('PUT', '/method', LRegex2);
    Match('DELETE', '/method', LWildcard);
    Tree.RemoveRouter('*', '/method');
    Match('DELETE', '/method', nil);
    // 静态方法表为空时，正则也未命中，仍应返回False。
    Tree.RemoveRouter('GET', '/method');
    Match('DELETE', '/method', nil);

    Tree.Clear;
    // 两个路径正则同时可匹配时，仍取先注册的一项。
    LRegex1 := AddRoute('GET', '/order/:first([a-z]+)');
    LRegex2 := AddRoute('GET', '/order/:second(.+)');
    Match('GET', '/order/alpha', LRegex1);
    Check((F.Request.Params['first'] = 'alpha')
      and not F.Request.Params.ExistsParam('second'), '首个匹配正则获胜且后项未执行');
    Match('GET', '/order/123', LRegex2);
    Check((F.Request.Params['second'] = '123')
      and not F.Request.Params.ExistsParam('first'), '不匹配正则不写入参数');

    Tree.Clear;
    LStatic := AddRoute('GET', '/文档/Étage');
    Match('GET', '/文档/Étage', LStatic);
    // 原实现使用本平台的ToLower；FPC与Delphi对非ASCII折叠可能不同。
    // 以相同的字符串归一化确定预期，不把优化变成大小写语义修复。
    LUpperSegment := 'Étage';
    LLowerSegment := 'étage';
    if LUpperSegment.ToLower = LLowerSegment.ToLower then
      Match('GET', '/文档/étage', LStatic)
    else
      Match('GET', '/文档/étage', nil);
    Match('GET', '/文档/absent', nil);
  finally
    Tree.Free;
    F.Free;
  end;
  Result := LChecks;
end;
