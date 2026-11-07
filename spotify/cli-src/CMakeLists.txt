cmake_minimum_required(VERSION 3.16)
project(cspot-ios3 CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# bell options for a tiny CLI: vorbis only, no sinks, no webserver, no mqtt, cJSON only
set(BELL_DISABLE_CODECS OFF)
set(BELL_CODEC_AAC ON)
set(BELL_CODEC_MP3 ON)
set(BELL_CODEC_OPUS OFF)
set(BELL_CODEC_ALAC OFF)
set(BELL_CODEC_VORBIS ON)
set(BELL_DISABLE_SINKS ON)
set(BELL_DISABLE_FMT ON)
set(BELL_DISABLE_REGEX ON)
set(BELL_ONLY_CJSON ON)
set(BELL_DISABLE_MQTT ON)
set(BELL_DISABLE_WEBSERVER OFF)
# 116 MB device: 1 civetweb worker, 128 KB thread stacks (civetweb USE_STACK_SIZE, bell Tasks)
add_compile_definitions(BELL_APPLE_NO_DISPATCH BELL_HTTP_NUM_THREADS="1" BELL_TASK_STACK_FLOOR=131072 USE_STACK_SIZE=131072)

# The cspot library tree (squeezelite-esp32 components/spotify/cspot + spotify/patches): ../cspot
# by default (a checkout or symlink next to cli-src), or -DCSPOT_DIR=...
set(CSPOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cspot CACHE PATH "cspot library tree")
add_subdirectory(${CSPOT_DIR} ${CMAKE_CURRENT_BINARY_DIR}/cspot)

# Shared with fidelio-audiod and fidelio-raop: the audiod wire framing and the SocketSink output.
set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
file(GLOB SOURCES "*.cpp")
if(NOT APPLE)
  list(FILTER SOURCES EXCLUDE REGEX "AudioQueueSink")  # host build: audiod / --no-audio only
endif()
add_executable(cspot-ios3 ${SOURCES} ${COMMON}/AudiodFraming.cpp ${COMMON}/SocketSink.cpp)
target_include_directories(cspot-ios3 PRIVATE "." ${COMMON} ${CMAKE_CURRENT_SOURCE_DIR}/../..)
if(APPLE)
  target_link_libraries(cspot-ios3 PUBLIC cspot "-framework AudioToolbox" "-framework CoreFoundation")
else()
  target_link_libraries(cspot-ios3 PUBLIC cspot pthread)
endif()
