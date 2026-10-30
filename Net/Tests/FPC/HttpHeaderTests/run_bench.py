"""#199 固定源码、正确性门槛、A/A 与交替 A/B；只写独立 bin 目录。"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import io
import json
import math
import os
import platform
import re
import random
import shutil
import statistics
import subprocess
import sys
import zipfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
DELPHI = Path(r'D:\Design\Delphi\D13.1')
FPC = Path(r'D:\Design\FreePascal\fpc\bin\x86_64-win64\ppcx64.exe')
CN = Path(r'D:\Design\Delphi\VclFmxRtl\cncrypto\Source')
FASTMM = Path(r'D:\Design\Delphi\VclFmxRtl\FastMM5\FastMM5.pas')
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
TARGETS = ['win64', 'win32', 'fpc64']
CASES = [('wire', x) for x in ('small', 'ascii', 'many', 'cookie', 'original')]
CASES += [('string', 'ascii')] + [('request', x) for x in ('small', 'ascii', 'original')]
CASES += [('new-request', 'ascii'), ('response', 'ascii'), ('encode', 'ascii')]
# 保留旧 CASES 的顺序，新增项只扩展覆盖，不改变旧输入。
CASES += [(mode, fixture) for mode in ('string', 'new-request', 'response', 'encode', 'server-encode')
          for fixture in ('small', 'many', 'cookie', 'unicode') if (mode, fixture) not in CASES]
CASES += [('request', 'noquery'), ('new-request', 'noquery'), ('server-encode', 'ascii')]
CASES += [('request', x) for x in ('many', 'cookie', 'unicode')]
CASES += [(mode, 'original') for mode in ('string', 'new-request', 'response', 'encode', 'server-encode')]
# 旧手工表达式和单独字段生成仅供补充分析，不加入默认 all 恢复门槛。
EXTRA_CASES = [(mode, fixture) for mode in ('encode-legacy', 'field-encode')
               for fixture in ('small', 'ascii', 'many', 'cookie', 'original', 'unicode')]
# URL解码专用输入只显式选择，不改变既有all对照范围。
EXTRA_CASES += [('request', fixture) for fixture in ('url-escaped', 'url-unicode', 'url-long-tail')]
AVAILABLE_CASES = CASES + EXTRA_CASES


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')


def command(args, cwd=REPO, timeout=120):
    result = subprocess.run(list(map(str, args)), cwd=cwd, capture_output=True,
                            timeout=timeout, creationflags=NO_WINDOW)
    if result.returncode:
        raise RuntimeError((result.stdout + result.stderr).decode('utf-8', errors='replace')[-6000:])
    return result.stdout


def http_module(root=None):
    driver = root / 'harness/run_http_bench.py' if root and (root / 'harness/run_http_bench.py').exists() else HERE.parent / 'HttpHotPathTests/run_http_bench.py'
    spec = importlib.util.spec_from_file_location('http_bench', driver)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.REPO = REPO  # 冻结驱动所在目录不同，示例模板仍明确取仓库。
    # #199 同时改变 Params/Server/Client/Utils；不能把旧记录类型和新调用方混编。
    mod.UNITS = ('Net.CrossHttpParams.pas', 'Net.CrossHttpServer.pas', 'Net.CrossHttpClient.pas',
                 'Net.CrossHttpUtils.pas', 'Net.CrossWebSocketClient.pas')
    return mod


def ensure_idle():
    # 不终止已有进程；有其他压测时拒绝启动本轮，以免污染 A/A、A/B。
    raw = command(['powershell.exe', '-NoProfile', '-Command', 'Get-Process | Select-Object -ExpandProperty ProcessName']).decode('utf-8', errors='replace').lower().splitlines()
    names = ['headerbench.exe', 'httpserver.exe', 'ab.exe', 'bombardier-windows-amd64.exe',
             'getstringtests.exe', 'testxxhashperf.exe', 'httphotpathtests.exe']
    active = [x for x in names if x.removesuffix('.exe') in raw]
    if active:
        raise RuntimeError('已有服务或基准正在运行：' + ', '.join(active))


def snapshot(root, before, before_source=None, after_source=None):
    archive = zipfile.ZipFile(io.BytesIO(command(['git', 'archive', '--format=zip', before,
                                                 'Net', 'Utils', 'DelphiToFPC', 'zLib.inc'])))
    sources = {'before': {}, 'after': {}}
    for entry in archive.infolist():
        path = Path(entry.filename)
        if entry.is_dir() or path.suffix.lower() not in ('.pas', '.inc'):
            continue
        if any(x.lower() in ('tests', 'demos') for x in path.parts):
            continue
        for role in ('before', 'after'):
            dest = root / role / 'source' / path
            assert dest.resolve().is_relative_to((root / role).resolve())
            dest.parent.mkdir(parents=True, exist_ok=True)
            origin = before_source if role == 'before' else after_source
            data = ((Path(origin) / path).read_bytes() if origin else
                    (archive.read(entry) if role == 'before' else (REPO / path).read_bytes()))
            dest.write_bytes(data)
            sources[role][path.as_posix()] = sha(data)
    save(root / 'source-manifest.json', sources)
    fixtures(root)


def fixtures(root):
    # 增加 fixture 时核对旧文件内容，已有测量输入不可被悄悄覆盖。
    original = bytes.fromhex((HERE.parent / 'Issue199Tests/request.hex').read_text(encoding='ascii'))
    small = b'GET /bench?a=1&b=2 HTTP/1.1\r\nHost: localhost\r\nUser-Agent: HeaderBench\r\nAccept: */*\r\nConnection: keep-alive\r\n\r\n'
    values = {'original': original, 'ascii': original.replace('有模式窗口'.encode(), b'window'), 'small': small}
    values['many'] = small[:-2] + b''.join(f'X-Field-{i:02d}: '.encode() + b'a' * 32 + b'\r\n' for i in range(36)) + b'\r\n'
    values['cookie'] = small[:-2] + b'Cookie: session=' + b'a' * 8192 + b'\r\n\r\n'
    values['noquery'] = small.replace(b'/bench?a=1&b=2', b'/bench')
    values['unicode'] = small[:-2] + 'X-Text: 中文文件名😀\r\n\r\n'.encode('utf-8')
    values['url-escaped'] = small.replace(b'/bench?a=1&b=2', b'/a%2Fb%20c?key=a%26b&plus=x+y')
    values['url-unicode'] = small.replace(b'/bench?a=1&b=2', '/中文/文件?name=中文&other=值'.encode('utf-8'))
    values['url-long-tail'] = small.replace(b'/bench?a=1&b=2', b'/' + b'a' * 1024 + b'%2Fend?q=' + b'x' * 1024 + b'%20z')
    folder = root / 'fixtures'
    folder.mkdir(exist_ok=True)
    for name, data in values.items():
        path = folder / (name + '.bin')
        if path.exists() and path.read_bytes() != data:
            raise RuntimeError('拒绝覆盖不同的 fixture: ' + str(path))
        if not path.exists():
            path.write_bytes(data)
    save(root / 'fixtures.json', {k: {'bytes': len(v), 'sha256': sha(v)} for k, v in values.items()})


def freeze_harness(root):
    folder = root / 'harness'
    folder.mkdir(exist_ok=False)
    names = ['run_bench.py', 'HeaderBench.lpr', 'HeaderBench.dpr', 'HeaderMemoryBench.lpr', 'HeaderMemoryBench.dpr']
    for name in names:
        shutil.copy2(HERE / name, folder / name)
    shutil.copy2(HERE.parent / 'HttpHotPathTests/run_http_bench.py', folder / 'run_http_bench.py')
    save(root / 'harness-manifest.json', {p.name: sha(p.read_bytes()) for p in folder.iterdir()})


def role_flags(root, role):
    config = root / 'settings.json'
    if config.exists():
        return json.loads(config.read_text(encoding='utf-8'))['header_bytes'][role]
    # 兼容最初由旧驱动建立但尚未编译的冻结目录。
    return 'TParamEntry = record' in (root / role / 'source/Net/Net.CrossHttpParams.pas').read_text(encoding='utf-8-sig')


def _pascal_code(text):
    # 识别真实语句，不让注释或字符串里的同名调用影响分支选择。
    token = re.compile(r"'(?:''|[^'])*'|//[^\n]*|\{.*?\}|\(\*.*?\*\)", re.S)
    return token.sub(lambda match: "''" if match[0].startswith("'") else ' ', text).lower()


def _function_source(path, qualified_name):
    text = path.read_text(encoding='utf-8-sig')
    start = re.search(r'^function\s+' + re.escape(qualified_name) + r'\b', text, re.M | re.I)
    if not start:
        raise RuntimeError('找不到出站函数: ' + qualified_name)
    following = re.search(r'^(?:function|procedure|constructor|destructor|class function|class procedure)\s+',
                          text[start.end():], re.M | re.I)
    end = start.end() + following.start() if following else len(text)
    return text[start.start():end]


def _detect_out_function(path, qualified_name):
    source = _function_source(path, qualified_name)
    code = _pascal_code(source)
    result = re.search(r'\bresult\s*:=\s*tencoding\.(ascii|utf8)\.getbytes\(\s*(\w+)\s*\)\s*;', code)
    safe = re.search(r'\bresult\s*:=\s*tcrosshttputils\.encodeheader\(\s*(\w+)\s*\)\s*;', code)
    if safe and result:
        raise RuntimeError('同一发送函数混用 helper 与直接编码，拒绝猜测: ' + qualified_name)
    if safe:
        codec, header = 'safe_utf8', safe[1]
        result = safe
    elif result:
        codec, header = result[1], result[2]
    else:
        raise RuntimeError('无法识别实际输出编码: ' + qualified_name)
    checks = list(re.finditer(r'\btcrosshttputils\.checkutf16\(\s*' + re.escape(header) + r'\s*\)\s*;', code))
    direct = re.search(r'\b' + re.escape(header) + r'\s*:=\s*' + re.escape(header)
                       + r'\s*\+\s*(?:frequestobj\.)?fheader\.encode\s*;', code)
    field = re.search(r'\b(\w+)\s*:=\s*(?:frequestobj\.)?fheader\.encode\s*;', code)
    mode = None
    if direct and direct.end() < result.start():
        if codec == 'ascii' and not checks:
            mode = 'ascii'
        elif codec == 'utf8' and len(checks) == 1 and direct.end() <= checks[0].start() < result.start():
            mode = 'full_utf8'
    if field and codec == 'utf8' and len(checks) == 1:
        joined = re.search(r'\b' + re.escape(header) + r'\s*:=\s*' + re.escape(header)
                           + r'\s*\+\s*' + re.escape(field[1]) + r'\s*;', code)
        if joined and field.end() <= checks[0].start() < joined.start() and joined.end() <= result.start():
            mode = 'prefix_utf8'
    if safe and not checks:
        # helper 自己检查完整 UTF-16；若调用方还检查同一头，不能省略那份实际工作。
        joined = (re.search(r'\b' + re.escape(header) + r'\s*:=\s*' + re.escape(header)
                            + r'\s*\+\s*' + re.escape(field[1]) + r'\s*;', code) if field else None)
        if ((direct and direct.end() < safe.start())
                or (field and joined and field.end() <= joined.start() and joined.end() < safe.start())):
            mode = 'safe_utf8'
    if mode is None:
        raise RuntimeError('未识别或混合的出站流程，请先更新基准映射，不能默认猜测: ' + qualified_name)
    return {'mode': mode, 'function': qualified_name, 'source_sha256': sha(source.encode('utf-8'))}


def output_path(source):
    paths = {'server': _detect_out_function(source / 'Net/Net.CrossHttpServer.pas', 'TCrossHttpResponse._CreateHeader'),
             'client': _detect_out_function(source / 'Net/Net.CrossHttpClient.pas', 'TCrossHttpClientConnection._CreateRequestHeader')}
    if paths['server']['mode'] != paths['client']['mode']:
        raise RuntimeError('Server/Client 出站流程不同，不能用同一 encode case 代表: ' + repr(paths))
    paths['mode'] = paths['server']['mode']
    if paths['mode'] == 'safe_utf8':
        helper = source / 'Net/Net.CrossHttpUtils.pas'
        paths['helper'] = {'file': 'Net/Net.CrossHttpUtils.pas', 'source_sha256': sha(helper.read_bytes())}
    return paths


def server_overlay(source, out):
    # 包装只存在于独立编译 overlay；不修改生产或冻结源。两角色应用相同变换。
    original = (source / 'Net/Net.CrossHttpServer.pas').read_bytes()
    text = original.decode('utf-8-sig')
    declaration = 'function HttpHeaderBenchCreate(const AResponse: TCrossHttpResponse; const ABodySize: Int64): TBytes;'
    match = re.search(r'^implementation\s*$', text, re.M)
    if not match:
        raise RuntimeError('HttpServer implementation boundary missing')
    text = text[:match.start()] + declaration + '\n\n' + text[match.start():]
    end = text.rfind('end.')
    if end < 0:
        raise RuntimeError('HttpServer unit end missing')
    text = text[:end] + declaration + '\nbegin\n  Result := AResponse._CreateHeader(ABodySize, False);\nend;\n\n' + text[end:]
    overlay = out / 'bench-source'
    overlay.mkdir(exist_ok=True)
    data = ('\ufeff' + text.replace('\r\n', '\n').replace('\n', '\r\n')).encode('utf-8')
    (overlay / 'Net.CrossHttpServer.pas').write_bytes(data)
    save(out / 'overlay.json', {'purpose': '同单元薄包装真实 _CreateHeader，构造在计时外',
         'frozen_source_sha256': sha(original), 'overlay_sha256': sha(data)})
    return overlay


def build_micro(root, role, target, program="HeaderBench"):
    source = root / role / 'source'
    out = root / role / target
    out.mkdir(parents=True, exist_ok=True)
    units = out / 'units'
    units.mkdir(exist_ok=True)
    search = ([server_overlay(source, out)] if program == 'HeaderBench' else []) + [source / 'Net', source / 'Utils', CN]
    harness = root / 'harness' if (root / 'harness').exists() else HERE
    out_path = output_path(source)
    defines = ['OUT_' + out_path['mode'].upper()]
    if role_flags(root, role):
        defines.append('HEADER_BYTES')
    if target == 'fpc64':
        search += [source / 'DelphiToFPC', Path(r'D:\Design\FreePascal\lazarus\components\lazutils')]
        args = [FPC, '-B', '-O3', '-CX', '-Xs', '-XX']
        args += ['-d' + define for define in defines]
        args += ['-Fu' + str(p) for p in search]
        args += ['-Fi' + str(source), '-FE' + str(out), '-FU' + str(units), harness / (program + '.lpr')]
    else:
        args = [DELPHI / 'bin' / ('dcc64.exe' if target == 'win64' else 'dcc32.exe'),
                '-Q', '-B', '-$C-', '-$D+', '-$I-', '-$L+', '-$O+', '-$Q-', '-$R-', '-$Y+',
                '-NSSystem;System.Win;Winapi', '-U' + ';'.join(map(str, search + [DELPHI / 'lib' / target / 'release'])),
                '-I' + str(source) + ';' + str(CN), '-E' + str(out), '-N0' + str(units)]
        args += ['-D' + define for define in defines]
        args += [harness / (program + '.dpr')]
    try:
        data = command(args, HERE)
    except RuntimeError as error:
        (out / (program + '-build-failed.log')).write_text(str(error), encoding='utf-8')
        save(out / (program + '-build-failed.json'), {'command': list(map(str, args)), 'harness_sha256': sha((harness / (program + '.lpr')).read_bytes()), 'out_path': out_path, 'defines': defines})
        raise
    (out / (program + '-build.log')).write_bytes(data)
    save(out / (program + '-build.json'), {'command': list(map(str, args)), 'harness_sha256': sha((harness / (program + '.lpr')).read_bytes()),
                             'header_bytes': role_flags(root, role), 'out_path': out_path, 'defines': defines, 'source_manifest_sha256': sha((root / 'source-manifest.json').read_bytes()),
                             'binary_sha256': sha((out / (program + '.exe')).read_bytes())})
    print('BUILD', role, target, flush=True)


def build(root, targets=TARGETS, skip_http=False, only_http=False):
    if not (root / 'harness').exists():
        freeze_harness(root)
    fixtures(root)
    if not only_http:
        for target in targets:
            for role in ('before', 'after'):
                build_micro(root, role, target)
    if not skip_http:
        mod = http_module(root)
        for allocator in ('rtl', 'fastmm5'):
            folder = root / 'http' / allocator
            folder.mkdir(parents=True, exist_ok=True)
            for role in ('before', 'after'):
                mod.build(folder, role, '566187c', FASTMM if allocator == 'fastmm5' else None, root / role / 'source')
                metadata_file = folder / role / 'source.json'
                metadata = json.loads(metadata_file.read_text(encoding='utf-8'))
                metadata['out_path'] = output_path(root / role / 'source')
                save(metadata_file, metadata)
        save(root / 'tools.json', {str(p): sha(p.read_bytes()) for p in (mod.AB, mod.BOMB, FASTMM)})


def prepare(root, before, before_source=None, after_source=None, before_bytes='auto', after_bytes='auto',
            targets=TARGETS, skip_http=False, reference_label=None, candidate_label=None):
    root.mkdir(parents=True, exist_ok=False)
    snapshot(root, before, before_source, after_source)
    flags = {}
    for role, value in (('before', before_bytes), ('after', after_bytes)):
        flags[role] = role_flags(root, role) if value == 'auto' else value == 'yes'
    workspace_head = command(['git', 'rev-parse', 'HEAD']).decode().strip()
    reference = {'label': reference_label or (str(before_source) if before_source else before),
                 'kind': 'frozen_source' if before_source else 'git_revision',
                 'source': str(before_source) if before_source else None,
                 'revision': None if before_source else before}
    candidate = {'label': candidate_label or (str(after_source) if after_source else 'working-tree'),
                 'kind': 'frozen_source' if after_source else 'working_tree',
                 'source': str(after_source) if after_source else None}
    save(root / 'settings.json', {'header_bytes': flags, 'template_revision': before,
         'reference': reference, 'candidate': candidate,
         'out_paths': {role: output_path(root / role / 'source') for role in ('before', 'after')},
         'source_manifest_sha256': sha((root / 'source-manifest.json').read_bytes()),
         'before_source': str(before_source) if before_source else None,
         'after_source': str(after_source) if after_source else None})
    save(root / 'environment.json', {'template_revision': before, 'reference': reference,
         'candidate': candidate, 'workspace_head_at_prepare': workspace_head,
         'platform': platform.platform(), 'processor': os.environ.get('PROCESSOR_IDENTIFIER'), 'cpu_count': os.cpu_count(),
         'micro_affinity_mask': 4, 'micro_allocator': 'compiler default',
         'runner_sha256': sha(Path(__file__).read_bytes()), 'http_runner_sha256': sha((HERE.parent / 'HttpHotPathTests/run_http_bench.py').read_bytes())})
    build(root, targets, skip_http)


def invoke(root, role, target, mode, fixture, count, allocations=False):
    exe = root / role / target / 'HeaderBench.exe'
    raw = command([exe, mode, root / 'fixtures' / (fixture + '.bin'), str(count), 'alloc' if allocations else 'time', '4'])
    text = raw.decode('utf-8', errors='replace')
    if 'SKIP invalid_output' in text:
        return {'valid': False, 'output': text.strip()}
    m = re.search(r'METRIC n=(\d+) us=([\d.]+) calls=(\d+) bytes=(\d+) checksum=(\d+)', text)
    if not m:
        raise RuntimeError(text)
    return {'valid': True, 'n': int(m[1]), 'us': float(m[2]), 'ns_per_op': float(m[2]) * 1000 / int(m[1]),
            'calls_per_op': int(m[3]) / int(m[1]), 'allocated_bytes_per_op': int(m[4]) / int(m[1]),
            'checksum': int(m[5]), 'shape': re.search(r'VALID (.+)', text)[1].strip()}


def summary(values):
    return {'median': statistics.median(values), 'min': min(values), 'max': max(values)}


def micro(root, samples, targets=TARGETS, cases=CASES, window_ms=250, aa_samples=None):
    rows = []
    aa_samples = samples if aa_samples is None else aa_samples
    save(root / 'micro-settings.json', {'targets': targets, 'cases': cases, 'samples': samples,
         'aa_samples': aa_samples, 'window_ms': window_ms, 'bootstrap_seed': 199, 'bootstrap_draws': 5000,
         'comparison': json.loads((root / 'settings.json').read_text(encoding='utf-8')) if (root / 'settings.json').exists() else None,
         'out_paths': {role: output_path(root / role / 'source') for role in ('before', 'after')},
         'case_notes': {'encode': '按冻结Server/Client真实出站流程映射；真实server-encode交叉验证',
                        'encode-legacy': '旧手工额外全头Check表达式，仅补充对照，不属于默认all',
                        'field-encode': '单独Header.Encode生成Unicode文本，仅补充分析'}})
    for target in targets:
        for mode, fixture in cases:
            pilot = {role: invoke(root, role, target, mode, fixture, 1000) for role in ('before', 'after')}
            valid = [role for role in pilot if pilot[role]['valid']]
            assert pilot['after']['valid'], (target, mode, fixture, pilot)
            # 同一对使用相同循环数；用较快端校准到至少约 250ms，避免短计时放大启动噪声。
            count = min(100000000, max(2000, math.ceil(window_ms * 1000 / min(pilot[r]['us'] for r in valid) * 1000)))
            row = {'target': target, 'mode': mode, 'fixture': fixture, 'count': count, 'pilot': pilot, 'aa': [], 'pairs': []}
            for role in valid:
                invoke(root, role, target, mode, fixture, count)  # 不计入统计的完整预热。
            aa_role = 'before' if pilot['before']['valid'] else 'after'
            for i in range(aa_samples):
                a1 = invoke(root, aa_role, target, mode, fixture, count)
                a2 = invoke(root, aa_role, target, mode, fixture, count)
                row['aa'].append({'reference': aa_role, 'a1_ns': a1['ns_per_op'], 'a2_ns': a2['ns_per_op'],
                                  'change_percent': (a2['ns_per_op'] / a1['ns_per_op'] - 1) * 100})
            for i in range(samples):
                pair = {'index': i}
                for role in (('before', 'after') if i % 2 == 0 else ('after', 'before')):
                    if role in valid:
                        pair[role] = invoke(root, role, target, mode, fixture, count)
                if len(valid) == 2:
                    pair['time_change_percent'] = (pair['after']['ns_per_op'] / pair['before']['ns_per_op'] - 1) * 100
                row['pairs'].append(pair)
            row['summary_ns'] = {role: summary([x[role]['ns_per_op'] for x in row['pairs']]) for role in valid}
            if len(valid) == 2:
                row['paired_change_percent'] = summary([x['time_change_percent'] for x in row['pairs']])
                row['decision'] = paired_decision([x['after']['ns_per_op'] / x['before']['ns_per_op'] for x in row['pairs']],
                                                 [x['a2_ns'] / x['a1_ns'] for x in row['aa']])
            else:
                row['decision'] = {'status': 'reference_invalid', 'reason': '旧版输出错误，须另以新合同基线比较'}
            # 分配统计另跑，不把内存管理器包装器带来的计数开销混入普通耗时。
            row['allocations'] = {role: invoke(root, role, target, mode, fixture, 10000, True) for role in valid}
            rows.append(row)
            save(root / 'micro.json', rows)
            print(json.dumps({k: row[k] for k in ('target', 'mode', 'fixture', 'summary_ns')}), flush=True)


def http(root, samples, duration, concurrency, aa_samples=5):
    mod = http_module(root)
    rows = []
    save(root / f'http-settings-c{concurrency}.json', {'samples': samples, 'aa_pairs': aa_samples, 'duration_seconds': duration, 'concurrency': concurrency, 'runner_sha256': sha(Path(__file__).read_bytes()), 'http_runner_sha256': sha((HERE.parent / 'HttpHotPathTests/run_http_bench.py').read_bytes())})
    for allocator in ('rtl', 'fastmm5'):
        folder = root / 'http' / allocator
        for phase, repetitions in (('aa', aa_samples), ('ab', samples)):
            logs = folder / (phase + f'-c{concurrency}')
            logs.mkdir(exist_ok=False)
            for i in range(repetitions):
                roles = ('a1', 'a2') if phase == 'aa' else (('before', 'after') if i % 2 == 0 else ('after', 'before'))
                for role in roles:
                    binary_role = 'before' if phase == 'aa' else role
                    exe = folder / binary_role / 'out/HttpServer.exe'
                    row = mod.benchmark(logs, role, exe, i, duration, concurrency)
                    row.update(allocator=allocator, phase=phase, concurrency=concurrency)
                    rows.append(row)
                    save(root / f'http-c{concurrency}.json', rows)


def memory(root, targets=TARGETS):
    rows = []
    for target in targets:
        for role in ('before', 'after'):
            build_micro(root, role, target, 'HeaderMemoryBench')
            for i in range(3):
                output = command([root / role / target / 'HeaderMemoryBench.exe', root / 'fixtures/ascii.bin']).decode('utf-8', errors='replace')
                for m in re.finditer(r'MEM kind=(\w+) n=(\d+) live_bytes=(\d+) per_object=([\d.]+) released_delta=(-?\d+)', output):
                    rows.append({'target': target, 'role': role, 'sample': i, 'kind': m[1], 'count': int(m[2]), 'live_bytes': int(m[3]), 'bytes_per_object': float(m[4]), 'released_delta': int(m[5])})
                if output.count('MEM ') != 2:
                    raise RuntimeError(output)
                print(target, role, output.strip(), flush=True)
    save(root / 'memory.json', rows)


def verify_inputs(root):
    manifest = json.loads((root / 'source-manifest.json').read_text(encoding='utf-8'))
    for role, files in manifest.items():
        for name, expected in files.items():
            path = root / role / 'source' / name
            if sha(path.read_bytes()) != expected:
                raise RuntimeError('冻结源码已变化: ' + str(path))
    frozen = root / 'harness-manifest.json'
    if frozen.exists():
        frozen_manifest = json.loads(frozen.read_text(encoding='utf-8'))
        if sha(Path(__file__).read_bytes()) != frozen_manifest['run_bench.py']:
            raise RuntimeError('当前 Python 驱动与本批冻结版本不同，请新建 label；不混用测量驱动')
        for name, expected in frozen_manifest.items():
            if sha((root / 'harness' / name).read_bytes()) != expected:
                raise RuntimeError('冻结基准驱动已变化: ' + name)


def validate(root, targets=TARGETS, cases=CASES):
    rows = []
    for target in targets:
        for mode, fixture in cases:
            for role in ('before', 'after'):
                exe = root / role / target / 'HeaderBench.exe'
                output = command([exe, mode, root / 'fixtures' / (fixture + '.bin'), '1', 'validate', '4']).decode('utf-8', errors='replace')
                valid = 'VALID_ONLY' in output
                if not valid and 'SKIP invalid_output' not in output:
                    raise RuntimeError(output)
                if role == 'after' and not valid:
                    raise RuntimeError(f'候选语义检查失败 {target}/{mode}/{fixture}/{role}: ' + output)
                rows.append({'target': target, 'mode': mode, 'fixture': fixture, 'role': role, 'valid': valid, 'output': output})
    save(root / 'validation.json', rows)
    print('VALIDATION', len(rows), 'valid=', sum(x['valid'] for x in rows), flush=True)


def paired_decision(ratios, aa_ratios, seed=199, draws=5000):
    if not ratios:
        return {'status': 'undetermined', 'reason': '没有有效配对'}
    rng = random.Random(seed)
    medians = sorted(statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(draws))
    lower = medians[max(0, math.ceil(draws * 0.05) - 1)]
    upper = medians[max(0, math.ceil(draws * 0.95) - 1)]
    median = statistics.median(ratios)
    aa_bias = statistics.median(aa_ratios) - 1 if aa_ratios else None
    stable = aa_bias is not None and abs(aa_bias) <= 0.01
    status = ('not_slower' if median <= 1 and upper <= 1 else
              ('regressed' if median > 1 and lower > 1 else 'undetermined'))
    if not stable:
        status = 'undetermined'
    return {'status': status, 'median_ratio': median, 'lower_one_sided_95': lower,
            'upper_one_sided_95': upper, 'aa_median_bias': aa_bias, 'aa_stable': stable,
            'seed': seed, 'draws': draws, 'pairs': len(ratios)}


def parse_cases(value):
    if not value or value == 'all':
        return CASES
    selected = []
    for item in value.split(','):
        if ':' not in item:
            raise ValueError('case 应为 mode:fixture')
        case = tuple(item.split(':', 1))
        if case not in AVAILABLE_CASES:
            raise ValueError('未知 case: ' + item)
        selected.append(case)
    return selected


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='backslashreplace')
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase', choices=['prepare', 'build', 'validate', 'micro', 'http', 'memory'])
    p.add_argument('--label', required=True)
    p.add_argument('--before', default='5e63745^')
    p.add_argument('--before-source', type=Path)
    p.add_argument('--reference-label', help='明确参考身份，例如 A、B 或上一候选，不从目录名推断')
    p.add_argument('--candidate-label', help='候选身份，例如 C01')
    p.add_argument('--after-source', type=Path)
    p.add_argument('--before-bytes', choices=['auto', 'yes', 'no'], default='auto')
    p.add_argument('--after-bytes', choices=['auto', 'yes', 'no'], default='auto')
    p.add_argument('--targets', nargs='+', choices=TARGETS, default=TARGETS)
    p.add_argument('--cases', default='all', help='逗号分隔 mode:fixture 或 all；encode-legacy/field-encode 仅显式可选')
    p.add_argument('--window-ms', type=int, default=250)
    p.add_argument('--aa-samples', type=int, default=5)
    p.add_argument('--samples', type=int, default=7)
    p.add_argument('--duration', type=int, default=10)
    p.add_argument('--concurrency', type=int, default=128)
    p.add_argument('--skip-http', action='store_true', help='准备/构建时暂不编译正式服务')
    p.add_argument('--only-http', action='store_true', help='build 阶段只编译正式服务')
    o = p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', o.label):
        p.error('invalid label')
    if o.samples <= 0 or o.aa_samples < 0 or o.window_ms <= 0:
        p.error('samples/window 必须为正，aa-samples 不得为负')
    try:
        cases = parse_cases(o.cases)
    except ValueError as e:
        p.error(str(e))
    ensure_idle()
    root = HERE / 'bin' / o.label
    if o.phase == 'prepare':
        prepare(root, command(['git', 'rev-parse', o.before]).decode().strip(),
                o.before_source.resolve(strict=True) if o.before_source else None,
                o.after_source.resolve(strict=True) if o.after_source else None,
                o.before_bytes, o.after_bytes, o.targets, o.skip_http, o.reference_label, o.candidate_label)
    elif o.phase == 'build':
        build(root, o.targets, o.skip_http, o.only_http)
    elif o.phase == 'validate':
        verify_inputs(root)
        validate(root, o.targets, cases)
        verify_inputs(root)
    elif o.phase == 'micro':
        verify_inputs(root)
        micro(root, o.samples, o.targets, cases, o.window_ms, o.aa_samples)
        verify_inputs(root)
    elif o.phase == 'memory':
        verify_inputs(root)
        memory(root, o.targets)
        verify_inputs(root)
    else:
        verify_inputs(root)
        http(root, o.samples, o.duration, o.concurrency, o.aa_samples)
        verify_inputs(root)


if __name__ == '__main__':
    main()
