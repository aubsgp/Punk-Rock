cmake_minimum_required(VERSION 3.16)
project(PunkRock CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../)
set(CMAKE_EXE_LINKER_FLAGS -static)

file(GLOB SSEQ_SOURCES deps/SSEQPlayer/*.cpp)
add_executable(
    PunkRock
    PunkRock.cpp
    deps/ROM/ROM.cpp
    ${SSEQ_SOURCES}
)

set_target_properties(PunkRock PROPERTIES 
    WIN32_EXECUTABLE TRUE
)

target_include_directories(PunkRock PRIVATE . deps/miniaudio deps/ROM deps/SSEQPlayer)
target_link_libraries(PunkRock PRIVATE ole32 comctl32)