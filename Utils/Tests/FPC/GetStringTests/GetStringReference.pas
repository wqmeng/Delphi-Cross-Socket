unit GetStringReference;

{$I zLib.inc}

interface

uses SysUtils, Classes, SysConst{$IFDEF FPC}, DTF.Consts{$ENDIF};

type
  // 冻结 2318fb0 的原算法，仅用于差分测试。
  TReferenceUtils = class
  public
    class function IsUTF8(const ABuf: Pointer; const ABufSize: Integer): Boolean;
    class function GetBufEncoding(const ABuf: Pointer; const ABufSize: Integer;
      var AEncoding: TEncoding; const ADefaultEncoding: TEncoding): Integer; static;
    class function GetString(const AStrBuf: PByte; ABufSize: Integer;
      const AEncoding: TEncoding = nil): string; overload; static;
    class function GetString(const AStrBytes: TBytes; AIndex, ACount: Integer;
      const AEncoding: TEncoding = nil): string; overload; static;
    class function GetString(const AStrBytes: TBytes;
      const AEncoding: TEncoding = nil): string; overload; static;
    class function GetString(const AStrStream: TStream;
      const AEncoding: TEncoding = nil): string; overload; static;
  end;

function ReferenceDecode(const AEncoding: TEncoding; const ABytes: PByte;
  AByteCount: Integer): string;

implementation

type
  TReferenceEncodingHelper = class helper for TEncoding
    function DecodeReference(const ABytes: PByte; AByteCount: Integer): string;
  end;

function ReferenceDecode(const AEncoding: TEncoding; const ABytes: PByte;
  AByteCount: Integer): string;
begin
  Result := AEncoding.DecodeReference(ABytes, AByteCount);
end;

class function TReferenceUtils.GetBufEncoding(const ABuf: Pointer; const ABufSize: Integer;
  var AEncoding: TEncoding; const ADefaultEncoding: TEncoding): Integer;

  function ContainsPreamble(const APreamble: TBytes; out APreambleSize: Integer): Boolean;
  begin
    APreambleSize := 0;
    if (Length(APreamble) <= 0)
      or (ABufSize < Length(APreamble)) then Exit(False);

    Result := CompareMem(ABuf, @APreamble[0], Length(APreamble));

    if Result then
      APreambleSize := Length(APreamble);
  end;

begin
  Result := 0;

  if (AEncoding = nil) then
  begin
    if ContainsPreamble(TEncoding.UTF8.GetPreamble, Result) then
      AEncoding := TEncoding.UTF8
    else if ContainsPreamble(TEncoding.Unicode.GetPreamble, Result) then
      AEncoding := TEncoding.Unicode
    else if ContainsPreamble(TEncoding.BigEndianUnicode.GetPreamble, Result) then
      AEncoding := TEncoding.BigEndianUnicode
    else if IsUTF8(ABuf, ABufSize) then
      AEncoding := TEncoding.UTF8
    else
    begin
      if (ADefaultEncoding <> nil) then
        AEncoding := ADefaultEncoding
      else
        AEncoding := TEncoding.Default;
      ContainsPreamble(AEncoding.GetPreamble, Result);
    end;
  end else
  begin
    ContainsPreamble(AEncoding.GetPreamble, Result);
  end;
end;

class function TReferenceUtils.GetString(const AStrBuf: PByte; ABufSize: Integer;
  const AEncoding: TEncoding): string;
var
  LEncoding: TEncoding;
  LBomLen: Integer;
begin
  if (AStrBuf = nil) or (ABufSize <= 0) then Exit('');

  LEncoding := AEncoding;
  LBomLen := TReferenceUtils.GetBufEncoding(AStrBuf, ABufSize, LEncoding, TEncoding.Default);

  Result := LEncoding.DecodeReference(AStrBuf + LBomLen, ABufSize - LBomLen);
end;

class function TReferenceUtils.GetString(const AStrBytes: TBytes; AIndex,
  ACount: Integer; const AEncoding: TEncoding): string;
begin
  if (AStrBytes = nil) or (ACount <= 0) then
    Result := ''
  else
    Result := GetString(PByte(AStrBytes) + AIndex, ACount, AEncoding);
end;

class function TReferenceUtils.GetString(const AStrBytes: TBytes;
  const AEncoding: TEncoding): string;
begin
  Result := GetString(AStrBytes, 0, Length(AStrBytes), AEncoding);
end;

class function TReferenceUtils.GetString(const AStrStream: TStream;
  const AEncoding: TEncoding): string;
var
  LBuf: TBytes;
  LBufSize: Integer;
  P: PByte;
begin
  if (AStrStream = nil) or (AStrStream.Size <= 0) then Exit('');

  if (AStrStream is TCustomMemoryStream) then
  begin
    P := (AStrStream as TCustomMemoryStream).Memory;
    LBufSize := AStrStream.Size;
  end else
  begin
    AStrStream.Position := 0;
    LBufSize := AStrStream.Size;
    SetLength(LBuf, LBufSize);
    AStrStream.ReadBuffer(LBuf, LBufSize);
    P := Pointer(LBuf);
  end;

  Result := GetString(P, LBufSize, AEncoding);
end;

class function TReferenceUtils.IsUTF8(const ABuf: Pointer; const ABufSize: Integer): Boolean;

  function _IsUTF8(const P, PEnd: PByte; out AUTF8Size: ShortInt): Boolean;
  var
    I: ShortInt;
  begin
    Result := False;

    // UTF8最多6个字节
    // 第一个字节高位有多少个1就表示这个字符要占用几个字节

    if (P^ and $FE) = $FC then      // 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
      AUTF8Size := 6
    else if (P^ and $FC) = $F8 then // 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
      AUTF8Size := 5
    else if (P^ and $F8) = $F0 then // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
      AUTF8Size := 4
    else if (P^ and $F0) = $E0 then // 1110xxxx 10xxxxxx 10xxxxxx
      AUTF8Size := 3
    else if (P^ and $E0) = $C0 then // 110xxxxx 10xxxxxx
      AUTF8Size := 2
    else
      Exit;

    if (P + AUTF8Size > PEnd) then Exit;

    for I := 1 to AUTF8Size - 1 do
      if (P[I] and $C0 <> $80) then Exit;

    Result := True;
  end;

var
  P, PEnd: PByte;
  LUTF8Size: ShortInt;
begin
  P := ABuf;
  PEnd := P + ABufSize;

  while (P < PEnd) do
  begin
    // $00 - $7F
    if P^ and $80 = 0 then
      Inc(P)
    else if _IsUTF8(P, PEnd, LUTF8Size) then
      Inc(P, LUTF8Size)
    else
      Exit(False);
  end;

  Result := True;
end;

function TReferenceEncodingHelper.DecodeReference(const ABytes: PByte; AByteCount: Integer): string;
var
  LSize: Integer;
begin
  if (ABytes = nil) then
    raise EEncodingError.CreateRes(@SInvalidSourceArray);
  if (AByteCount < 0) then
    raise EEncodingError.CreateResFmt(@SInvalidCharCount, [AByteCount]);

  LSize := GetCharCount(ABytes, AByteCount);
  if (AByteCount > 0) and (LSize = 0) then
    raise EEncodingError.CreateRes(@SNoMappingForUnicodeCharacter);
  SetLength(Result, LSize);
  GetChars(ABytes, AByteCount, PChar(Result), LSize);
end;

end.
