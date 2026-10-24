// AYEventSystem unit test entry point.
#include "AYTest.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#endif
#ifndef AY_EVENT_TEST_MODULE
#define AY_EVENT_TEST_MODULE "AYEventSystem"
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    return ayt::test::runTests(AY_EVENT_TEST_MODULE, argc, argv);
}
