// AYEventSystem unit test entry point.
#include "AYTest.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    return ayt::test::runAllTests("AYEventSystem");
}