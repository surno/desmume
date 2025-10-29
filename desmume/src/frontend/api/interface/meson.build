project('desmume',
  ['c', 'cpp'],
  version: '2.7.10',
  meson_version: '>=0.45',
  default_options: [
    'warning_level=1',
    'c_std=c11',
    'cpp_std=c++14',
  ],
  license: 'GPL2+',
)

dep_glib2 = dependency('glib-2.0')
dep_sdl = dependency('sdl2')
dep_pcap = dependency('pcap')
dep_zlib = dependency('zlib')
dep_threads = dependency('threads')
dep_openal = dependency('openal', required: get_option('openal'))
dep_alsa = dependency('alsa', required: false)
dep_soundtouch = dependency('soundtouch', required: false)

if host_machine.system() == 'darwin'
  dep_gl = dependency('appleframeworks', modules: 'OpenGL', required: false)
  dep_gles = dependency('appleframeworks', modules: 'OpenGLES', required: false)
else
  dep_gl = dependency('gl', required: false)
  dep_gles = dependency('glesv2', required: false)
endif

if get_option('wifi')
  add_global_arguments('-DEXPERIMENTAL_WIFI_COMM', language: ['c', 'cpp'])
endif

add_global_arguments('-DTARGET_INTERFACE', language: ['c', 'cpp'])

dependencies = [dep_glib2, dep_sdl, dep_pcap, dep_zlib, dep_threads]

if target_machine.cpu() == 'x86_64'
  add_global_arguments('-DHAVE_JIT', language: ['c', 'cpp'])
  add_global_arguments('-DHOST_64', language: ['c', 'cpp'])
  have_jit = true
elif target_machine.cpu() == 'i686'
  add_global_arguments('-DHAVE_JIT', language: ['c', 'cpp'])
  add_global_arguments('-DHOST_32', language: ['c', 'cpp'])
  have_jit = true
else
  have_jit = false
endif

# Clang refuses to compile with this warning
if meson.get_compiler('cpp').get_id() == 'clang'
  add_global_arguments('-Wno-c++11-narrowing', language: ['c', 'cpp'])
endif

includes = include_directories(
  '../../../src',
  '../../../src/libretro-common/include',
  '../../../src/frontend',
)

libdesmume_src = [
  'interface.cpp', 
  '../../../platform/sdl/window/draw_sdl_window.cpp',
  
  # Core CPU
  '../../../core/cpu/armcpu.cpp',
  '../../../core/cpu/arm_instructions.cpp',
  '../../../core/cpu/thumb_instructions.cpp',
  '../../../core/cpu/cp15.cpp',
  
  # Core Memory
  '../../../core/memory/MMU.cpp',
  
  # Core Graphics
  '../../../core/graphics/GPU.cpp',
  '../../../core/graphics/render3D.cpp',
  '../../../core/graphics/rasterize.cpp',
  '../../../core/graphics/texcache.cpp',
  '../../../core/graphics/gfx3d.cpp',
  '../../../core/graphics/matrix.cpp',
  
  # Core Audio
  '../../../core/audio/SPU.cpp',
  '../../../core/audio/metaspu/metaspu.cpp',
  
  # Core I/O
  '../../../core/io/wifi.cpp',
  '../../../core/io/slot1.cpp',
  '../../../core/io/slot2.cpp',
  '../../../core/io/rtc.cpp',
  '../../../core/io/mic.cpp',
  
  # Core Storage
  '../../../core/storage/firmware.cpp',
  '../../../core/storage/mc.cpp',
  '../../../core/storage/saves.cpp',
  
  # Core System
  '../../../core/system/NDSSystem.cpp',
  '../../../core/system/bios.cpp',
  
  # Platform SDL
  '../../../platform/sdl/audio/sndsdl.cpp',
  '../../../platform/sdl/input/ctrlssdl.cpp',
  
  # Platform Threading
  '../../../platform/threading/libretro-common/rthreads/async_job.c',
  '../../../platform/threading/libretro-common/rthreads/rsemaphore.c',
  '../../../platform/threading/libretro-common/rthreads/rthreads.c',
  
  # Utils Common
  '../../../utils/common/common.cpp',
  '../../../utils/common/path.cpp',
  '../../../utils/common/version.cpp',
  '../../../utils/common/advanscene.cpp',
  '../../../utils/common/datetime.cpp',
  '../../../utils/common/guid.cpp',
  '../../../utils/common/emufat.cpp',
  '../../../utils/common/fsnitro.cpp',
  '../../../utils/common/xstring.cpp',
  '../../../utils/common/task.cpp',
  '../../../utils/common/vfat.cpp',
  '../../../utils/common/dlditool.cpp',
  '../../../utils/common/colorspacehandler/colorspacehandler.cpp',
  
  # Utils File
  '../../../utils/file/emufile.cpp',
  '../../../utils/file/readwrite.cpp',
  '../../../utils/file/ROMReader.cpp',
  
  # Utils Debug
  '../../../utils/debug/debug.cpp',
  '../../../utils/debug/gdbstub/gdbstub.cpp',
  
  # Utils Database
  '../../../utils/database/Database.cpp',
  
  # Utils Movie
  '../../../utils/movie/movie.cpp',
  
  # Utils Cheats
  '../../../utils/cheats/cheatSystem.cpp',
  
  # Graphics Filters
  '../../../graphics/filters/2xsai.cpp',
  '../../../graphics/filters/bilinear.cpp',
  '../../../graphics/filters/deposterize.cpp',
  '../../../graphics/filters/epx.cpp',
  '../../../graphics/filters/hq2x.cpp',
  '../../../graphics/filters/hq3x.cpp',
  '../../../graphics/filters/hq4x.cpp',
  '../../../graphics/filters/lq2x.cpp',
  '../../../graphics/filters/scanline.cpp',
  '../../../graphics/filters/videofilter.cpp',
  '../../../graphics/filters/xbrz.cpp',
  
  # Addons
  '../../addons/slot2_auto.cpp', '../../addons/slot2_mpcf.cpp', '../../addons/slot2_paddle.cpp', '../../addons/slot2_gbagame.cpp', '../../addons/slot2_none.cpp', '../../addons/slot2_rumblepak.cpp', '../../addons/slot2_guitarGrip.cpp', '../../addons/slot2_hcv1000.cpp', '../../addons/slot2_expMemory.cpp', '../../addons/slot2_piano.cpp', '../../addons/slot2_passme.cpp', '../../addons/slot1_none.cpp', '../../addons/slot1_r4.cpp', '../../addons/slot1_retail_nand.cpp', '../../addons/slot1_retail_auto.cpp', '../../addons/slot1_retail_mcrom.cpp', '../../addons/slot1_retail_mcrom_debug.cpp', '../../addons/slot1comp_mc.cpp', '../../addons/slot1comp_rom.cpp', '../../addons/slot1comp_protocol.cpp',
  
  # Remaining files
  '../../../FIFO.cpp',
  '../../../encrypt.cpp',
  '../../../commandline.cpp',
  '../../../driver.cpp',
  '../../../frontend/modules/Disassembler.cpp',
  '../../../utils/decrypt/crc.cpp', '../../../utils/decrypt/decrypt.cpp',
  '../../../utils/decrypt/header.cpp',
  '../../../utils/libfat/cache.cpp',
  '../../../utils/libfat/directory.cpp',
  '../../../utils/libfat/disc.cpp',
  '../../../utils/libfat/fatdir.cpp',
  '../../../utils/libfat/fatfile.cpp',
  '../../../utils/libfat/filetime.cpp',
  '../../../utils/libfat/file_allocation_table.cpp',
  '../../../utils/libfat/libfat.cpp',
  '../../../utils/libfat/libfat_public_api.cpp',
  '../../../utils/libfat/lock.cpp',
  '../../../utils/libfat/partition.cpp',
  '../../../utils/tinyxml/tinystr.cpp',
  '../../../utils/tinyxml/tinyxml.cpp',
  '../../../utils/tinyxml/tinyxmlerror.cpp',
  '../../../utils/tinyxml/tinyxmlparser.cpp',
  '../../../libretro-common/compat/compat_getopt.c',
  '../../../libretro-common/file/file_path.c',
  '../../../libretro-common/compat/compat_strl.c',
  '../../../libretro-common/features/features_cpu.c',
  '../../../libretro-common/file/retro_dirent.c',
  '../../../libretro-common/file/retro_stat.c',
  '../../../libretro-common/encodings/encoding_utf.c',
]

# SDL files are already included in the main libdesmume_src list

if have_jit
  libdesmume_src += [
    '../../arm_jit.cpp',
    '../../utils/AsmJit/core/assembler.cpp',
    '../../utils/AsmJit/core/assert.cpp',
    '../../utils/AsmJit/core/buffer.cpp',
    '../../utils/AsmJit/core/compiler.cpp',
    '../../utils/AsmJit/core/compilercontext.cpp',
    '../../utils/AsmJit/core/compilerfunc.cpp',
    '../../utils/AsmJit/core/compileritem.cpp',
    '../../utils/AsmJit/core/context.cpp',
    '../../utils/AsmJit/core/cpuinfo.cpp',
    '../../utils/AsmJit/core/defs.cpp',
    '../../utils/AsmJit/core/func.cpp',
    '../../utils/AsmJit/core/logger.cpp',
    '../../utils/AsmJit/core/memorymanager.cpp',
    '../../utils/AsmJit/core/memorymarker.cpp',
    '../../utils/AsmJit/core/operand.cpp',
    '../../utils/AsmJit/core/stringbuilder.cpp',
    '../../utils/AsmJit/core/stringutil.cpp',
    '../../utils/AsmJit/core/virtualmemory.cpp',
    '../../utils/AsmJit/core/zonememory.cpp',
    '../../utils/AsmJit/x86/x86assembler.cpp',
    '../../utils/AsmJit/x86/x86compiler.cpp',
    '../../utils/AsmJit/x86/x86compilercontext.cpp',
    '../../utils/AsmJit/x86/x86compilerfunc.cpp',
    '../../utils/AsmJit/x86/x86compileritem.cpp',
    '../../utils/AsmJit/x86/x86cpuinfo.cpp',
    '../../utils/AsmJit/x86/x86defs.cpp',
    '../../utils/AsmJit/x86/x86func.cpp',
    '../../utils/AsmJit/x86/x86operand.cpp',
    '../../utils/AsmJit/x86/x86util.cpp',
  ]
endif

if get_option('opengles')
  if dep_gles.found()
    add_global_arguments('-DENABLE_OPENGL_ES', language: ['c', 'cpp'])
    dependencies += dep_gles
    # OpenGL files are already included in the main libdesmume_src list
  else
    message('Cannot build with OpenGL ES -- library not found.')
  endif
elif get_option('opengl')
  if dep_gl.found()
    add_global_arguments('-DENABLE_OPENGL_STANDARD', language: ['c', 'cpp'])
    dependencies += dep_gl
    # OpenGL files are already included in the main libdesmume_src list
  else
    message('Cannot build with OpenGL -- library not found.')
  endif
endif

if dep_openal.found()
  dependencies += dep_openal
  libdesmume_src += [
    '../../../frontend/posix/shared/mic_openal.cpp',
  ]
elif dep_alsa.found()
  dependencies += dep_alsa
  libdesmume_src += [
    '../../../frontend/posix/shared/mic_alsa.cpp',
  ]
else
  # mic.cpp is already included in the main libdesmume_src list
endif

if dep_soundtouch.found()
  dependencies += dep_soundtouch
  add_global_arguments('-DHAVE_LIBSOUNDTOUCH', language: ['c', 'cpp'])
  libdesmume_src += [
    '../../core/audio/metaspu/SndOut.cpp',
    '../../core/audio/metaspu/Timestretcher.cpp',
  ]
endif

if get_option('gdb-stub')
  add_global_arguments('-DGDB_STUB', language: ['c', 'cpp'])
  # gdbstub files are already included in the main libdesmume_src list
endif

simd = import('unstable-simd')
cxx = meson.get_compiler('cpp')
rval = simd.check('colorspacehandler',
  sse2: '../../../utils/common/colorspacehandler/colorspacehandler_SSE2.cpp',
  # TODO: add these two even in generic builds.
  #avx2: '../../utils/common/colorspacehandler/colorspacehandler_AVX2.cpp',
  #avx512: '../../utils/common/colorspacehandler/colorspacehandler_AVX512.cpp',
  # TODO: add support for this in meson.
  #altivec: '../../utils/common/colorspacehandler/colorspacehandler_AltiVec.cpp',
  compiler: cxx,
  include_directories: includes,
)
colorspacehandler_simd = rval[0]

library('desmume',
  libdesmume_src,
  dependencies: dependencies,
  include_directories: includes,
  link_with: colorspacehandler_simd,
)
