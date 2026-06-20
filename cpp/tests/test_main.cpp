// Doctest entry point for the native C++ test executable.
//
// Keeping main in one translation unit lets individual test files include
// doctest without repeating DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
