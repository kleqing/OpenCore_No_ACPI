#coding:utf-8
#!/usr/bin/env python3

"""
Validates schema files (e.g. OcConfigurationLib.c) for being sorted.
"""

import re
import sys

if len(sys.argv) < 2:
    print('传递文件以检查')
    sys.exit(-1)

with open(sys.argv[1], 'r', encoding='utf-8') as f:
    prev = ''
    content = [line.strip() for line in f.readlines()]
    for index, line in enumerate(content):
        if line == 'OC_SCHEMA':
            print('检查架构 {}'.format(re.match(r'^\w+', content[index + 1]).group(0)))
            prev = ''
            continue
        x = re.search(r'"([^"]+)"', line)
        if x:
            if x.group(1) < prev:
                print(f'错误: {prev} 先于 {x.group(1)}')
                sys.exit(1)
            prev = x.group(1)
