cmake_minimum_required(VERSION 3.16)
project(desmume LANGUAGES C CXX)

option(DESMUME_SDL "Enable SDL2 support" ON)
option(DESMUME_OPENGL "Enable standard OpenGL" OFF)
option(DESMUME_OPENGLES "Enable OpenGL ES (overrides OpenGL)" OFF)
option(DESMUME_OPENAL "Enable experimental OpenAL microphone input" OFF)
option(DESMUME_ALSA "Enable ALSA microphone input (when OpenAL disabled)" OFF)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DTARGET_INTERFACE)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  add_definitions(-DHOST_64)
else()
  add_definitions(-DHOST_32)
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CPU_LOWER)
if(CPU_LOWER MATCHES "x86_64|amd64|x86|i686|i586|i486|i386|arm|aarch64|arm64")
  add_definitions(-DHAVE_JIT)
endif()

if(DESMUME_SDL)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

if(DESMUME_OPENGLES)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLES2 REQUIRED glesv2)
  add_definitions(-DENABLE_OPENGL_ES)
elseif(DESMUME_OPENGL)
  find_package(OpenGL REQUIRED)
  add_definitions(-DENABLE_OPENGL_STANDARD)
endif()

if(DESMUME_OPENAL)
  find_package(OpenAL REQUIRED)
  add_definitions(-DHAVE_OPENAL)
elseif(DESMUME_ALSA)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ALSA REQUIRED alsa)
endif()

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/desmume/src")

include_directories(
  ${ROOT}
  ${ROOT}/libretro-common/include
)

file(GLOB_RECURSE CORE_CPU
  ${ROOT}/core/cpu/*.cpp
)
file(GLOB_RECURSE CORE_MEMORY
  ${ROOT}/core/memory/MMU.cpp
)
file(GLOB_RECURSE CORE_GRAPHICS
  ${ROOT}/core/graphics/*.cpp
)
file(GLOB_RECURSE CORE_AUDIO
  ${ROOT}/core/audio/*.cpp
)
file(GLOB_RECURSE CORE_IO
  ${ROOT}/core/io/*.cpp
)
file(GLOB_RECURSE CORE_STORAGE
  ${ROOT}/core/storage/*.cpp
)
file(GLOB_RECURSE CORE_SYSTEM
  ${ROOT}/core/system/*.cpp
)

file(GLOB_RECURSE PLATFORM_OPENGL
  ${ROOT}/platform/opengl/*.cpp
)

file(GLOB_RECURSE PLATFORM_SIMD
  ${ROOT}/platform/simd/*.cpp
)

file(GLOB_RECURSE PLATFORM_SDL
  ${ROOT}/platform/sdl/audio/*.cpp
  ${ROOT}/platform/sdl/input/*.cpp
  ${ROOT}/platform/sdl/graphics/*.cpp
  ${ROOT}/platform/sdl/window/*.cpp
)

file(GLOB_RECURSE UTILS_COMMON
  ${ROOT}/utils/common/*.cpp
)
file(GLOB_RECURSE UTILS_FILE
  ${ROOT}/utils/file/*.cpp
)
file(GLOB_RECURSE UTILS_DEBUG
  ${ROOT}/utils/debug/*.cpp
)
file(GLOB_RECURSE UTILS_DATABASE
  ${ROOT}/utils/database/*.cpp
)
file(GLOB_RECURSE UTILS_MOVIE
  ${ROOT}/utils/movie/*.cpp
)
file(GLOB_RECURSE UTILS_DECRYPT
  ${ROOT}/utils/decrypt/*.cpp
)
file(GLOB_RECURSE UTILS_LIBFAT
  ${ROOT}/utils/libfat/*.cpp
)
file(GLOB_RECURSE UTILS_TINYXML
  ${ROOT}/utils/tinyxml/*.cpp
)

file(GLOB GRAPHICS_FILTERS
  ${ROOT}/graphics/filters/*.cpp
)

set(ADDONS
  ${ROOT}/addons/slot2_auto.cpp
  ${ROOT}/addons/slot2_mpcf.cpp
  ${ROOT}/addons/slot2_paddle.cpp
  ${ROOT}/addons/slot2_gbagame.cpp
  ${ROOT}/addons/slot2_none.cpp
  ${ROOT}/addons/slot2_rumblepak.cpp
  ${ROOT}/addons/slot2_guitarGrip.cpp
  ${ROOT}/addons/slot2_hcv1000.cpp
  ${ROOT}/addons/slot2_expMemory.cpp
  ${ROOT}/addons/slot2_piano.cpp
  ${ROOT}/addons/slot2_passme.cpp
  ${ROOT}/addons/slot1_none.cpp
  ${ROOT}/addons/slot1_r4.cpp
  ${ROOT}/addons/slot1_retail_nand.cpp
  ${ROOT}/addons/slot1_retail_auto.cpp
  ${ROOT}/addons/slot1_retail_mcrom.cpp
  ${ROOT}/addons/slot1_retail_mcrom_debug.cpp
  ${ROOT}/addons/slot1comp_mc.cpp
  ${ROOT}/addons/slot1comp_rom.cpp
  ${ROOT}/addons/slot1comp_protocol.cpp
)

set(MISC
  ${ROOT}/FIFO.cpp
  ${ROOT}/encrypt.cpp
  ${ROOT}/commandline.cpp
  ${ROOT}/driver.cpp
  ${ROOT}/frontend/modules/Disassembler.cpp
)

add_library(desmume-core STATIC
  ${CORE_CPU}
  ${CORE_MEMORY}
  ${CORE_GRAPHICS}
  ${CORE_AUDIO}
  ${CORE_IO}
  ${CORE_STORAGE}
  ${CORE_SYSTEM}
  ${PLATFORM_OPENGL}
  ${PLATFORM_SIMD}
  ${UTILS_COMMON}
  ${UTILS_FILE}
  ${UTILS_DEBUG}
  ${UTILS_DATABASE}
  ${UTILS_MOVIE}
  ${UTILS_DECRYPT}
  ${UTILS_LIBFAT}
  ${UTILS_TINYXML}
  ${GRAPHICS_FILTERS}
  ${ADDONS}
  ${MISC}
)

target_include_directories(desmume-core PUBLIC
  ${ROOT}
  ${ROOT}/libretro-common/include
)

if(DESMUME_SDL)
  include_directories(${SDL2_INCLUDE_DIRS})
endif()

if(DESMUME_OPENGL)
  include_directories(${OPENGL_INCLUDE_DIRS})
endif()

if(DESMUME_OPENGLES)
  include_directories(${GLES2_INCLUDE_DIRS})
endif()

if(DESMUME_OPENAL)
  include_directories(${OPENAL_INCLUDE_DIR})
endif()

if(DESMUME_ALSA)
  include_directories(${ALSA_INCLUDE_DIRS})
endif()

# Interface shared library (depends on SDL)
set(INTERFACE_SRC
  ${ROOT}/frontend/api/interface/interface.cpp
  ${ROOT}/platform/sdl/window/draw_sdl_window.cpp
  ${PLATFORM_SDL}
)

add_library(desmume-interface SHARED ${INTERFACE_SRC})
target_link_libraries(desmume-interface PRIVATE desmume-core)

if(DESMUME_SDL)
  target_compile_options(desmume-interface PRIVATE ${SDL2_CFLAGS_OTHER})
  target_link_libraries(desmume-interface PRIVATE ${SDL2_LIBRARIES})
endif()

if(DESMUME_OPENGLES)
  target_link_libraries(desmume-core PRIVATE ${GLES2_LIBRARIES})
elseif(DESMUME_OPENGL)
  target_link_libraries(desmume-core PRIVATE ${OPENGL_gl_LIBRARY})
endif()

if(DESMUME_OPENAL)
  target_link_libraries(desmume-interface PRIVATE OpenAL::OpenAL)
elseif(DESMUME_ALSA)
  target_link_libraries(desmume-interface PRIVATE ${ALSA_LIBRARIES})
endif()

if(UNIX)
  target_link_libraries(desmume-interface PRIVATE dl m)
endif()

set_target_properties(desmume-interface PROPERTIES OUTPUT_NAME desmume)


