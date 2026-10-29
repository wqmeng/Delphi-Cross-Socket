// 只经公开 API 验证共享字符串、旧记录及重复项；不访问列表私有布局。
function RunParamMutationTests: Integer;
var
  H: THttpHeader;
  U: THttpUrlParams;
  LChecks: Integer;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then
      raise Exception.Create('参数修改: ' + AMessage);
  end;

  procedure CheckParams(const AParams: TBaseParams);
  var
    E, LSaved: TParamEntry;
    B: TBytes;
    LText: string;
  begin
    B := Bytes('中文旧值');
    E := TParamEntry.Create('X-Data', Pointer(B), Length(B));
    AParams.Add(E);
    AParams.Add('x-data', 'second', True);
    AParams.Add('Z', 'tail', True);
    LSaved := AParams.Items[0];

    // 即使值来自同一个 getter，字符串写入也必须使第一项 raw 失效。
    AParams['x-DaTa'] := AParams['X-Data'];
    Check((AParams.Count = 3) and (AParams.Items[0].Name = 'X-Data')
      and (AParams.Items[0].Value = '中文旧值'), '自赋值保留名称和值');
    Check(not AParams.Items[0].HasRawValue and (AParams.Items[0].RawValue = nil),
      '相同字符串覆盖清除字节来源');
    Check((AParams.Items[1].Name = 'x-data') and (AParams.Items[1].Value = 'second'),
      '仅更新第一条同名项');
    Check(LSaved.HasRawValue and (LSaved.Value = '中文旧值')
      and SameBytes(LSaved.RawValue, B), '旧记录仍保留原始字节和值');

    // 名称和值可共享同一个字符串，赋值不能先释放仍在使用的源。
    AParams.Add(AParams.Items[0].Name, AParams.Items[0].Name);
    Check((AParams.Count = 3) and (AParams.Items[0].Name = 'X-Data')
      and (AParams.Items[0].Value = 'X-Data'), '使用已有名称作新值');
    AParams['X-Data'] := AParams.Items[1].Value;
    Check((AParams.Items[0].Value = 'second') and (AParams.Items[1].Value = 'second'),
      '使用另一条记录的值');
    AParams.Add(AParams.Items[1].Value, AParams.Items[1].Value);
    Check((AParams.Count = 4) and (AParams.Items[3].Name = 'second')
      and (AParams.Items[3].Value = 'second') and not AParams.Items[3].HasRawValue,
      '使用已有值追加新名称');
    AParams['X-Data'] := '';
    Check((AParams.Items[0].Name = 'X-Data') and (AParams.Items[0].Value = '')
      and not AParams.Items[0].HasRawValue and (AParams.Items[0].RawValue = nil),
      '空字符串覆盖');

    LText := AParams.Items[1].Value;
    AParams['X-Data'] := LText;
    LText[1] := 'S';
    Check((LText = 'Second') and (AParams.Items[0].Value = 'second')
      and (AParams.Items[1].Value = 'second'), '外部共享字符串写时复制');

    // 删除参数本身来自待删项；删除全部大小写变体后，其他项顺序保持。
    AParams['X-Data'] := 'X-Data';
    AParams.Remove(AParams['X-Data']);
    Check((AParams.Count = 2) and (AParams.Items[0].Name = 'Z')
      and (AParams.Items[1].Name = 'second'), '值参数别名删除全部重复项');
    Check(LSaved.HasRawValue and (LSaved.Name = 'X-Data')
      and (LSaved.Value = '中文旧值') and SameBytes(LSaved.RawValue, B),
      '删除不污染外部完整记录');
    AParams.Remove(AParams.Items[0].Name);
    Check((AParams.Count = 1) and (AParams.Items[0].Name = 'second'),
      '名称参数来自待删项');
    AParams.Remove('missing');
    Check(AParams.Count = 1, '不存在的名称不改变集合');
    AParams.Remove(AParams.Items[0].Value);
    Check(AParams.Count = 0, '删除最后一项');
    AParams.Remove('missing');
    Check(AParams.Count = 0, '空集合删除');

    // TBaseParams 的字符串入口仍支持 Unicode 名称，不套用 HTTP token 限制。
    AParams.Add('自定义键', '一');
    AParams.Add('自定义键', '二', True);
    AParams['自定义键'] := '三';
    Check((AParams.Count = 2) and (AParams.Items[0].Value = '三')
      and (AParams.Items[1].Value = '二'), 'Unicode 名称更新保留重复规则');
    AParams.Remove('自定义键');
    Check(AParams.Count = 0, 'Unicode 名称删除');
  end;

  procedure CheckAppend(const AParams: TBaseParams);
  var
    E, LSaved, LTail: TParamEntry;
    B: TBytes;
    I, LIndex: Integer;
    LName, LValue, LText: string;
  begin
    AParams.Clear;
    B := Bytes('原始来源');
    AParams.Add(TParamEntry.Create('Source', Pointer(B), Length(B)));
    LSaved := AParams.Items[0];
    // 连续追加跨越当前 RTL 的多个容量边界；Items/getter 的结果直接作为参数传入，
    // 不先存到测试局部变量，从而覆盖扩容时的别名存活期及容量内的直接槽位写入。
    for I := 1 to 16 do
    begin
      case I mod 4 of
        0: AParams.Add(AParams.Items[0].Name, AParams.Items[0].Value, True);
        1: AParams.Add('自定义-' + IntToStr(I), AParams['Source'], True);
        2: AParams.Add('', StringOfChar('x', I * 3), True);
        3: AParams.Add('Source', '', True);
      end;
      case I mod 4 of
        0: begin LName := 'Source'; LValue := '原始来源'; end;
        1: begin LName := '自定义-' + IntToStr(I); LValue := '原始来源'; end;
        2: begin LName := ''; LValue := StringOfChar('x', I * 3); end;
        3: begin LName := 'Source'; LValue := ''; end;
      end;
      E := AParams.Items[I];
      Check((AParams.Count = I + 1) and (E.Name = LName) and (E.Value = LValue)
        and not E.HasRawValue and (E.RawValue = nil),
        '追加保留重复顺序、空字段及Unicode值 ' + IntToStr(I));
    end;
    E := AParams.Items[0];
    Check((E.Name = LSaved.Name) and (E.Value = LSaved.Value) and E.HasRawValue
      and (Pointer(E.RawValue) = Pointer(LSaved.RawValue))
      and SameBytes(LSaved.RawValue, B), '多次扩容不覆盖来源项或外部raw快照');

    // 删除有raw的尾项后复用其容量；新字符串项不得继承旧数组或来源标志。
    AParams.Add(TParamEntry.Create('Tail-Raw', Pointer(B), Length(B)));
    LIndex := AParams.Count - 1;
    LTail := AParams.Items[LIndex];
    AParams.Remove(LIndex);
    AParams.Add('', '', True);
    E := AParams.Items[LIndex];
    Check((E.Name = '') and (E.Value = '') and not E.HasRawValue
      and (E.RawValue = nil), '删除raw尾项后空字符串追加清除来源');
    Check(LTail.HasRawValue and (LTail.Value = '原始来源')
      and SameBytes(LTail.RawValue, B), '复用尾槽不污染已删除项的外部快照');

    LText := StringOfChar('z', 257);
    AParams.Add('Long-Value', LText, True);
    LText[1] := 'Q';
    Check(AParams.Items[AParams.Count - 1].Value = StringOfChar('z', 257),
      '追加后外部字符串写时复制');
  end;

begin
  LChecks := 0;
  H := THttpHeader.Create;
  U := THttpUrlParams.Create;
  try
    CheckParams(H);
    CheckParams(U);
    CheckAppend(H);
    CheckAppend(U);
  finally
    U.Free;
    H.Free;
  end;
  Result := LChecks;
end;
