# AddressSanitizer toolchain
# Applies to debug by default

if (MSVC)
  message(FATAL_ERROR "MSVC is not supported because this project requires PipeWire (Linux only).")
else()
  set(_SAN_COMPILE "-fsanitize=address,undefined -fno-omit-frame-pointer -g")
  set(_SAN_LINK    "-fsanitize=address,undefined")
endif()

# Only Debug (change to *_FLAGS_INIT if you want all configs)
set(CMAKE_C_FLAGS_DEBUG_INIT             "${CMAKE_C_FLAGS_DEBUG_INIT} ${_SAN_COMPILE}")
set(CMAKE_CXX_FLAGS_DEBUG_INIT           "${CMAKE_CXX_FLAGS_DEBUG_INIT} ${_SAN_COMPILE}")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT    "${CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT} ${_SAN_LINK}")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT "${CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT} ${_SAN_LINK}")
