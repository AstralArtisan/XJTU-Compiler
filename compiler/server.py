#!/usr/bin/env python3
"""Lightweight API server wrapping the compiler binary.

Usage:
    cd compiler && make && python3 server.py [--port 8080]

Endpoints:
    POST /api/scan   { "source": "int x = 1;" }
    POST /api/dfa    { "action": "test",      "dfa_file": "data/simple.dfa", "input": "aab" }
    POST /api/dfa    { "action": "enumerate",  "dfa_file": "data/simple.dfa", "max_len": 3 }
    POST /api/dfa    { "action": "json",       "dfa_file": "data/simple.dfa" }
    GET  /api/health

All responses are JSON. CORS enabled for GitHub Pages.
"""

import json
import os
import subprocess
import sys
import tempfile
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT = 8080
COMPILER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'compiler')
ALLOWED_DFA_FILES = {'data/simple.dfa', 'data/lexer.dfa', 'data/stmt.dfa', 'tests/dfa/lab1_basic.dfa'}

CORS_HEADERS = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'POST, GET, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
}


def run_compiler(args, stdin_data=None, timeout=5):
    try:
        result = subprocess.run(
            [COMPILER] + args,
            input=stdin_data,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=os.path.dirname(os.path.abspath(__file__))
        )
        return result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired:
        return '', 'timeout', -1
    except FileNotFoundError:
        return '', 'compiler binary not found', -1


class Handler(BaseHTTPRequestHandler):
    def _cors(self):
        for k, v in CORS_HEADERS.items():
            self.send_header(k, v)

    def _json_response(self, code, data):
        body = json.dumps(data, ensure_ascii=False).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self._cors()
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.end_headers()

    def do_GET(self):
        if self.path == '/api/health':
            self._json_response(200, {'status': 'ok', 'compiler': os.path.exists(COMPILER)})
        else:
            self._json_response(404, {'error': 'not found'})

    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        raw = self.rfile.read(length) if length else b'{}'
        try:
            body = json.loads(raw)
        except json.JSONDecodeError:
            self._json_response(400, {'error': 'invalid JSON'})
            return

        if self.path == '/api/scan':
            self._handle_scan(body)
        elif self.path == '/api/dfa':
            self._handle_dfa(body)
        else:
            self._json_response(404, {'error': 'not found'})

    def _handle_scan(self, body):
        source = body.get('source', '')
        if not source:
            self._json_response(400, {'error': 'missing source'})
            return
        if len(source) > 50000:
            self._json_response(400, {'error': 'source too large (max 50KB)'})
            return

        with tempfile.NamedTemporaryFile(mode='w', suffix='.c', delete=False) as f:
            f.write(source)
            tmp = f.name

        try:
            out, err, code = run_compiler(['scan', '-f', tmp, '--format=json'])
            if code == 0 and out.strip():
                tokens = json.loads(out)
                self._json_response(200, {'tokens': tokens, 'errors': err.strip()})
            else:
                self._json_response(200, {'tokens': [], 'errors': err.strip()})
        except json.JSONDecodeError:
            self._json_response(500, {'error': 'invalid compiler output'})
        finally:
            os.unlink(tmp)

    def _handle_dfa(self, body):
        action = body.get('action', 'json')
        dfa_file = body.get('dfa_file', '')

        if dfa_file and dfa_file not in ALLOWED_DFA_FILES:
            self._json_response(400, {'error': 'dfa_file not allowed'})
            return

        if action == 'json':
            if not dfa_file:
                self._json_response(400, {'error': 'missing dfa_file'})
                return
            out, err, code = run_compiler(['dfa', dfa_file, '--format=json'])
            if code == 0 and out.strip():
                try:
                    self._json_response(200, json.loads(out))
                except json.JSONDecodeError:
                    self._json_response(500, {'error': 'invalid compiler output'})
            else:
                self._json_response(500, {'error': err.strip()})

        elif action == 'test':
            input_str = body.get('input', '')
            if not dfa_file:
                self._json_response(400, {'error': 'missing dfa_file'})
                return
            out, err, code = run_compiler(['dfa', dfa_file, '--test', input_str])
            accepted = 'ACCEPT' in out
            self._json_response(200, {'input': input_str, 'accepted': accepted, 'output': out.strip()})

        elif action == 'enumerate':
            max_len = body.get('max_len', 3)
            if not dfa_file:
                self._json_response(400, {'error': 'missing dfa_file'})
                return
            out, err, code = run_compiler(['dfa', dfa_file, '--enumerate', str(max_len)])
            lines = [l for l in out.strip().split('\n') if l and not l.startswith('Total')]
            total_line = [l for l in out.strip().split('\n') if l.startswith('Total')]
            self._json_response(200, {'strings': lines, 'total': total_line[0] if total_line else ''})

        else:
            self._json_response(400, {'error': 'unknown action: ' + action})

    def log_message(self, fmt, *args):
        sys.stderr.write('[api] %s\n' % (fmt % args))


if __name__ == '__main__':
    port = PORT
    for i, arg in enumerate(sys.argv[1:], 1):
        if arg == '--port' and i < len(sys.argv) - 1:
            port = int(sys.argv[i + 1])

    print(f'Compiler API server starting on port {port}')
    print(f'Compiler binary: {COMPILER}')
    print(f'Endpoints: POST /api/scan, POST /api/dfa, GET /api/health')
    server = HTTPServer(('0.0.0.0', port), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nShutting down.')
        server.server_close()
