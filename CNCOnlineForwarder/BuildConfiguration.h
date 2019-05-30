#pragma once
#include <string>

#ifndef PROJECT_NAME
#define PROJECT_NAME PLACEHOLDER_STRING
#endif

#define PLACEHOLDER_STRING std::string{ __FILE__ } + "_"  + std::to_string(__LINE__)