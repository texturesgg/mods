# Building mods for tgg-melee's Windows build, from Linux, with MinGW-w64 GCC
# (`nix develop .#windows`). The SDK carries every option that decides a game
# struct's layout (-mno-ms-bitfields among them), so this only names the
# target, the compilers and how to link.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

foreach(kind EXE SHARED MODULE)
    # The same source and SDK give the same mod.dll on every build
    string(APPEND CMAKE_${kind}_LINKER_FLAGS_INIT " -Wl,--no-insert-timestamp")
    # nixpkgs' MinGW GCC links with -lmcfgthread; the game ships its DLL
    string(APPEND CMAKE_${kind}_LINKER_FLAGS_INIT " -L$ENV{TGG_MINGW_MCFGTHREADS}/lib")
endforeach()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
