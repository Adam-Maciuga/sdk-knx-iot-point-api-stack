"""Entry point for the EITT replacement GUI.

    cd tests/runtime
    pip install -r gui/requirements.txt
    python -m gui.eitt_gui [path/to/project.xml]

Defaults to the shipped master template, which is opened READ-ONLY — the
GUI never writes it (see the GUI plan's "master XML stays read-only"
decision; operator edits will go to a generated copy in phase 4).
"""

from __future__ import annotations

import argparse
import sys


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("template", nargs="?", default=None,
                        help="EITT project XML (defaults to the master "
                             "template in tests/runtime/data/)")
    args = parser.parse_args(argv)

    try:
        from PySide6.QtWidgets import QApplication
    except ImportError:
        print("PySide6 is not installed.\n"
              "    pip install -r gui/requirements.txt\n"
              "It is deliberately NOT in tests/runtime/requirements.txt so "
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
