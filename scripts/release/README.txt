Pulse Universe
==============

A Windows system monitor: processes and CPU cores drawn as a small solar system in your browser.

Run
---
1. Double-click "Start Pulse Universe.bat" (or pulse-engine.exe).
   A console window opens and your browser shows the universe at http://127.0.0.1:9000/.
2. Press Ctrl+C in the console window to stop.

For measured thread-to-core lines, double-click "Start Pulse Universe (Admin).bat" and accept the
permission prompt. The top badge in the browser shows "elevated" when it worked. Without administrator
rights everything still works; the lines are estimated (drawn fainter) and a few processes show 0 MB.

Windows SmartScreen
-------------------
The program is not code-signed, so Windows may say "Windows protected your PC" the first time.
Choose "More info" and then "Run anyway". The download page lists a .sha256 file; you can compare it
with the zip you downloaded:
    Get-FileHash pulse-universe-*-win-x64.zip -Algorithm SHA256

Keys in the browser
-------------------
D  switch between the universe and the numbers dashboard
H  hide or show the legend
P  performance readout
Esc, or click empty space / the star  release focus

Privacy
-------
Everything stays on your PC. The engine listens on 127.0.0.1 only and sends nothing anywhere.

Uninstall
---------
Delete this folder. Nothing is installed and nothing is written outside it.

Source and issues: https://github.com/Rafdidas/pulse-universe
