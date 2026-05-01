#!/bin/bash
# deploy.sh — 部署 compiler API 到云服务器
# 用法: bash deploy.sh
# 需要先在本地能 ssh 到服务器

SERVER="wuji@igw.netperf.cc"
PORT=2123
REMOTE_DIR="~/XJTU-Compiler"

echo "=== 1. 上传 compiler/ 到服务器 ==="
scp -P $PORT -r compiler/ $SERVER:$REMOTE_DIR/compiler/

echo "=== 2. 编译 + 启动服务 ==="
ssh -p $PORT $SERVER << 'EOF'
cd ~/XJTU-Compiler/compiler

# 编译
make clean && make
if [ $? -ne 0 ]; then echo "编译失败"; exit 1; fi

# 停掉旧的 server
pkill -f "python3 server.py" 2>/dev/null
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
