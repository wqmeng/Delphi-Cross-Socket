// 通过完整请求入口验证分片后再转换，不复用被测 helper 生成预期值。
function RunRequestQueryTests: Integer;
const
  QueryText = 'plain=abc&empty=&bare&=v&&dup=one&dup=two&eq=a=b&esc=%26%3D'
    + '&once=%2526&plus=a+b%2Bc&bad=%XY&utf8=%E4%B8%AD&raw=中文&tail=';
  Names: array[0..13] of string = ('plain', 'empty', 'bare', '', 'dup', 'dup',
    'eq', 'esc', 'once', 'plus', 'bad', 'utf8', 'raw', 'tail');
  Values: array[0..13] of string = ('abc', '', '', 'v', 'one', 'two', 'a=b', '&=',
    '%26', 'a b+c', '%XY', '中', '中文', '');
var
  F: TFixture;
  B: TBytes;
  I, C, LChecks: Integer;
  LExpected, LName, LSavedName, LSavedValue: string;
  E: TParamEntry;

  procedure Check(const ACondition: Boolean; const AMessage: string);
  begin
    Inc(LChecks);
    if not ACondition then raise Exception.Create('query分片: ' + AMessage);
  end;

  procedure Parse(const AQuery: string);
  begin
    B := Bytes('GET /query?' + AQuery + ' HTTP/1.1'#13#10'Host: localhost'#13#10#13#10);
    Check(TRequestAccess(F.Request as TCrossHttpRequest).ParseHeader(
      Pointer(B), Length(B)), '解析成功');
  end;

begin
  LChecks := 0;
  F := TFixture.Create;
  try
    Parse(QueryText);
    Check(F.Request.Query.Count = Length(Names), '空片段忽略、空名称及重复项保留');
    for I := Low(Names) to High(Names) do
    begin
      E := F.Request.Query.Items[I];
      Check((E.Name = Names[I]) and (E.Value = Values[I]), '名称和值及顺序 ' + IntToStr(I));
      Check(not E.HasRawValue and (E.RawValue = nil), 'query仍是字符串来源');
    end;
    LSavedName := F.Request.Query.Items[4].Name;
    LSavedValue := F.Request.Query.Items[4].Value;
    Parse('');
    Check(F.Request.Query.Count = 0, '空query清理已有参数');
    Check((LSavedName = 'dup') and (LSavedValue = 'one'), '清理不影响外部字符串快照');
    // 可见ASCII逐字符检查；&是结构分隔符，已在上面的固定组合中验证。
    // 加号应还原空格，单独的非法百分号原样保留，其余字符（含等号/问号）不改变。
    for C := 33 to 126 do
    begin
      if C = Ord('&') then Continue;
      LName := 'key' + IntToStr(C);
      LExpected := Char(C);
      if C = Ord('+') then LExpected := ' ';
      Parse(LName + '=' + Char(C));
      Check((F.Request.Query.Count = 1) and (F.Request.Query.Items[0].Name = LName)
        and (F.Request.Query.Items[0].Value = LExpected), 'ASCII值 ' + IntToStr(C));
    end;
    Parse('%26%3D=%2526&&+=%2B&%E4%B8%AD=中文&');
    Check(F.Request.Query.Count = 3, '转换后的分隔符不能重新拆片');
    Check((F.Request.Query.Items[0].Name = '&=') and (F.Request.Query.Items[0].Value = '%26'),
      '名称和值各解码一次');
    Check((F.Request.Query.Items[1].Name = ' ') and (F.Request.Query.Items[1].Value = '+'),
      '名称加号与编码加号');
    Check((F.Request.Query.Items[2].Name = '中') and (F.Request.Query.Items[2].Value = '中文'),
      '编码名称与Unicode值');
  finally
    F.Free;
  end;
  Result := LChecks;
end;
