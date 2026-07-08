#!/usr/bin/env python3
import sys, os, ctypes, time, threading

try:
    _console = ctypes.windll.kernel32.AllocConsole()
    if _console:
        sys.stdout = open("CONOUT$", "w", encoding="utf-8")
        sys.stderr = open("CONOUT$", "w", encoding="utf-8")
except Exception:
    _console = False


def _boot_msg(msg):
    if _console:
        print(f"[Meccha Chameleon Tools] {msg}")


_boot_msg("Starting Meccha Chameleon Tools...")
_boot_msg("Loading modules...")

import meccha_chameleon_tools.logger as log

# Crash hooks — capture unhandled exceptions to log file
_original_excepthook = sys.excepthook

def _crash_hook(etype, value, tb):
    import traceback
    msg = "".join(traceback.format_exception(etype, value, tb))
    log.crash(f"Unhandled exception:\n{msg}")
    if _original_excepthook:
        _original_excepthook(etype, value, tb)

sys.excepthook = _crash_hook

from PyQt5.QtWidgets import QApplication, QMessageBox
from PyQt5.QtCore import QTimer
from PyQt5.QtCore import qInstallMessageHandler

def _qt_msg_handler(mode, context, msg):
    log.crash(f"Qt {mode}: {msg}")

qInstallMessageHandler(_qt_msg_handler)

from meccha_chameleon_tools.core import MecchaESP
from meccha_chameleon_tools.config import Config, load_config, save_config, CONFIG_FILE
from meccha_chameleon_tools.translations import _tr
from meccha_chameleon_tools.ui import Menu, Overlay

_boot_msg("Modules loaded.")

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
            import meccha_chameleon_tools.logger as log
            log.debug("SetProcessDPIAware failed")


def _check_single_instance():
    mutex = ctypes.windll.kernel32.CreateMutexW(None, False, "MecchaCamouflage-Instance")
    if ctypes.windll.kernel32.GetLastError() == 183:
        QMessageBox.warning(None, "Already Running",
            "MecchaCamouflage is already running.\nOnly one instance is allowed.")
        sys.exit(1)


def _close_console():
    global _console
    if _console:
        try:
            sys.stdout.close()
        except Exception as _e:
            log.debug(f"{_e}")
        try:
            sys.stderr.close()
        except Exception as _e:
            log.debug(f"{_e}")
        ctypes.windll.kernel32.FreeConsole()
        _console = False


def main():
    _boot_msg("Checking single instance...")
    _check_single_instance()
    _boot_msg("Setting DPI awareness...")
    _set_dpi_aware()
    _boot_msg("Initializing Qt application...")
    app = QApplication(sys.argv)

    log.init()
    log.info("=== MecchaCamouflage v1.9.1-wow ===")
    if "--verbose" in sys.argv or "-v" in sys.argv or "--debug" in sys.argv:
        log.enable()
        _boot_msg("Debug logging enabled.")

    _boot_msg("Loading configuration...")
    config = load_config()
    if config.language == "EN" and not os.path.exists(CONFIG_FILE):
        detected = detect_system_language()
        if detected != "EN":
            config.language = detected
    _tr.set_language(config.language)

    _boot_msg("Initializing memory engine...")
    esp = None
    try:
        esp = MecchaESP()
        _boot_msg("Game connected.")
    except RuntimeError as e:
        log.warn(f"ESP init failed: {e}")
        _boot_msg("Game not found — will attach in background.")
    except Exception as e:
        log.error(f"Unexpected error: {e}")
        _boot_msg("Game not found — will attach in background.")

    _boot_msg("Creating GUI...")
    menu = Menu(config, esp)
    menu.show()

    overlay = Overlay(esp, config)
    overlay.show()

    _boot_msg("GUI ready.")
    _close_console()

    app.aboutToQuit.connect(lambda: (save_config(config), esp.cleanup() if esp else None))
    ret = app.exec_()
    save_config(config)
    if esp:
        esp.cleanup()
    sys.exit(ret)


if __name__ == "__main__":
    main()
