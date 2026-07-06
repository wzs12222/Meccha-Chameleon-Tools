#!/usr/bin/env python3
import sys, os, ctypes, subprocess, threading, json, time

from PyQt5.QtWidgets import QApplication, QMessageBox
from PyQt5.QtCore import QTimer

from meccha_chameleon_tools.core import MecchaESP
from meccha_chameleon_tools.config import Config, load_config, save_config, CONFIG_FILE
from meccha_chameleon_tools.translations import _tr
from meccha_chameleon_tools.ui import Menu

_DEFAULT_GAME_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\MECCA CHAMELEON\Chameleon\Binaries\Win64"
_OVERLAY_EXE = "meccha-overlay.exe"
_MENU_TOGGLE_EVENT = "MecchaMenuToggle"

g_overlay_proc = None
g_menu_visible = True


def get_game_dir(config=None):
    if config and hasattr(config, "game_directory") and config.game_directory:
        return config.game_directory
    return _DEFAULT_GAME_DIR


def _resource_path(relative):
    try:
        base = sys._MEIPASS
    except AttributeError:
        base = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, relative)


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


def _start_overlay():
    global g_overlay_proc
    exe_path = _resource_path(_OVERLAY_EXE)
    if not os.path.exists(exe_path):
        return
    try:
        g_overlay_proc = subprocess.Popen(
            [exe_path],
            creationflags=subprocess.CREATE_NO_WINDOW,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        g_overlay_proc = None


def _menu_toggle_listener(menu):
    global g_menu_visible
    event = None
    try:
        event = ctypes.windll.kernel32.CreateEventW(None, False, False, _MENU_TOGGLE_EVENT)
        while True:
            if ctypes.windll.kernel32.WaitForSingleObject(event, 500) == 0:
                g_menu_visible = not g_menu_visible
                if g_menu_visible:
                    menu.show()
                    menu.activateWindow()
                else:
                    menu.hide()
    except Exception:
        pass
    finally:
        if event:
            ctypes.windll.kernel32.CloseHandle(event)


def main():
    _check_single_instance()
    _set_dpi_aware()
    app = QApplication(sys.argv)

    import meccha_chameleon_tools.logger as log
    log.init()
    log.info("=== MecchaCamouflage v1.9.1-wow starting (C++ overlay mode) ===")
    if "--verbose" in sys.argv or "-v" in sys.argv:
        log.enable()

    config = load_config()
    if config.language == "EN" and not os.path.exists(CONFIG_FILE):
        detected = detect_system_language()
        if detected != "EN":
            config.language = detected
    _tr.set_language(config.language)

    # Game attach
    esp = None
    for retry in range(5):
        try:
            esp = MecchaESP()
            log.info("Game connected")
            break
        except Exception as e:
            log.warn(f"Game attach attempt {retry+1}: {e}")
            time.sleep(1)

    # Start C++ overlay (it has its own memory engine)
    _start_overlay()
    log.info("C++ overlay launched")

    # Menu only (no Python overlay - C++ does the rendering)
    menu = Menu(config, esp)
    menu.show()

    # Menu toggle thread (listens for F1/Insert from overlay)
    t = threading.Thread(target=_menu_toggle_listener, args=(menu,), daemon=True)
    t.start()

    app.aboutToQuit.connect(lambda: _cleanup(config, esp))
    ret = app.exec_()
    _cleanup(config, esp)
    sys.exit(ret)


def _cleanup(config, esp):
    global g_overlay_proc
    save_config(config)
    if g_overlay_proc and g_overlay_proc.poll() is None:
        try:
            g_overlay_proc.terminate()
            g_overlay_proc.wait(3)
        except Exception:
            pass
    if esp:
        esp.cleanup()


if __name__ == "__main__":
    main()
