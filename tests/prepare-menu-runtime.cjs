const fs=require('fs');
const config=fs.readFileSync('src/Config.cpp','utf8').replace('#include "pch.h"',`#define NOMINMAX
#define FMT_HEADER_ONLY
#include <windows.h>
#include <spdlog/spdlog.h>`).replace('#include "Diagnostics.h"','namespace Diagnostics { inline void SetLevel(int){} }');
fs.writeFileSync('build/menu-runtime-test.cpp',config+'\n#include "../tests/menu-runtime.cpp"\n');
