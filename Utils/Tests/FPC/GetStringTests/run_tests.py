"""GetString 行为差分及交替性能比较；构建参数沿用 HttpHotPathTests。"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import statistics
import subprocess
import sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
DELPHI = Path(r'D:\Design\Delphi\D13.1')
FPC = Path(r'D:\Design\FreePascal\fpc\bin\x86_64-win64\ppcx64.exe')
SCENARIOS = ['ascii8', 'ascii32', 'ascii128', 'ascii1k', 'ascii64k',
             'chinese1k', 'mixed1k', 'mixed64k', 'explicit32', 'explicit1k',
             'bom1k', 'utf16le', 'utf16be', 'fallback', 'memory', 'stream', 'custom']


def execute(args, timeout=180):
    p = subprocess.run(list(map(str, args)), cwd=HERE, capture_output=True,
                       timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
    raw = p.stdout + p.stderr
    try:
        output = raw.decode('utf-8')
    except UnicodeDecodeError:
        output = raw.decode('gb18030', errors='replace')
    if p.returncode:
        raise RuntimeError(output[-10000:])
    return output


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def build(target, label, source, checked):
    out = HERE / 'bin' / label / target
    units = HERE / 'lib' / label / target
    out.mkdir(parents=True, exist_ok=True)
    units.mkdir(parents=True, exist_ok=True)
    search = [source, REPO / 'Utils']
    if target == 'fpc64':
        search += [REPO / 'DelphiToFPC', Path(r'D:\Design\FreePascal\lazarus\components\lazutils')]
        args = [FPC, '-B', '-O2'] + ['-Fu' + str(p) for p in search]
        args += ['-Fi' + str(REPO), '-FE' + str(out), '-FU' + str(units)]
        if checked:
            args += ['-Cr', '-Co']
        args += [HERE / 'GetStringTests.lpr']
    else:
        args = [DELPHI / 'bin' / ('dcc64.exe' if target == 'win64' else 'dcc32.exe'),
                '-Q', '-B', '-$O+', '-GD', '-NSSystem;System.Win;Winapi',
                '-U' + ';'.join(map(str, search + [DELPHI / 'lib' / target / 'release'])),
                '-I' + str(REPO), '-E' + str(out), '-N0' + str(units)]
        if checked:
            args += ['-$R+', '-$Q+']
        args += [HERE / 'GetStringTests.dpr']
    source_hash = sha(source / 'Utils.Utils.pas')
    try:
        output = execute(args)
    except RuntimeError as e:
        (out / 'build.log').write_text(str(e), encoding='utf-8')
        raise
    (out / 'build.log').write_text(output, encoding='utf-8')
    if sha(source / 'Utils.Utils.pas') != source_hash:
        raise RuntimeError('source changed during compilation')
    metadata = {'args': list(map(str, args)), 'source_sha256': source_hash,
                'runner_sha256': sha(Path(__file__)),
                'binary_sha256': sha(out / 'GetStringTests.exe'),
                'tests': {p.name: sha(p) for p in HERE.iterdir() if p.suffix in ('.pas', '.inc', '.lpr', '.dpr')},
                'compiler': str(args[0]), 'checked': checked}
    (out / 'source.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    print('BUILD PASS', label, target, flush=True)


def invoke(target, label, *args):
    return execute([HERE / 'bin' / label / target / 'GetStringTests.exe', *args])


def sample(target, label, scenario, count, alloc=False):
    output = invoke(target, label, 'bench', scenario, str(count), 'alloc' if alloc else 'time')
    m = re.search(r'us=([\d.]+) calls=(\d+) bytes=(\d+) checksum=(\d+)', output)
    if not m:
        raise RuntimeError(output)
    return dict(us=float(m[1]), calls=int(m[2]), bytes=int(m[3]), checksum=int(m[4]))


def compare(target, before, after, scenarios, rounds, duration, tag):
    manifests = [json.loads((HERE / 'bin' / label / target / 'source.json').read_text()) for label in (before, after)]
    if manifests[0]['tests'] != manifests[1]['tests'] or any(m['checked'] for m in manifests):
        raise RuntimeError('comparison requires identical test sources and release builds')
    for label, manifest in zip((before, after), manifests):
        if sha(HERE / 'bin' / label / target / 'GetStringTests.exe') != manifest['binary_sha256']:
            raise RuntimeError('binary changed since compilation')
    rows = []
    for name in scenarios:
        probe = sample(target, before, name, 1000)
        count = max(100, min(3000000, int(1000 * duration * 1e6 / max(probe['us'], 1))))
        samples = []
        for i in range(rounds + 1):
            pair = {'warmup': i == 0}
            order = [('before', before), ('after', after)]
            if i % 2:
                order.reverse()
            for key, label in order:
                pair[key] = sample(target, label, name, count)
            if pair['before']['checksum'] != pair['after']['checksum']:
                raise RuntimeError(f'benchmark checksum mismatch: {name}')
            samples.append(pair)
        b = statistics.median(x['before']['us'] for x in samples[1:])
        a = statistics.median(x['after']['us'] for x in samples[1:])
        row = dict(name=name, count=count, samples=samples,
                   before_us=b, after_us=a, change_percent=(a / b - 1) * 100,
                   paired_change_percent=statistics.median((x['after']['us'] / x['before']['us'] - 1) * 100 for x in samples[1:]),
                   allocations={key: sample(target, label, name, 1000, True) for key, label in [('before', before), ('after', after)]})
        rows.append(row)
        print(target, name, f'{b / count * 1000:.1f}->{a / count * 1000:.1f} ns/call {row["change_percent"]:+.1f}%',
              'alloc', row['allocations']['before']['calls'], '->', row['allocations']['after']['calls'], flush=True)
        dest = HERE / 'bin' / after / target / f'{tag}.json'
        dest.write_text(json.dumps(dict(before=before, after=after, rounds=rounds,
                                      target_seconds=duration, affinity_mask=4, rows=rows), indent=2), encoding='utf-8')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='backslashreplace')
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--label', required=True)
    p.add_argument('--targets', nargs='+', default=['win64', 'win32', 'fpc64'])
    p.add_argument('--checked', action='store_true')
    p.add_argument('--no-build', action='store_true')
    p.add_argument('--bench-only', action='store_true')
    p.add_argument('--before')
    p.add_argument('--scenarios', nargs='+', default=SCENARIOS)
    p.add_argument('--rounds', type=int, default=7)
    p.add_argument('--duration', type=float, default=0.04)
    p.add_argument('--tag', default='benchmarks')
    p.add_argument('--source-root', type=Path)
    p.add_argument('--complete-boolean', action='store_true')
    p.add_argument('--case', choices=['test', 'guard'], default='test')
    o = p.parse_args()
    for label in (o.label, o.before or 'none', o.tag):
        if not re.fullmatch(r'[A-Za-z0-9_-]+', label):
            p.error('invalid label')
    snapshot = HERE / 'bin' / o.label / 'source'
    if not o.no_build:
        snapshot.mkdir(parents=True, exist_ok=True)
        src = (o.source_root if o.source_root else REPO / 'Utils') / 'Utils.Utils.pas'
        dst = snapshot / src.name
        source_bytes = src.read_bytes()
        if o.complete_boolean:
            source_bytes = source_bytes.replace(b'{$I zLib.inc}', b'{$I zLib.inc}\r\n{$B+}', 1)
        if dst.exists() and dst.read_bytes() != source_bytes:
            p.error('label already contains a different source snapshot; use a new label')
        dst.write_bytes(source_bytes)
    for target in o.targets:
        if not o.no_build:
            build(target, o.label, snapshot, o.checked)
        if not o.bench_only:
            try:
                output = invoke(target, o.label, o.case)
            except RuntimeError as error:
                (HERE / 'bin' / o.label / target / 'failure.txt').write_text(str(error), encoding='utf-8')
                raise
            if 'PASS checks=' not in output:
                raise RuntimeError(output)
            (HERE / 'bin' / o.label / target / 'results.txt').write_text(output, encoding='utf-8')
            print(o.label, target, output.strip(), flush=True)
        if o.before:
            if o.checked:
                p.error('checked builds must not be benchmarked')
            compare(target, o.before, o.label, o.scenarios, o.rounds, o.duration, o.tag)
