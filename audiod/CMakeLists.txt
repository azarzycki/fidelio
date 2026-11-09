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
if(APPLE)
  # The hardware output: the route-following AudioQueue sink shared with the producers' --direct-audio.
  target_sources(fidelio-audiod PRIVATE ${REPO}/airplay/src/AudioQueueSink.cpp)
  target_link_libraries(fidelio-audiod PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
else()
  target_link_libraries(fidelio-audiod PRIVATE pthread)
endif()

# Host tests
add_executable(framing_test hosttest/framing_test.cpp)
target_include_directories(framing_test PRIVATE ${INC})
target_link_libraries(framing_test PRIVATE audiod_common)

# A tiny producer that streams a file through SocketSink (exercises reconnect too)
add_executable(socketsink_producer hosttest/socketsink_producer.cpp)
target_include_directories(socketsink_producer PRIVATE ${INC})
target_link_libraries(socketsink_producer PRIVATE audiod_common)

# AudioQueueSink state machine on the host, against a mock AudioToolbox (hosttest/mock)
if(NOT APPLE)
  add_executable(aqsink_test hosttest/aqsink_test.cpp hosttest/mock/mock_audiotoolbox.cpp ${REPO}/airplay/src/AudioQueueSink.cpp)
  target_include_directories(aqsink_test BEFORE PRIVATE hosttest/mock)
  target_include_directories(aqsink_test PRIVATE ${INC})
  target_compile_options(aqsink_test PRIVATE -O1 -g -Wall -Wno-multichar)
  target_link_libraries(aqsink_test PRIVATE pthread)
  # B1.8: the bare AudioQueue path (--audio-session off / auto fallback) makes ZERO AudioSession calls
  add_executable(aqsink_bare_test hosttest/aqsink_bare_test.cpp hosttest/mock/mock_audiotoolbox.cpp ${REPO}/airplay/src/AudioQueueSink.cpp)
  target_include_directories(aqsink_bare_test BEFORE PRIVATE hosttest/mock)
  target_include_directories(aqsink_bare_test PRIVATE ${INC})
  target_compile_options(aqsink_bare_test PRIVATE -O1 -g -Wall -Wno-multichar)
  target_link_libraries(aqsink_bare_test PRIVATE pthread)
endif()
