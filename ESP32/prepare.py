#! /usr/bin/python3

import json
import subprocess
import sys


def get_git_hash():
    ret = subprocess.run(["git", "diff", "--quiet"], stdout=subprocess.PIPE, text=True)
    add = '*' if ret.returncode != 0 else ''

    ret = subprocess.run(["git", "rev-parse", "--short", "HEAD"], stdout=subprocess.PIPE, text=True)
    return ret.stdout.strip() + add

fh = open('version.cpp', 'w')
fh.write('const char *version_str = "' + get_git_hash() + '";\n')
fh.close()

# lint of configuration
try:
    j = json.loads(open('data/cfg-iESP.json', 'r').read())
except json.decoder.JSONDecodeError as je:
    print(f'data/cfg-iESP.json is invalid, please check ({je})')
    sys.exit(1)
