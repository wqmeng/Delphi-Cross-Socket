// 使用明确的接收行为预期，不复用生产扫描器；这里只测试Cookie文本，不改变HTTP字段校验。
function RunRequestCookieScanTests: Integer;
const
  BadAfterValid: array[0..3] of string = (
    'seed=changed; Bad Name=x', 'seed=changed;;tail=more',
    'seed=changed;missing', 'seed=changed; =x');
var
  LChecks, I, J: Integer;
  C: TRequestCookies;
  B: TBytes;
  E, LSaved: TParamEntry;
  S, LSavedName, LSavedValue: string;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('请求Cookie扫描: ' + AMessage);
  end;

  procedure CheckCase(const AInput: string; const AExpected: Boolean;
    const ANames, AValues: array of string);
  var
    H: TRequestCookies;
    K: Integer;
    LItem: TParamEntry;
  begin
    H := TRequestCookies.Create;
    try
      Check(H.Decode(AInput) = AExpected, '返回值: ' + AInput);
      Check(H.Count = Length(ANames), '字段数: ' + AInput);
      for K := 0 to High(ANames) do
      begin
        LItem := H.Items[K];
        Check((LItem.Name = ANames[K]) and (LItem.Value = AValues[K]),
          '名称/值/顺序: ' + IntToStr(K));
        Check(not LItem.HasRawValue and (LItem.RawValue = nil), '解码为字符串来源');
      end;
    finally
      H.Free;
    end;
  end;

  procedure Seed;
  begin
    C.Clear;
    B := Bytes('旧值');
    C.Add(TParamEntry.Create('Seed', Pointer(B), Length(B)));
    C.Add('sEED', 'second', True);
    C.Add('Other', 'tail', True);
    LSaved := C.Items[0];
  end;

  procedure CheckSnapshot;
  begin
    Check((LSaved.Name = 'Seed') and (LSaved.Value = '旧值') and LSaved.HasRawValue
      and SameBytes(LSaved.RawValue, B), '外部完整记录快照保持');
  end;

begin
  LChecks := 0;
  CheckCase('', False, [], []);
  CheckCase(' '#9, False, [], []);
  CheckCase(';', False, [], []);
  CheckCase('a=1', True, ['a'], ['1']);
  CheckCase('a=', True, ['a'], ['']);
  CheckCase('a=1;', True, ['a'], ['1']);
  CheckCase('a=1; '#9, True, ['a'], ['1']);
  // 旧行为只容许最后的分隔符；前导/中间空片段属于整行错误。
  CheckCase(';a=1', False, [], []);
  CheckCase('a=1;;b=2', False, [], []);
  CheckCase('a=1; ;b=2', False, [], []);
  CheckCase(#9' a=1; '#9'b=2', True, ['a', 'b'], ['1', '2']);
  CheckCase('a =1', False, [], []);
  CheckCase('=1', False, [], []);
  CheckCase('missing', False, [], []);
  CheckCase('a=1;missing', False, [], []);
  CheckCase('a=b=c', True, ['a'], ['b=c']);
  CheckCase('a==b=', True, ['a'], ['=b=']);
  CheckCase('a="b=c"', True, ['a'], ['b=c']);
  CheckCase('a=""', True, ['a'], ['']);
  // 接收端保留既有宽松值规则，不借优化收紧引号/逗号/Unicode。
  CheckCase('a="unterminated', True, ['a'], ['"unterminated']);
  CheckCase('a=" b " ', True, ['a'], ['" b " ']);
  CheckCase('a="a;b"', False, [], []);
  CheckCase('a=one, two', True, ['a'], ['one, two']);
  CheckCase('originalCase=first; ORIGINALCASE=second; tail=x', True,
    ['originalCase', 'tail'], ['second', 'x']);
  CheckCase('word=中文'#$D83D#$DE00, True, ['word'], ['中文'#$D83D#$DE00]);
  CheckCase('中文=x', False, [], []);
  CheckCase(' a= x '#9, True, ['a'], [' x '#9]);
  CheckCase('literal=%41+a', True, ['literal'], ['%41+a']);
  CheckCase('a=x'#0'y', True, ['a'], ['x'#0'y']);
  CheckCase(#10'a=1', False, [], []);

  C := TRequestCookies.Create;
  try
    // 先有合法项、后遇错误时，不得提交前面的部分更新。
    for I := 0 to 1 do
      for J := Low(BadAfterValid) to High(BadAfterValid) do
      begin
        Seed;
        Check(not C.Decode(BadAfterValid[J], I <> 0), '后续坏项整行失败');
        if I <> 0 then
          Check(C.Count = 0, '清空模式失败后为空')
        else
        begin
          Check(C.Count = 3, '追加模式失败保留原计数');
          E := C.Items[0];
          Check((E.Name = 'Seed') and (E.Value = '旧值') and E.HasRawValue
            and (Pointer(E.RawValue) = Pointer(LSaved.RawValue)), '失败不更新首项或raw身份');
          Check((C.Items[1].Name = 'sEED') and (C.Items[1].Value = 'second')
            and (C.Items[2].Name = 'Other') and (C.Items[2].Value = 'tail'),
            '失败保留重复项和其它项');
        end;
        CheckSnapshot;
      end;

    Seed;
    Check(C.Decode('seed=one; New=first; SEED=two; new=second', False), '合法追加提交');
    Check(C.Count = 4, '同名更新不增加重复项');
    E := C.Items[0];
    Check((E.Name = 'Seed') and (E.Value = 'two') and not E.HasRawValue
      and (E.RawValue = nil), '只更新首个旧项并使raw失效');
    Check((C.Items[1].Name = 'sEED') and (C.Items[1].Value = 'second')
      and (C.Items[2].Name = 'Other') and (C.Items[2].Value = 'tail'), '其它旧项及顺序保持');
    Check((C.Items[3].Name = 'New') and (C.Items[3].Value = 'second'), '新项保留首次名称和最后值');
    CheckSnapshot;

    Seed;
    Check(C.Decode('fresh=value', True), '清空模式成功');
    Check((C.Count = 1) and (C.Items[0].Name = 'fresh')
      and (C.Items[0].Value = 'value'), '成功替换旧集合');
    CheckSnapshot;
    Seed;
    Check(not C.Decode(' '#9, False) and (C.Count = 3), '空追加不改变已有集合');
    CheckSnapshot;
    Check(not C.Decode(' '#9, True) and (C.Count = 0), '空清空模式仍清理旧集合');
    CheckSnapshot;

    S := 'Held=中文=a';
    Check(C.Decode(S, True), '保存名称和值');
    LSavedName := C.Items[0].Name;
    LSavedValue := C.Items[0].Value;
    S[1] := 'X';
    Check((C.Items[0].Name = 'Held') and (C.Items[0].Value = '中文=a'),
      '调用方修改输入不污染结果');
    C.Clear;
    Check((LSavedName = 'Held') and (LSavedValue = '中文=a'), '集合清空后外部文本快照有效');
  finally
    C.Free;
  end;
  Result := LChecks;
end;
