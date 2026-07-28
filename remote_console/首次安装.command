#!/bin/zsh
set -e

SCRIPT_DIR="${0:A:h}"
cd "$SCRIPT_DIR"

echo "正在安装灌溉控制台依赖……"
npm install
echo
echo "安装完成。以后双击“启动灌溉控制台.command”即可使用。"
read "?按回车键关闭窗口。"
