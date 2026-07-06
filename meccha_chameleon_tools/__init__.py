#!/usr/bin/env python3
"""
MECCHA CHAMELEON Box ESP — Entry Point
Fully external box ESP for MECCHA CHAMELEON (Steam / UE5.6).
"""
import sys
import os
import ctypes

from PyQt5.QtWidgets import QApplication, QMessageBox
from PyQt5.QtCore import QTimer

# Re-export for backward compatibility with debug scripts
from meccha_chameleon_tools.core import (
    MecchaESP, rp, ru32, ru16, rfloat, wfloat, rvec3, rvec3_f,
    read_array, read_tarray_ptr, dist, OFFSETS,
    PatternScanner, FNameResolver, UObjectArray, OffsetResolver,
)
from meccha_chameleon_tools.config import Config, load_config, save_config, CONFIG_FILE
from meccha_chameleon_tools.translations import _tr
from meccha_chameleon_tools.ui import Menu, Overlay


# Default game directory - user can override via config
_DEFAULT_GAME_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\MECCA CHAMELEON\Chameleon\Binaries\Win64"

def get_game_dir(config=None):
    """Get game directory from config or default."""
    if config and hasattr(config, "game_directory") and config.game_directory:
        return config.game_directory
    return _DEFAULT_GAME_DIR


def detect_system_language():
    try:
        lcid = ctypes.windll.kernel32.GetUserDefaultUILanguage()
        lang_id = lcid & 0x3FF
        if lang_id == 0x04:
            return "CN"
        return "EN"
    except Exception:
        return "EN"


def _set_dpi_aware():
    try:
        ctypes.windll.user32.SetProcessDpiAwarenessContext(-4)
    except Exception:
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass


def main():
    _set_dpi_aware()
    app = QApplication(sys.argv)

    config = load_config()
    if config.language == "EN" and not os.path.exists(CONFIG_FILE):
        detected = detect_system_language()
        if detected != "EN":
            config.language = detected
    _tr.set_language(config.language)

    esp = None
    try:
        esp = MecchaESP()
    except Exception:
        pass

    menu = Menu(config, esp)
    overlay = Overlay(esp, config)
    overlay.show()
    menu.show()

    ret = app.exec_()
    save_config(config)
    if esp:
        esp.cleanup()
    sys.exit(ret)


if __name__ == "__main__":
    main()
