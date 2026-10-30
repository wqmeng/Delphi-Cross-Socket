"""复用 HttpHeadTests 的真实 TCP 客户端，验证额外尾部不污染后续请求。"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'HttpHeadTests'))
import run_response_tests as response

CASES = {
    'normal': ('response-reuse', b'HTTP/1.1 200 OK', b'Content-Length: 2\r\n', b'ok'),
    'tail-crlf': ('response-close', b'HTTP/1.1 200 OK', b'Content-Length: 2\r\n', b'ok\r\n'),
    'tail-response': ('response-close', b'HTTP/1.1 200 OK', b'Content-Length: 2\r\n',
                      b'okHTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nevil'),
    'tail-partial': ('response-close', b'HTTP/1.1 200 OK', b'Content-Length: 2\r\n', b'okHTTP/1.1'),
    'interim-tail': ('response-close', b'HTTP/1.1 103 Early Hints', b'',
                     b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\nX-Connection-Id: 1\r\n\r\nok\r\n'),
}


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='backslashreplace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--report', required=True, type=Path)
    options = parser.parse_args()
    exe = options.executable.resolve(strict=True)
    response.CASES.update(CASES)
    results = []
    for name in CASES:
        try:
            item = response.run(exe, name)
        except Exception as error:
            item = {'case': name, 'passed': False, 'output': str(error)}
        results.append(item)
        print('PASS' if item['passed'] else 'FAIL', name, item, flush=True)
    report = {'binary_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'passed': sum(x['passed'] for x in results), 'total': len(results), 'results': results}
    options.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    raise SystemExit(int(report['passed'] != report['total']))
