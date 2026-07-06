#!/usr/bin/env python3
import sys, os, ctypes, time

from PyQt5.QtWidgets import QApplication, QMessageBox
from PyQt5.QtCore import QTimer

from meccha_chameleon_tools.core import MecchaESP
from meccha_chameleon_tools.config import Config, load_config, save_config, CONFIG_FILE
from meccha_chameleon_tools.translations import _tr
from meccha_chameleon_tools.ui import Menu, Overlay

_DEFAULT_GAME_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\MECCA CHAMELEON\Chameleon\Binaries\Win64"


def get_game_dir(config=None):
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


def _check_single_instance():
    mutex = ctypes.windll.kernel32.CreateMutexW(None, False, "MecchaCamouflage-Instance")
    if ctypes.windll.kernel32.GetLastError() == 183:
        QMessageBox.warning(None, "Already Running",
            "MecchaCamouflage is already running.\nOnly one instance is allowed.")
        sys.exit(1)


def main():
    _check_single_instance()
    _set_dpi_aware()
    app = QApplication(sys.argv)

    import meccha_chameleon_tools.logger as log
    log.init()
    log.info("=== MecchaCamouflage v1.9.1-wow (Python overlay mode) ===")
    if "--verbose" in sys.argv or "-v" in sys.argv:
        log.enable()

    config = load_config()
    if config.language == "EN" and not os.path.exists(CONFIG_FILE):
        detected = detect_system_language()
        if detected != "EN":
            config.language = detected
    _tr.set_language(config.language)

    esp = None
    for retry in range(5):
        try:
            esp = MecchaESP()
            log.info("Game connected")
            break
        except Exception as e:
            log.warn(f"Game attach attempt {retry+1}: {e}")
            time.sleep(1)

    menu = Menu(config, esp)
    menu.show()

    # Python overlay — fully functional, pixel-perfect reference
    overlay = Overlay(esp, config)
    overlay.show()

    app.aboutToQuit.connect(lambda: (save_config(config), esp.cleanup() if esp else None))
    ret = app.exec_()
    save_config(config)
    if esp:
        esp.cleanup()
    sys.exit(ret)


if __name__ == "__main__":
    main()
