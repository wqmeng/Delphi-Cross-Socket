"""重放 #199 的原始 HTTP 字节，保存服务端行为；不使用 HTTP 客户端重编码。"""
from pathlib import Path
import argparse
import hashlib
import json
import queue
import re
import socket
import subprocess
import sys
import threading
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
DELPHI = Path(r'D:\Design\Delphi\D13.1')
CN = Path(r'D:\Design\Delphi\VclFmxRtl\cncrypto\Source')
FPC = Path(r'D:\Design\FreePascal\fpc\bin\x86_64-win64\ppcx64.exe')
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
FOLLOWUP = b'GET /after HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n'


def sha(data):
    return hashlib.sha256(data).hexdigest()


# request.hex 是报告中的固定线上字节；先校验哈希和头/体尺寸，禁止经文本读写重编码。
def fixture():
    data = bytes.fromhex((HERE / 'request.hex').read_text(encoding='ascii'))
    metadata = json.loads((HERE / 'fixture.json').read_text(encoding='utf-8'))
    assert sha(data) == metadata['sha256'], '原始报文哈希不符'
    header, body = data.split(b'\r\n\r\n', 1)
    assert len(data) == 865 and len(header) + 4 == 700 and len(body) == 165
    assert b'Content-Length: 165\r\n' in header
    return data


# 每个目标使用独立 units 目录并全量构建，记录源码/二进制哈希；调用方的 label 隔离历史证据。
# Release 参数参考用户脚本，Delphi 的 D+/L+/Y+ 仍保留调试资料，不等于“关闭全部调试信息”。
def build(target, output):
    output.mkdir(parents=True, exist_ok=True)
    units = output / 'units'
    units.mkdir()
    search = [REPO / 'Net', REPO / 'Utils', CN]
    if target == 'fpc64':
        search += [REPO / 'DelphiToFPC', Path(r'D:\Design\FreePascal\lazarus\components\lazutils')]
        args = [FPC, '-B', '-O3', '-CX', '-Xs', '-XX'] + ['-Fu' + str(p) for p in search]
        args += ['-Fi' + str(REPO), '-FE' + str(output), '-FU' + str(units), HERE / 'Issue199Server.lpr']
    else:
        compiler = 'dcc64.exe' if target == 'win64' else 'dcc32.exe'
        args = [DELPHI / 'bin' / compiler, '-Q', '-B', '-$C-', '-$D+', '-$I-', '-$L+', '-$O+', '-$Q-', '-$R-', '-$Y+', '-NSSystem;System.Win;Winapi',
                '-U' + ';'.join(map(str, search + [DELPHI / 'lib' / target / 'release'])),
                '-I' + str(REPO) + ';' + str(CN), '-E' + str(output), '-N0' + str(units),
                HERE / 'Issue199Server.dpr']
    result = subprocess.run(list(map(str, args)), cwd=HERE, capture_output=True,
                            timeout=120, creationflags=NO_WINDOW)
    (output / 'build.log').write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError((result.stdout + result.stderr).decode('utf-8', errors='replace')[-8000:])
    sources = [REPO / 'Net' / name for name in (
        'Net.CrossHttpServer.pas', 'Net.CrossHttpParser.pas', 'Net.CrossHttpParams.pas',
        'Net.CrossSocket.Base.pas', 'Net.CrossSocket.Iocp.pas')]
    sources += [HERE / name for name in ('Issue199Server.lpr', 'Issue199Server.dpr', 'run_tests.py', 'request.hex')]
    metadata = {'command': list(map(str, args)),
                'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
                'sources': {str(p.relative_to(REPO)): sha(p.read_bytes()) for p in sources},
                'binary_sha256': sha((output / 'Issue199Server.exe').read_bytes())}
    (output / 'build.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    print('BUILD OK', target, flush=True)
    return output / 'Issue199Server.exe'


def read_response(connection):
    data = bytearray()
    while b'\r\n\r\n' not in data:
        part = connection.recv(65536)
        if not part:
            raise RuntimeError('响应头前 EOF')
        data.extend(part)
    header, body = bytes(data).split(b'\r\n\r\n', 1)
    lines = header.split(b'\r\n')
    headers = dict(line.lower().split(b':', 1) for line in lines[1:])
    assert b'transfer-encoding' not in headers, '诊断服务端响应不应使用 chunked'
    size = int(headers.get(b'content-length', b'0').strip())
    while len(body) < size:
        part = connection.recv(65536)
        if not part:
            raise RuntimeError('响应正文前 EOF')
        body += part
        data.extend(part)
    assert len(body) == size, '响应有额外尾部数据'
    return int(lines[0].split()[1]), bytes(data), body


def request_fields(body):
    text = body.decode('utf-8', errors='replace')
    match = re.fullmatch(
        r'REQUEST number=(\d+) method=(\S+) path=(\S+) headers=(\d+) content_length=(-?\d+) '
        r'body_type=(\d+) body_nil=(\d) raw_body_nil=(\d) body_size=(\d+) raw_nul_index=(-?\d+)', text)
    if not match:
        return None
    names = ('number', 'method', 'path', 'headers', 'content_length', 'body_type',
             'body_nil', 'raw_body_nil', 'body_size', 'raw_nul_index')
    return {name: value if name in ('method', 'path') else int(value)
            for name, value in zip(names, match.groups())}


# payload 为原始 bytes，split=True 只在头/体边界分两次发送；不保证操作系统的 recv 分块形状。
# directory 保存本次收发和服务端诊断；同连接 FOLLOWUP 用来发现前次正文未消费造成的串包。
def replay(executable, directory, payload, split):
    directory.mkdir()
    (directory / 'sent.bin').write_bytes(payload)
    process = subprocess.Popen([str(executable), str(directory)], cwd=HERE,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, creationflags=NO_WINDOW)
    lines, messages = [], queue.Queue()

    def drain():
        for raw in process.stdout:
            line = raw.decode('utf-8', errors='replace').strip()
            lines.append(line)
            messages.put(line)

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()
    result = {'sent_bytes': len(payload), 'sent_sha256': sha(payload), 'send_mode': 'split' if split else 'whole'}
    sent = bytearray()
    try:
        deadline = time.monotonic() + 10
        port = None
        while time.monotonic() < deadline:
            line = messages.get(timeout=max(0.01, deadline - time.monotonic()))
            if line.startswith('READY '):
                port = int(line.split()[1])
                break
            if line.startswith('ERROR '):
                raise RuntimeError(line)
        assert port, '服务端未就绪'
        with socket.create_connection(('127.0.0.1', port), timeout=3) as connection:
            connection.settimeout(3)
            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            if split:
                boundary = payload.index(b'\r\n\r\n') + 4
                connection.sendall(payload[:boundary])
                sent.extend(payload[:boundary])
                time.sleep(0.1)
                try:
                    connection.sendall(payload[boundary:])
                    sent.extend(payload[boundary:])
                except OSError as error:
                    result['body_send_error'] = str(error)
            else:
                connection.sendall(payload)
                sent.extend(payload)
            status, raw, body = read_response(connection)
            result.update(status=status, response_body=body.decode('utf-8', errors='replace'))
            result['request'] = request_fields(body)
            (directory / 'response.bin').write_bytes(raw)
            if status == 200:
                connection.sendall(FOLLOWUP)
                sent.extend(FOLLOWUP)
                next_status, next_raw, next_body = read_response(connection)
                (directory / 'followup-response.bin').write_bytes(next_raw)
                result['followup_status'] = next_status
                result['followup_request'] = request_fields(next_body)
                result['followup_method_valid'] = (result['followup_request'] or {}).get('method') == 'GET'
                assert next_status == 200, '同连接后续请求失败'
            try:
                result['closed'] = connection.recv(1) == b''
            except (ConnectionResetError, ConnectionAbortedError):
                result['closed'] = True
            except socket.timeout:
                result['closed'] = False
    except Exception as error:
        result['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        try:
            if process.poll() is None:
                process.stdin.write(b'\n')
                process.stdin.flush()
                process.wait(timeout=8)
        except (OSError, subprocess.TimeoutExpired):
            process.kill()
            process.wait()
        reader.join(timeout=3)
        process.stdin.close()
        process.stdout.close()
        result['server_exit_code'] = process.returncode
        (directory / 'server.log').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        received_path = directory / 'received-1.bin'
        received = received_path.read_bytes() if received_path.exists() else b''
        result['received_bytes'] = len(received)
        result['received_matches_sent'] = received == bytes(sent)
        result['received_original_prefix_matches'] = received[:min(len(received), len(payload))] == payload[:min(len(received), len(payload))]
        result['received_full_request'] = received.startswith(payload)
        result['server_events'] = lines
        body_path = directory / 'body-1.bin'
        header_path = directory / 'header-1.bin'
        result['header_matches_original'] = header_path.exists() and header_path.read_bytes() == payload.split(b'\r\n\r\n', 1)[0] + b'\r\n\r\n'
        result['body_matches_original'] = body_path.exists() and body_path.read_bytes() == payload.split(b'\r\n\r\n', 1)[1]
        result['requests'] = next((int(line.split()[1]) for line in lines if line.startswith('TOTAL ')), None)
        # observation_ok 表示观测过程完整，也允许成功复现旧缺陷；
        # behavior_ok 才要求头、正文、后续 Method 全部正确，严格模式用它决定退出码。
        result['observation_ok'] = ('error' not in result and process.returncode == 0
                                    and result['received_original_prefix_matches'] and bool(received)
                                    and result.get('status') in (200, 400) and result.get('closed', False))
        if result.get('status') == 200:
            result['observation_ok'] &= (result['requests'] == 2 and result.get('request') is not None
                                         and result.get('followup_request') is not None
                                         and result['received_matches_sent'])
        if result.get('status') == 400:
            result['observation_ok'] &= result['requests'] == 0
        result['behavior_ok'] = (result['header_matches_original'] and result['body_matches_original'] and result.get('followup_method_valid', False)
                                 and result.get('followup_status') == 200
                                 and (result.get('request') or {}).get('method') == 'POST'
                                 and (result.get('request') or {}).get('headers') == 10
                                 and (result.get('request') or {}).get('content_length') == 165)
        (directory / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--targets', nargs='+', choices=['win64', 'win32', 'fpc64'], default=['win64', 'win32', 'fpc64'])
    parser.add_argument('--label', default=time.strftime('run-%Y%m%d-%H%M%S'))
    parser.add_argument('--require-valid-body', action='store_true',
                        help='要求正文和后续请求解析正确，否则返回非零；默认只要求观察过程完整')
    options = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', options.label):
        parser.error('label 只能含字母、数字、下划线或连字符')
    output = HERE / 'bin' / options.label
    output.mkdir(parents=True, exist_ok=False)
    original = fixture()
    chinese = '有模式窗口'.encode('utf-8')
    assert original.count(chinese) == 1
    cases = [('original-whole', original, False), ('original-split', original, True),
             ('ascii-control', original.replace(chinese, b'window'), False),
             ('gbk-control', original.replace(chinese, '有模式窗口'.encode('gbk')), False)]
    results = []
    for target in options.targets:
        target_dir = output / target
        exe = build(target, target_dir)
        for name, payload, split in cases:
            result = replay(exe, target_dir / name, payload, split)
            results.append(dict(target=target, case=name, **result))
            print(target, name, json.dumps({key: result.get(key) for key in (
                'status', 'requests', 'header_matches_original', 'body_matches_original', 'followup_method_valid', 'received_full_request', 'observation_ok', 'error')}, ensure_ascii=False), flush=True)
    (output / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    print('OBSERVATIONS', sum(item['observation_ok'] for item in results), '/', len(results),
          'CORRECT_BEHAVIOR', sum(item['behavior_ok'] for item in results), '/', len(results), flush=True)
    print('RESULTS', output / 'results.json', flush=True)
    return 0 if all(item['observation_ok'] and (not options.require_valid_body or item['behavior_ok'])
                    for item in results) else 1


if __name__ == '__main__':
    sys.exit(main())
