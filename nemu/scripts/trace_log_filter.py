"""
从 nemu-log.txt 中筛选出所有指定类型的 trace 部分的日志并打印出来。
"""

import sys


def main() -> None:
    if len(sys.argv) < 3:
        print("Usage: python trace_log_filter.py <trace_type> <path_to_nemu_log_txt>")
        sys.exit(1)
    trace_type = sys.argv[1]
    with open(sys.argv[2], "r") as f:
        content = f.read()
        lines = content.splitlines()
        filtered_lines = [
            line for line in lines if line.startswith(f"[{trace_type}] ")
        ]
        print("\n".join(filtered_lines))


if __name__ == "__main__":
    main()
