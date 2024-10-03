#! /usr/bin/python3

import subprocess


def get_git_hash():
    ret = subprocess.run(["git", "diff", "--quiet"], stdout=subprocess.PIPE, text=True)
    add = '*' if ret.returncode != 0 else ''

    ret = subprocess.run(["git", "rev-parse", "--short", "HEAD"], stdout=subprocess.PIPE, text=True)
    return ret.stdout.strip() + add

fh = open('version.cpp', 'w')
fh.write('const char *version_str = "' + get_git_hash() + '";\n')
fh.close()


###

Import("env")
from os.path import join
platform = env.PioPlatform()
FRAMEWORK_DIR = platform.get_package_dir("framework-arduinopico")
env.Append(CPPPATH=[
    join(FRAMEWORK_DIR, "pico-sdk", "src", "rp2_common", "pico_aon_timer", "include")
])
env.BuildSources(
    join("$BUILD_DIR", "PicoAON"),
    join(FRAMEWORK_DIR, "pico-sdk", "src", "rp2_common", "pico_aon_timer")
)

###

print('prepare.py finished')
