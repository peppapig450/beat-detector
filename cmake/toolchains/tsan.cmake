
# ThreadSanitizer toolchain
if(MSVC)
  message(FATAL_ERROR "ThreadSanitizer is not supported on MSVC.")
endif()

set(_SAN_COMPILE "-fsanitize=thread -g")
set(_SAN_LINK    "-fsanitize=thread")

set(CMAKE_C_FLAGS_DEBUG_INIT          "${CMAKE_C_FLAGS_DEBUG_INIT} ${_SAN_COMPILE}")
set(CMAKE_CXX_FLAGS_DEBUG_INIT        "${CMAKE_CXX_FLAGS_DEBUG_INIT} ${_SAN_COMPILE}")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT "${CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT} ${_SAN_LINK}")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT "${CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT} ${_SAN_LINK}")
