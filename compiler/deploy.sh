#!/bin/bash
# deploy.sh — 部署 compiler API 到云服务器
# 用法: bash deploy.sh
# 需要先在本地能 ssh 到服务器

set -e

SERVER="wuji@igw.netperf.cc"
PORT=2123
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REMOTE_COMPILER_DIR="compiler"
SSH_BIN="${SSH_BIN:-ssh}"
SCP_BIN="${SCP_BIN:-scp}"

if [ -n "${WSL_DISTRO_NAME:-}" ]; then
    if command -v ssh.exe >/dev/null 2>&1; then SSH_BIN="ssh.exe"; fi
    if command -v scp.exe >/dev/null 2>&1; then SCP_BIN="scp.exe"; fi
fi

echo "=== 1. 准备远端目录 ==="
"$SSH_BIN" -p "$PORT" "$SERVER" "mkdir -p ~/$REMOTE_COMPILER_DIR"

echo "=== 2. 上传 compiler/ 内容到服务器 ==="
(
    cd "$SCRIPT_DIR"
    "$SCP_BIN" -P "$PORT" -r Makefile server.py deploy.sh data include src tests tools "$SERVER:$REMOTE_COMPILER_DIR/"
)

echo "=== 3. 编译 + 启动服务 ==="
"$SSH_BIN" -p "$PORT" "$SERVER" << 'EOF'
cd ~/compiler

# 编译
make clean && make
if [ $? -ne 0 ]; then echo "编译失败"; exit 1; fi

# 停掉旧的 server
pkill -f "[p]ython3 server.py" 2>/dev/null || true
sleep 1

# 启动新的 server (后台运行)
nohup python3 server.py --port 8080 > server.log 2>&1 &
sleep 2

# 验证
if curl -s http://localhost:8080/api/health | grep -q '"ok"'; then
    echo "=== 服务启动成功 ==="
    echo "API: http://igw.netperf.cc:8080/api/health"
else
    echo "=== 服务启动失败，查看日志 ==="
    tail -20 server.log
fi
EOF
