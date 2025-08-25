"""
从 nemu-log.txt 中筛选出所有 ftrace 部分的日志并打印出来。
"""

import sys


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: python ftrace_log_filter.py <path_to_nemu_log_txt>")
        sys.exit(1)
    with open(sys.argv[1], "r") as f:
        content = f.read()
        lines = content.splitlines()
        filtered_lines = [
            line for line in lines if line.startswith("[ftrace] ")
        ]
        print("\n".join(filtered_lines))


if __name__ == "__main__":
    main()
