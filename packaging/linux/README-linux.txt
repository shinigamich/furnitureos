FurnitureOS — Linux build (x86_64)
==================================

Free, open-source furniture CAD/CAM. Early development: this is a preview build,
not a production release. See share/doc/FurnitureOS/KNOWN_LIMITATIONS.md.

This tarball is built on Ubuntu 24.04 against the system Qt 6 libraries; it does
NOT bundle Qt. It runs on Ubuntu 24.04 or newer and similar distributions with
Qt 6.4+ installed, e.g. on Ubuntu/Debian:

  sudo apt install libqt6widgets6 libqt6opengl6 libqt6openglwidgets6 \
                   libqt6printsupport6 libqt6svg6 qt6-qpa-plugins

Running
-------
* Extract anywhere:  tar xzf FurnitureOS-*-linux-x86_64.tar.gz
* Desktop application:  bin/furnitureos
* Command-line tool:    bin/fos --help
* If the 3D view stays empty (no OpenGL 3.3):  QT_OPENGL=software bin/furnitureos

CNC output is a generic G-code dialect that is NOT verified for any machine, and
hardware drilling data is generic: check everything before production.

Licence: GNU AGPL-3.0-only. Source: https://github.com/shinigamich/furnitureos
Third-party notices: share/doc/FurnitureOS/THIRD_PARTY_LICENSES.md
