# Third-party components

EditHere uses dynamically linked Qt 6.8.3 (Qt Core, Gui, Widgets,
Concurrent, Network and Qt Image Formats), copyright The Qt Company Ltd. and
contributors, under LGPL version 3 and the components' applicable licenses.

Qt is not part of EditHere's application code. The shared libraries
and image plugins remain separately replaceable by compatible modified versions.
No EditHere restriction prohibits reverse engineering for debugging modifications
to these LGPL libraries. The license texts, copyright notices and upstream
attributions are included in the licenses directory. Some notices cover source-tree
components beyond the subset present in this runtime.

Windows runtime libraries include MinGW-w64/winpthreads and GCC 13.1.0 runtime
libraries. See licenses/mingw for their notices, GPL text and GCC Runtime Library Exception.

Corresponding unmodified Qt sources for this local delivery are retained alongside
the project at dist/native-sources/qtbase-everywhere-src-6.8.3.zip and
dist/native-sources/qtimageformats-everywhere-src-6.8.3.zip. They include third-party
sources and Qt build configuration. Qt binaries were obtained from the official Qt
online repository through aqtinstall; the application dynamically links them.
Recipients can obtain the same unmodified sources at no charge from the upstream
references below. The application license does not cover these libraries.

Upstream source references:
- https://download.qt.io/archive/qt/6.8/6.8.3/submodules/
- https://code.qt.io/cgit/qt/qtbase.git/tag/?h=v6.8.3
- https://code.qt.io/cgit/qt/qtimageformats.git/tag/?h=v6.8.3
- https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html

System fonts are loaded from the operating system and are not bundled.
No Snipaste, PixPin or Agentation source code or assets are included.
