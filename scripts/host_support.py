"""Portable compiler selection for isolated host validation, never target builds."""
import os
import shutil
import sys


def non_pie_flags(platform=None):
    # Linux ASan shadow placement can require non-PIE. Apple's linker rejects
    # the Linux -no-pie spelling under -Werror; retain its supported default.
    return ['-fno-pie', '-no-pie'] if (platform or sys.platform).startswith('linux') else []


def analyzer_compilers():
    clang = os.getenv('CLANG') or shutil.which('clang')
    clangxx = os.getenv('CLANGXX') or shutil.which('clang++')
    if not clang or not clangxx:
        raise RuntimeError('Clang static analyzer unavailable; set CLANG and CLANGXX')
    return clang, clangxx
