"""Entry point for the EITT replacement GUI.

    cd tests/eitt
    pip install -r gui/requirements.txt
    python -m gui.eitt_gui [path/to/project.xml]

Defaults to the shipped master template, which is opened READ-ONLY — the
GUI never writes it (see the GUI plan's "master XML stays read-only"
decision; operator edits will go to a generated copy in phase 4).
"""

from __future__ import annotations

import argparse
import os
import sys

# The GUI is the only entry point into the engine that pytest does NOT
# start. Under pytest, tests/pytest.ini's `pythonpath = . common eitt`
# puts BOTH tiers' shared code on sys.path; launched by hand from
# tests/eitt, only this directory is there, so `interpreter.*` and
# `gui.*` resolve but the transport clients and the fixtures module in
# tests/common do not. Add tests/common here so a hand-launched GUI sees
# exactly what a pytest run does.
_COMMON = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "common")
if _COMMON not in sys.path:
    sys.path.insert(0, _COMMON)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("template", nargs="?", default=None,
                        help="EITT project XML (defaults to the master "
                             "template in tests/eitt/data/)")
    args = parser.parse_args(argv)

    try:
        from PySide6.QtWidgets import QApplication
    except ImportError:
        print("PySide6 is not installed.\n"
              "    pip install -r gui/requirements.txt\n"
              "It is deliberately NOT in tests/common/requirements.txt so "
              "the headless Docker conformance image stays Qt-free.",
              file=sys.stderr)
        return 2

    from .main_window import MainWindow

    app = QApplication(sys.argv[:1])
    window = MainWindow(template=args.template)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
