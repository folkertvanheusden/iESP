set(CMAKE_SYSTEM_NAME Windows)

add_compile_options(-mno-ms-bitfields -D_FILE_OFFSET_BITS=64)
link_libraries(ws2_32)
set(CMAKE_EXE_LINKER_FLAGS "-lws2_32 -static -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -lwinpthread -Wl,-Bdynamic")
