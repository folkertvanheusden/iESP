# mkdir buildMingw64 && cd buildMingw64
# cmake -DCMAKE_TOOLCHAIN_FILE=../mingw64.cmake ..

set(CMAKE_SYSTEM_NAME Windows)
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc-posix)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++-posix)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX} /usr/lib/gcc/${TOOLCHAIN_PREFIX}/12-posix)

set(IS_WINDOWS TRUE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

add_compile_options(-mno-ms-bitfields -D_FILE_OFFSET_BITS=64)
link_libraries(ws2_32)
set(CMAKE_EXE_LINKER_FLAGS "-lws2_32 -static -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -lwinpthread -Wl,-Bdynamic")
