# Free Notice — proprietary component

`src/app/FreeNoticePrivate.cpp` / `FreeNoticePrivate.h` contain the encrypted
free-notice payload. They are **not** covered by the project's open-source
license and must not be published with the public source tree.

`src/app/FreeNotice.cpp` and `qml/Main.qml` reference the payload; removing the
payload, the loader, or the startup gate means the application cannot start
(runtime integrity check returns false and `main()` exits with code 4).

This is an anti-tamper / attribution component, not a security boundary:
client-side code can always be patched by a determined attacker.
