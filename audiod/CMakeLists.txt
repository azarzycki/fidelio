cmake_minimum_required(VERSION 3.16)
project(fidelio-audiod CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared pieces: the wire protocol (audiod/proto.h), framing + SocketSink (common/), and the
# AudioOutput interface + FileOutput/NullOutput from the AirPlay receiver (airplay/src).
set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(INC ${REPO} ${REPO}/common ${REPO}/airplay/src ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(audiod_common STATIC ${REPO}/common/AudiodFraming.cpp ${REPO}/common/SocketSink.cpp)
target_include_directories(audiod_common PUBLIC ${INC})
target_compile_options(audiod_common PRIVATE -O2 -Wall)

add_executable(fidelio-audiod src/main.cpp src/Server.cpp src/Arbiter.cpp)
target_include_directories(fidelio-audiod PRIVATE ${INC})
target_compile_options(fidelio-audiod PRIVATE -O2 -Wall)
target_link_libraries(fidelio-audiod PRIVATE audiod_common)
if(NOT APPLE)
  target_link_libraries(fidelio-audiod PRIVATE pthread)
endif()
