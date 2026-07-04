#!/usr/bin/env python3
"""Standalone camouflage-only entry point for MecchaCamouflage.exe."""
import sys
import os
import ctypes

from PyQt5.QtWidgets import QApplication, QMessageBox
from PyQt5.QtCore import QTimer

from meccha_chameleon_tools.core import MecchaESP
from meccha_chameleon_tools.config import Config, load_config, save_config
from meccha_chameleon_tools.i18n import set_language, tr
from meccha_chameleon_tools.ui import Menu


def camo_main():
    try:
        ctypes.windll.user32.SetProcessDpiAwarenessContext(-4)
    except Exception:
        pass
    app = QApplication(sys.argv)

    config = load_config()
    set_language(config.language)
    try:
        esp = MecchaESP()
    except (RuntimeError, Exception) as e:
        QMessageBox.critical(
            None, tr("game_not_found_title"),
            tr("game_not_found_msg", error=str(e))
        )
        sys.exit(1)

    menu = Menu(config, esp, tabs=["Camouflage"])
    menu.setWindowTitle(tr("camo_title"))
    menu.show()
    app.aboutToQuit.connect(lambda: (save_config(config), esp.cleanup()))
    sys.exit(app.exec_())


if __name__ == "__main__":
    camo_main()
