// Installer tests must never launch the user's application or change settings.
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return 0; }
