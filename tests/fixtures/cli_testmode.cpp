// Exercise all production CLI parsing and IPC logic unchanged, while selecting
// the same isolated QStandardPaths namespace as the online test's parent.
#include <QStandardPaths>
#define main edithereCliMain
#include "../../app/cli.cpp"
#undef main

int main(int argc, char **argv) {
    QStandardPaths::setTestModeEnabled(true);
    return edithereCliMain(argc, argv);
}
