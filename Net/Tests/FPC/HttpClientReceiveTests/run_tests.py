"""Issue #218：真实客户端解析与可控物理发送完成回归。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
DELPHI = Path(r'D:\Design\Delphi\D13.1')
FPC = Path(r'D:\Design\FreePascal\fpc\bin\x86_64-win64')
LAZ = Path(r'D:\Design\FreePascal\lazarus')
SDK = Path.home() / 'Documents/Embarcadero/Studio/SDKs/ubuntu22.04.sdk'
CASES = ('normal', 'split', 'tail-crlf', 'tail-response', 'idle-data', 'callback-tail',
         'callback-normal', 'early-normal', 'early-tail', 'early-idle-data', 'early-close',
         'legacy-early', 'failure-window', 'timeout-window', 'eof', 'truncated',
         'stream-fail', 'stream-close', 'interim', 'complete-close')


def decode(raw):
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        return raw.decode('gb18030', errors='replace')


def build(target, label, source_root):
    out, units = HERE / 'bin' / label / target, HERE / 'lib' / label / target
    out.mkdir(parents=True, exist_ok=True)
    units.mkdir(parents=True, exist_ok=True)
    linux = 'linux' in target
    search = ([source_root] if source_root else []) + [REPO / 'Net', REPO / 'Utils']
    defines = 'CROSS_HTTP_SEND_TESTS;CROSS_HTTP_TIMEOUT_TESTS'
    if target.startswith('fpc'):
        args = [FPC / ('ppcrossx64.exe' if linux else 'ppcx64.exe'), '-B',
                '-dCROSS_HTTP_SEND_TESTS', '-dCROSS_HTTP_TIMEOUT_TESTS']
        if linux:
            args.append('-Tlinux')
        args += ['-Fu' + str(p) for p in search + [REPO / 'DelphiToFPC', LAZ / 'components/lazutils']]
        args += ['-Fi' + str(REPO), '-FE' + str(out), '-FU' + str(units), HERE / 'HttpClientReceiveTests.lpr']
    else:
        arch = 'linux64' if linux else ('win32' if target == 'delphi-win32' else 'win64')
        compiler = 'dcclinux64.exe' if linux else ('dcc32.exe' if arch == 'win32' else 'dcc64.exe')
        args = [DELPHI / 'bin' / compiler, '-Q', '-B', '-D' + defines,
                '-NSSystem;Posix' if linux else '-NSSystem;System.Win;Winapi',
                '-U' + ';'.join(map(str, search + [DELPHI / 'lib' / arch / 'release'])),
                '-I' + str(REPO), '-E' + str(out), '-N0' + str(units)]
        if linux:
            libs = [DELPHI / 'lib/linux64/release', SDK / 'usr/lib/x86_64-linux-gnu',
                    SDK / 'lib/x86_64-linux-gnu', SDK / 'usr/lib/gcc/x86_64-linux-gnu/11']
            args += ['--syslibroot:' + str(SDK), '--libpath:' + ';'.join(map(str, libs))]
        args.append(HERE / 'HttpClientReceiveTests.dpr')
    sources = [HERE / 'HttpClientReceiveTests.lpr']
    for name in ('Net.CrossHttpClient.pas', 'Net.CrossHttpParser.pas'):
        sources.append(source_root / name if source_root and (source_root / name).exists() else REPO / 'Net' / name)
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    result = subprocess.run(list(map(str, args)), cwd=HERE, capture_output=True, timeout=120,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    (out / 'build.log').write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(decode(result.stdout + result.stderr)[-6000:])
    if hashes != {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}:
        raise RuntimeError('构建期间源码变化')
    (out / 'source.json').write_text(json.dumps(hashes, indent=2), encoding='utf-8')
    print('BUILD PASS', target, flush=True)
    return out / ('HttpClientReceiveTests' if linux else 'HttpClientReceiveTests.exe')


def run(exe, cases, report):
    results = []
    for case in cases:
        try:
            p = subprocess.run([str(exe), case], cwd=exe.parent, capture_output=True, timeout=15,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            item = {'case': case, 'passed': p.returncode == 0, 'exitcode': p.returncode,
                    'output': decode(p.stdout + p.stderr).strip()}
        except Exception as error:
            item = {'case': case, 'passed': False, 'output': str(error)}
        results.append(item)
        print('PASS' if item['passed'] else 'FAIL', case, item['output'], flush=True)
    data = {'binary_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
            'passed': sum(x['passed'] for x in results), 'total': len(results), 'results': results}
    report.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    return data['passed'] == data['total']


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='backslashreplace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('targets', nargs='*')
    parser.add_argument('--label', required=True)
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--cases', nargs='+', choices=CASES, default=CASES)
    options = parser.parse_args()
    if not options.label.replace('-', '').replace('_', '').isalnum():
        parser.error('invalid label')
    if set(options.targets) - {'delphi-win64', 'delphi-win32', 'fpc-win64', 'delphi-linux64', 'fpc-linux64'}:
        parser.error('invalid target')
    if not options.targets and not options.exe:
        parser.error('provide targets or --exe')
    success = True
    if options.exe:
        exe = options.exe.resolve(strict=True)
        success = run(exe, options.cases, exe.parent / (options.label + '-results.json'))
    for target in options.targets:
        exe = build(target, options.label, options.source_root.resolve() if options.source_root else None)
        if 'linux' not in target:
            success = run(exe, options.cases, exe.parent / 'results.json') and success
    raise SystemExit(0 if success else 1)
