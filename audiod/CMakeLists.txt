cmake_minimum_required(VERSION 3.16)
project(fidelio-audiod CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared pieces: the wire protocol (audiod/proto.h), framing + SocketSink (common/), and the
# AudioOutput interface + FileOutput/NullOutput from the AirPlay receiver (airplay/src).
set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(INC ${REPO} ${REPO}/common ${REPO}/airplay/src ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(audiod_common STATIC ${REPO}/common/AudiodFraming.cpp ${REPO}/common/SocketSink.cpp ${REPO}/common/Settings.cpp ${REPO}/common/AudiodControl.cpp)
target_include_directories(audiod_common PUBLIC ${INC})
target_compile_options(audiod_common PRIVATE -O2 -Wall)

add_executable(fidelio-audiod src/main.cpp src/Server.cpp src/Arbiter.cpp src/HardwareVolume.cpp)
target_include_directories(fidelio-audiod PRIVATE ${INC})
target_compile_options(fidelio-audiod PRIVATE -O2 -Wall)
target_link_libraries(fidelio-audiod PRIVATE audiod_common)
if(APPLE)
  # B1.11: fidelio-lockctl enables/disables a SpringBoard lock-screen bundle by name (dlopen'ed).
  add_executable(fidelio-lockctl src/lockctl.cpp)
  target_include_directories(fidelio-lockctl PRIVATE ${INC})
  target_compile_options(fidelio-lockctl PRIVATE -O2 -Wall)

  # B1.15 A: SpringBoard checks com.apple.springboard.launchapplications (for
  # SBSLaunchApplicationWithIdentifier, wake-on-play) and .activateawayviewplugins (for
  # SBEnableLockScreenBundle, the lock plugin). Without them the launch returns 1 even unlocked/root
  # and the away-view enable is ignored. Sign fidelio-audiod and fidelio-lockctl at build time with the
  # entitlements plist using the patched armv6-ios3-ldid (keeps the LDID_CD_VERSION CodeDirectory that
  # loads on iPhone OS 3.1.3). Strip first so the signature is over the final bytes.
  set(ENTS ${REPO}/common/fidelio-springboard.entitlements.plist)
  find_program(IOS3_STRIP armv6-ios3-strip)
  find_program(IOS3_LDID armv6-ios3-ldid)
  if(IOS3_STRIP AND IOS3_LDID)
    foreach(tgt fidelio-audiod fidelio-lockctl)
      # B1.16.4: the signature is made at link time, so a changed plist must relink (otherwise the
      # target that did not change keeps its old entitlements: found when opensensitiveurl was added).
      set_property(TARGET ${tgt} APPEND PROPERTY LINK_DEPENDS ${ENTS})
      add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${IOS3_STRIP} -x $<TARGET_FILE:${tgt}>
        COMMAND ${IOS3_LDID} -S${ENTS} $<TARGET_FILE:${tgt}>
        COMMENT "Signing ${tgt} with the SpringBoard entitlements (B1.15 A)"
        VERBATIM)
    endforeach()
  else()
    message(WARNING "armv6-ios3-strip/ldid not found: fidelio-audiod/lockctl will NOT be entitlement-signed")
  endif()
endif()

if(APPLE)
  # The hardware output: the route-following AudioQueue sink shared with the producers' --direct-audio.
  target_sources(fidelio-audiod PRIVATE ${REPO}/airplay/src/AudioQueueSink.cpp src/HardwareVolumeApple.cpp src/DisplayWakeApple.cpp)
  # B1.9: the private Celestial / SpringBoardServices / GraphicsServices frameworks are dlopen'ed at
  # runtime (never linked); only the public ObjC runtime is linked for objc_msgSend.
  target_link_libraries(fidelio-audiod PRIVATE "-framework AudioToolbox" "-framework CoreFoundation" objc)
else()
  target_link_libraries(fidelio-audiod PRIVATE pthread)
endif()

# Host tests
add_executable(framing_test hosttest/framing_test.cpp)
target_include_directories(framing_test PRIVATE ${INC})
target_link_libraries(framing_test PRIVATE audiod_common)

# A tiny producer that streams a file through SocketSink (exercises reconnect too)
add_executable(settings_test hosttest/settings_test.cpp)
target_include_directories(settings_test PRIVATE ${INC})
target_link_libraries(settings_test PRIVATE audiod_common)

add_executable(writeframe_test hosttest/writeframe_test.cpp)
target_include_directories(writeframe_test PRIVATE ${INC})
target_link_libraries(writeframe_test PRIVATE audiod_common pthread)

add_executable(control_send hosttest/control_send.cpp)
target_include_directories(control_send PRIVATE ${INC})
target_link_libraries(control_send PRIVATE audiod_common)

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
