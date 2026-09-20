cmake -B build
cmake --build build --config Release
gcc LiquidVoice.c deps/lua/lua51.def -shared -static-libgcc -I"deps/lua" -L"deps/lua" -llua51 -o bin/LiquidVoice.dll