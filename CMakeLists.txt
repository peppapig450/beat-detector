cmake_minimum_required(VERSION 3.29) # C++20 modules file sets need 3.28+, 3.29+ recommended
project(beat_detector VERSION 0.1.0 LANGUAGES CXX)

# --- C++ standard & convenience ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Let CMake scan TU's for `import ...` automatically (helps consumers)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# --- Options ---
option(BUILD_LIBRARY         "Build the beat_detector shared library" ON)
option(BUILD_CLI             "Build the beat_cli executable"          ON)
option(BUILD_MODULE_WRAPPER  "Build C++20 module wrapper units"       ON)
option(ENABLE_LTO            "Enable interprocedural optimization"    OFF)
option(WERROR                "Treat warnings as errors"               OFF)

# --- IPO / LTO toggle ---
if(ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(WARNING "IPO/LTO not supported: ${_ipo_msg}")
  endif()
endif()

# --- Dependencies via pkg-config (treat includes as SYSTEM like in Meson) ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(AUBIO    REQUIRED IMPORTED_TARGET aubio)

# --- Sources ---
set(IMPL_SRCS
  modules/beat.detector.impl.cpp
  # 'src/pw_audio_raii.hpp' is header-only, not compiled directly
)
set(MAIN_SRC src/main.cpp)

# --- Helper: strict but friendly warnings, per compiler ---
function(setup_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(${target} PRIVATE
      -Weverything
      -Wno-c++98-compat
      -Wno-c++98-compat-pedantic
      -Wno-padded
      -Wno-switch-enum
      -Wno-covered-switch-default
      -Wno-disabled-macro-expansion
      -Wno-weak-vtables
      -Wno-global-constructors
      -fsafe-buffer-usage-suggestions
      -Wno-exit-time-destructors
      -Wno-unused-macros
      -Wno-gnu-anonymous-struct
      -Wno-nested-anon-types
      -Wno-reserved-id-macro
      -Wno-documentation-unknown-command
      # extras not in -Weverything
      -Wshadow-all
      -Wshorten-64-to-32
      -Wimplicit-fallthrough
      -Wfloat-equal
      -Wnull-dereference
      -Wextra-semi
      -Wcomma
    )
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -Wconversion -Wsign-conversion -Wshadow
      -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
      -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast
    )
  endif()

  if(WERROR)
    if(MSVC)
      target_compile_options(${target} PRIVATE /WX)
    else()
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# --- Library target (optional) ---
if(BUILD_LIBRARY)
  add_library(beat_detector SHARED ${IMPL_SRCS})

  # Our own headers (non-system) + external deps (system)
  target_include_directories(beat_detector PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_include_directories(beat_detector SYSTEM PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${AUBIO_INCLUDE_DIRS}
  )
  
  # Link pkg-config imported targets (modern CMake),
  # and keep SYSTEM includes to reduce external warning noise.
  target_link_libraries(beat_detector
    PRIVATE
      PkgConfig::PIPEWIRE
      PkgConfig::AUBIO
  )

  setup_warnings(beat_detector)

  # --- C++20 modules wrapper (optional) ---
  if(BUILD_MODULE_WRAPPER)
    # If our compiler supports standard C++20 modules, CMake will drive it.
    # (Older TS flags like -fmodules-ts or /experimental:module are not used here;
    # switch them on manually if we truly need them.)
    target_sources(beat_detector
      PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES 
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
          modules/audio.blocks.cppm
          modules/audio.blocks.spa.cppm
          modules/audio.blocks.core.cppm
          modules/beat.detector.aubio_raii.cppm
          modules/beat.detector.cppm
          modules/beat.detector.pw_raii.cppm
    )
    # If you discover a toolchain that needs TS flags, uncomment as needed:
    # if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    #   target_compile_options(beat_detector PRIVATE -fmodules-ts)
    # elseif(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.37)
    #   target_compile_options(beat_detector PRIVATE /experimental:module)
    # endif()
  endif()

  # Install the shared lib and public headers
  install(TARGETS beat_detector
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    FILE_SET cxx_modules DESTINATION include  # installs module interface units
  )
  install(DIRECTORY include/ DESTINATION include)
endif()

# --- CLI target (optional) ---
if(BUILD_CLI)
  if(BUILD_LIBRARY)
    add_executable(beat_cli ${MAIN_SRC})
    target_link_libraries(beat_cli PRIVATE beat_detector PkgConfig::PIPEWIRE PkgConfig::AUBIO)
    target_include_directories(beat_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    setup_warnings(beat_cli)
  else()
    # No library: compile implementation into the CLI directly
    add_executable(beat_cli ${IMPL_SRCS} ${MAIN_SRC})
    target_include_directories(beat_cli
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
      SYSTEM PRIVATE
        ${PIPEWIRE_INCLUDE_DIRS}
        ${AUBIO_INCLUDE_DIRS}
    )
    target_link_libraries(beat_cli PRIVATE PkgConfig::PIPEWIRE PkgConfig::AUBIO)
    setup_warnings(beat_cli)
  endif()

  install(TARGETS beat_cli RUNTIME DESTINATION bin)
endif()

# --- clang-format helper---
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
  # Use a simple glob; swap to `git ls-files` if you want to strictly follow the index.
  file(GLOB_RECURSE FORMAT_SRCS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/*.ixx
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/*.cppm
  )
  if(FORMAT_SRCS)
    add_custom_target(format
      COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_SRCS}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Running clang-format on project sources"
      VERBATIM
    )
  endif()
endif()
