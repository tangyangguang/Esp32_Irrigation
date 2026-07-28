#!/bin/zsh
set -e

SCRIPT_DIR="${0:A:h}"
cd "$SCRIPT_DIR"

if ! command -v node >/dev/null 2>&1; then
  echo "没有找到 Node.js，请先安装 Node.js 20 或更高版本。"
  read "?按回车键关闭窗口。"
  exit 1
fi

if [ ! -d node_modules ]; then
  echo "首次运行，正在安装依赖……"
  npm install
fi

npm start &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT INT TERM

for attempt in {1..30}; do
  if curl --silent --fail http://127.0.0.1:8787/api/status >/dev/null; then
    open http://127.0.0.1:8787/
    echo "灌溉控制台已启动。关闭这个终端窗口即可停止本地服务。"
    wait "$SERVER_PID"
    exit $?
  fi
  sleep 0.2
done

echo "控制台启动失败，请查看上方错误信息。"
wait "$SERVER_PID"
