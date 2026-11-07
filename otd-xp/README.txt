otd-xp - small tablet driver for Windows XP (32-bit) and newer
==============================================================

A stripped-down driver in the spirit of OpenTabletDriver, written in plain C so it needs
no .NET and no installer. It reads the pen tablet through Windows' standard HID driver and
moves the mouse cursor. It uses OpenTabletDriver's tablet database (tablets.txt) to
recognise about 340 tablet models and decode their reports.

NOT included: GUI, plugins, filters/smoothing, tilt, Windows Ink, tablet buttons/touch.
Settings are in the text file otd-xp.ini.

Using it
--------
1. Keep otd-xp.exe, tablets.txt and otd-xp.ini together in one folder.
2. Plug in the tablet. If a manufacturer driver (e.g. Wacom) is installed, uninstall it or
   close its tray program first; two drivers cannot read the same tablet.
3. Open a command prompt in that folder and run:   otd-xp.exe --list
   You should see your tablet with an arrow line "-> <model name> (<parser>)".
4. Run:   otd-xp.exe
   Move the pen. Press Ctrl+C to quit. Edit otd-xp.ini to change the area, rotation, etc.

Troubleshooting
---------------
* "no supported tablet found": run --list and send/read the output. If your tablet is shown
  as "known tablet ... but with report length X", it is exposing a different HID interface;
  try its other interfaces. If it says "parser not implemented", this build cannot decode it.
* Tablet listed but the cursor does not move: run   otd-xp.exe --dump N   (N = the [number]
  from --list) and move the pen. If numbers appear, the reports are arriving; set debug = 1
  in otd-xp.ini and compare. If nothing appears, another driver is holding the device.
* Cursor jumps to wrong place: adjust rotation / area_* / lock_aspect in otd-xp.ini.
* Some tablets (Wacom Intuos 4/5/Pro, Cintiq) may need their own driver's vendor mode and
  may not work without it.

Licence
-------
Report decoding and tablets.txt are derived from OpenTabletDriver (LGPL-3.0-or-later,
https://github.com/OpenTabletDriver/OpenTabletDriver). This program is released under the
same licence.

Rebuilding: needs mingw-w64 (i686). See Makefile, or use the GitHub Actions workflow.
