#!/usr/bin/env python3
"""Logging — silent by default, --verbose enables console output.
Crash logs are always written regardless of verbose setting."""
import sys
import os
import time
import glob
import traceback

VERBOSE = False
_log_file = None
_log_dir = None
MAX_LOG_FILES = 10


def _log_path():
    d = _log_dir or os.path.join(os.environ.get("APPDATA", "."), "MecchaCamouflage", "logs")
    os.makedirs(d, exist_ok=True)
    return os.path.join(d, time.strftime("%Y%m%d_%H%M%S") + ".log")


def _rotate():
    if not _log_dir:
        return
    pattern = os.path.join(_log_dir, "*.log")
    files = sorted(glob.glob(pattern))
    while len(files) > MAX_LOG_FILES:
        try:
            os.remove(files[0])
            files = files[1:]
        except Exception:
            break


def set_log_dir(path):
    global _log_dir
    _log_dir = path
    os.makedirs(path, exist_ok=True)
    _rotate()


def get_log_dir():
    return _log_dir or os.path.join(os.environ.get("APPDATA", "."), "MecchaCamouflage", "logs")


def init():
    global _log_file, _log_dir
    _log_dir = get_log_dir()
    os.makedirs(_log_dir, exist_ok=True)
    _rotate()
    try:
        _log_file = open(_log_path(), "w", encoding="utf-8")
    except Exception:
        _log_file = None


def enable():
    global VERBOSE
    VERBOSE = True


def disable():
    global VERBOSE
    VERBOSE = False


def _fmt(msg):
    return f"[{time.strftime('%H:%M:%S')}] {msg}"


def _write(line):
    if _log_file:
        _log_file.write(line + "\n")
        _log_file.flush()


def info(msg):
    line = _fmt(f"INFO  {msg}")
    if VERBOSE:
        print(line)
    _write(line)


def warn(msg):
    line = _fmt(f"WARN  {msg}")
    if VERBOSE:
        print(line)
    _write(line)


def error(msg):
    line = _fmt(f"ERROR {msg}")
    if VERBOSE or True:
        print(line)
    _write(line)


def debug(msg):
    if not VERBOSE:
        return
    line = _fmt(f"DEBUG {msg}")
    print(line)
    _write(line)


def crash(msg):
    """Write a crash entry even without verbose, always."""
    line = _fmt(f"CRASH {msg}")
    print(line)
    _write(line)
    _write("Stack trace:")
    for tb_line in traceback.format_stack():
        _write("  " + tb_line.rstrip())
