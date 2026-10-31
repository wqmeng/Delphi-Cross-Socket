// 仅经公开接口验证完整记录复制和生命周期，不依赖列表容量或私有布局。
function RunParamAssignTests: Integer;
var
  H: THttpHeader;
  U: THttpUrlParams;
  E: TParamEntry;
  B: TBytes;
  LChecks: Integer;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('参数复制: ' + AMessage);
  end;

  procedure CheckCopy(const ASource, ADest: TBaseParams);
  var
    I: Integer;
    S, D: TParamEntry;
    Raw: TBytes;
    LName, LValue: string;
  begin
    ASource.Clear;
    ADest.Clear;
    ADest.Add('obsolete', '旧内容', True);
    // 超过数次常规扩容；包含重复名、文本/raw不一致及两种非常规来源标志。
    for I := 0 to 39 do
    begin
      LName := 'item' + IntToStr(I mod 7);
      LValue := '值' + IntToStr(I);
      Raw := Bytes('wire' + IntToStr(I));
      case I mod 4 of
        0:
          begin
            S := TParamEntry.Create(LName, Pointer(Raw), Length(Raw));
            S.Value := LValue;
          end;
        1:
          begin
            S := TParamEntry.Create(LName, nil, 0);
            S.Value := LValue;
          end;
        2:
          begin
            S := TParamEntry.Create(LName, LValue);
            S.RawValue := Raw;
          end;
      else
        S := TParamEntry.Create(LName, LValue);
      end;
      // Assign只复制记录，不进行Header语法检查、编码转换或来源修正。
      if I = 1 then
      begin
        S.Name := 'not a header';
        S.Value := #$D800;
      end;
      ASource.Add(S);
    end;
    Raw := nil;
    S := Default(TParamEntry);
    ADest.Assign(ASource);
    Check(ADest.Count = 40, '替换旧项并复制全部项');
    for I := 0 to 39 do
    begin
      S := ASource.Items[I];
      D := ADest.Items[I];
      Check((D.Name = S.Name) and (D.Value = S.Value)
        and (D.HasRawValue = S.HasRawValue)
        and (Pointer(D.RawValue) = Pointer(S.RawValue))
        and SameBytes(D.RawValue, S.RawValue), '四字段和raw引用保持 item=' + IntToStr(I));
    end;
    ADest.Assign(ADest);
    Check((ADest.Count = 40) and ADest.Items[1].HasRawValue
      and (ADest.Items[1].RawValue = nil) and (ADest.Items[1].Value = #$D800),
      '自赋值保留空raw及原始文本');

    // 字符串写时复制；修改目标记录和字符串入口都不能污染来源。
    S := ASource.Items[3];
    D := ADest.Items[3];
    D.Name[1] := 'X';
    D.Value[1] := 'Y';
    ADest.Items[3] := D;
    Check((ASource.Items[3].Name = S.Name) and (ASource.Items[3].Value = S.Value),
      '目标字符串修改不污染来源');
    Check((ADest.Items[3].Name = D.Name) and (ADest.Items[3].Value = D.Value),
      '目标记录修改实际生效');
    ADest['item0'] := 'target-only';
    Check((ASource.Items[0].Value = '值0') and ASource.Items[0].HasRawValue
      and (ADest.Items[0].Value = 'target-only') and not ADest.Items[0].HasRawValue,
      '目标字符串入口失效raw但不改变来源');

    // 不留测试局部的raw引用，来源清空后由目标独立维持记录生命周期。
    S := Default(TParamEntry);
    D := Default(TParamEntry);
    ASource.Clear;
    Check((ADest.Count = 40) and (ADest.Items[39].Value = '值39'),
      '来源清空后目标仍存活');
    Check(not ADest.Items[2].HasRawValue
      and SameBytes(ADest.Items[2].RawValue, Bytes('wire2')),
      '来源清空后未标记raw仍按完整记录保留');
    ADest.Assign(ASource);
    Check(ADest.Count = 0, '空来源替换非空目标');
    ADest.Add('obsolete', 'value', True);
    ADest.Assign(nil);
    Check(ADest.Count = 0, 'nil来源清空非空目标');
  end;

begin
  LChecks := 0;
  H := THttpHeader.Create;
  U := THttpUrlParams.Create;
  try
    // 两个方向分别覆盖普通列表与FPC专用Header列表作为复制目标。
    CheckCopy(H, U);
    CheckCopy(U, H);
    B := Bytes('source-freed');
    E := TParamEntry.Create('Survive', Pointer(B), Length(B));
    H.Add(E);
    U.Assign(H);
    B := nil;
    E := Default(TParamEntry);
    FreeAndNil(H);
    Check((U.Count = 1) and (U.Items[0].Name = 'Survive')
      and (U.Items[0].Value = 'source-freed') and U.Items[0].HasRawValue
      and SameBytes(U.Items[0].RawValue, Bytes('source-freed')),
      '来源释放后目标文本和raw仍有效');
  finally
    U.Free;
    H.Free;
  end;
  Result := LChecks;
end;
