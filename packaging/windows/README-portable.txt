FurnitureOS — portable Windows build
====================================

Free, open-source furniture CAD/CAM. Early development: this is a preview build,
not a production release. See docs\README.md and docs\KNOWN_LIMITATIONS.md.

Running
-------
* Unzip the folder anywhere (USB stick, Documents, ...). No installation and no
  administrator rights are needed; nothing is written to the registry.
* Start furnitureos.exe for the desktop application.
* fos.exe is the command-line tool (run "fos --help" in a terminal).
* Settings are stored per user by Qt (registry key HKCU\Software\FurnitureOS);
  projects are ordinary .fosp files wherever you save them.

If the 3D view stays empty
--------------------------
Older or virtual machines may lack OpenGL 3.3. Start the application with
software rendering:  set QT_OPENGL=software  then  furnitureos.exe

Printing
--------
Drawings are printed at exact scale. Disable "fit to page" / scaling in the
printer dialog and check the 100 mm calibration mark with a ruler.

CNC output
----------
G-code is generated for a generic dialect that is NOT verified for any machine.
It must be reviewed and proven by a qualified operator before use.

Licence
-------
FurnitureOS is licensed under the GNU Affero General Public License v3.0 only
(docs\LICENSE). Source code: https://github.com/shinigamich/furnitureos
This package includes the Qt 6 libraries (LGPL-3.0, dynamically linked; you may
replace the Qt DLLs with your own build) and the Microsoft Visual C++ runtime.
See docs\THIRD_PARTY_LICENSES.md for all third-party notices.
