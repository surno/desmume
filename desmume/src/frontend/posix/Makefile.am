include desmume.mk

AM_CPPFLAGS += $(SDL_CFLAGS) $(GTHREAD_CFLAGS) $(X_CFLAGS) $(ALSA_CFLAGS) $(LIBAGG_CFLAGS) $(LIBSOUNDTOUCH_CFLAGS)



# from old stuff
# EXTRA_DIST = README.LIN README.MAC README.WIN dsm.txt dsv.txt autogen.sh $(PO_FILES_IN)
# DISTCLEANFILES = $(PO_FILES)
# SUBDIRS = $(PO_DIR) src

EXTRA_DIST = instruction_tabdef.inc thumb_tabdef.inc cocoa
if HAVE_GDB_STUB
SUBDIRS = . $(UI_DIR)
else
SUBDIRS = . $(UI_DIR)
endif
#DIST_SUBDIRS = . cli gtk
SUBDIRS += $(PO_DIR)
noinst_LIBRARIES = libdesmume.a
libdesmume_a_SOURCES = \
	# Core CPU
	../../core/cpu/armcpu.cpp ../../core/cpu/armcpu.h \
	../../core/cpu/arm_instructions.cpp \
	../../core/cpu/thumb_instructions.cpp \
	../../core/cpu/cp15.cpp ../../core/cpu/cp15.h \
	../../core/cpu/instructions.h \
	../../core/cpu/instruction_attributes.h \
	# Core Memory
	../../core/memory/MMU.cpp ../../core/memory/MMU.h \
	../../core/memory/MMU_timing.h \
	../../core/memory/mem.h \
	# Core Graphics
	../../core/graphics/GPU.cpp ../../core/graphics/GPU.h \
	../../core/graphics/GPU_osd.h \
	../../core/graphics/render3D.cpp ../../core/graphics/render3D.h \
	../../core/graphics/rasterize.cpp ../../core/graphics/rasterize.h \
	../../core/graphics/texcache.cpp ../../core/graphics/texcache.h \
	../../core/graphics/gfx3d.cpp ../../core/graphics/gfx3d.h \
	../../core/graphics/matrix.cpp ../../core/graphics/matrix.h \
	# Core Audio
	../../core/audio/SPU.cpp ../../core/audio/SPU.h \
	../../core/audio/metaspu/metaspu.cpp ../../core/audio/metaspu/metaspu.h \
	# Core I/O
	../../core/io/wifi.cpp ../../core/io/wifi.h \
	../../core/io/slot1.cpp ../../core/io/slot1.h \
	../../core/io/slot2.cpp ../../core/io/slot2.h \
	../../core/io/rtc.cpp ../../core/io/rtc.h \
	../../core/io/mic.cpp ../../core/io/mic.h \
	# Core Storage
	../../core/storage/firmware.cpp ../../core/storage/firmware.h \
	../../core/storage/mc.cpp ../../core/storage/mc.h \
	../../core/storage/saves.cpp ../../core/storage/saves.h \
	# Core System
	../../core/system/NDSSystem.cpp ../../core/system/NDSSystem.h \
	../../core/system/bios.cpp ../../core/system/bios.h \
	../../core/system/registers.h \
	# Platform OpenGL
	../../platform/opengl/OGLRender.cpp ../../platform/opengl/OGLRender.h \
	../../platform/opengl/OGLRender_3_2.cpp ../../platform/opengl/OGLRender_3_2.h \
	../../platform/opengl/OGLRender_ES3.cpp ../../platform/opengl/OGLRender_ES3.h \
	# Platform SDL
	../../platform/sdl/audio/sndsdl.cpp ../../platform/sdl/audio/sndsdl.h \
	../../platform/sdl/input/ctrlssdl.cpp ../../platform/sdl/input/ctrlssdl.h \
	../../platform/sdl/graphics/sdl_3Demu.cpp ../../platform/sdl/graphics/sdl_3Demu.h \
	../../platform/sdl/window/draw_sdl_window.cpp ../../platform/sdl/window/draw_sdl_window.h \
	# Utils Common
	../../utils/common/common.cpp ../../utils/common/common.h \
	../../utils/common/path.cpp ../../utils/common/path.h \
	../../utils/common/version.cpp ../../utils/common/version.h \
	../../utils/common/advanscene.cpp ../../utils/common/advanscene.h \
	../../utils/common/datetime.cpp ../../utils/common/datetime.h \
	../../utils/common/guid.cpp ../../utils/common/guid.h \
	../../utils/common/emufat.cpp ../../utils/common/emufat.h \
	../../utils/common/fsnitro.cpp ../../utils/common/fsnitro.h \
	../../utils/common/xstring.cpp ../../utils/common/xstring.h \
	../../utils/common/task.cpp ../../utils/common/task.h \
	../../utils/common/vfat.h ../../utils/common/vfat.cpp \
	../../utils/common/dlditool.cpp \
	../../utils/common/colorspacehandler/colorspacehandler.cpp ../../utils/common/colorspacehandler/colorspacehandler.h \
	# Utils File
	../../utils/file/emufile.h ../../utils/file/emufile.cpp \
	../../utils/file/readwrite.cpp ../../utils/file/readwrite.h \
	../../utils/file/ROMReader.cpp ../../utils/file/ROMReader.h \
	# Utils Debug
	../../utils/debug/debug.cpp ../../utils/debug/debug.h \
	# Utils Database
	../../utils/database/Database.cpp ../../utils/database/Database.h \
	# Utils Movie
	../../utils/movie/movie.cpp ../../utils/movie/movie.h \
	# Utils Cheats
	../../utils/cheats/cheatSystem.cpp ../../utils/cheats/cheatSystem.h \
	# Graphics Filters
	../../graphics/filters/2xsai.cpp ../../graphics/filters/bilinear.cpp ../../graphics/filters/deposterize.cpp ../../graphics/filters/epx.cpp ../../graphics/filters/filter.h \
	../../graphics/filters/hq2x.cpp ../../graphics/filters/hq2x.h \
	../../graphics/filters/hq3x.cpp ../../graphics/filters/hq3x.dat \
	../../graphics/filters/hq4x.cpp ../../graphics/filters/hq4x.dat \
	../../graphics/filters/interp.h ../../graphics/filters/lq2x.cpp ../../graphics/filters/lq2x.h ../../graphics/filters/scanline.cpp \
	../../graphics/filters/videofilter.cpp ../../graphics/filters/videofilter.h \
	../../graphics/filters/xbrz.cpp ../../graphics/filters/xbrz.h \
	# Addons
	../../addons/slot2_auto.cpp ../../addons/slot2_mpcf.cpp ../../addons/slot2_paddle.cpp ../../addons/slot2_gbagame.cpp ../../addons/slot2_none.cpp ../../addons/slot2_rumblepak.cpp ../../addons/slot2_guitarGrip.cpp ../../addons/slot2_hcv1000.cpp ../../addons/slot2_expMemory.cpp ../../addons/slot2_piano.cpp ../../addons/slot2_passme.cpp ../../addons/slot1_none.cpp ../../addons/slot1_r4.cpp ../../addons/slot1_retail_nand.cpp ../../addons/slot1_retail_auto.cpp ../../addons/slot1_retail_mcrom.cpp ../../addons/slot1_retail_mcrom_debug.cpp ../../addons/slot1comp_mc.cpp ../../addons/slot1comp_mc.h ../../addons/slot1comp_rom.h ../../addons/slot1comp_rom.cpp ../../addons/slot1comp_protocol.h ../../addons/slot1comp_protocol.cpp \
	# Remaining files
	../../FIFO.cpp ../../FIFO.h \
	../../encrypt.h ../../encrypt.cpp \
	../../commandline.h ../../commandline.cpp \
	../../driver.cpp ../../driver.h \
	../../PACKED.h ../../PACKED_END.h \
	../../types.h \
	../../frontend/modules/Disassembler.cpp ../../frontend/modules/Disassembler.h \
	../../utils/decrypt/crc.cpp ../../utils/decrypt/crc.h ../../utils/decrypt/decrypt.cpp \
	../../utils/decrypt/decrypt.h ../../utils/decrypt/header.cpp ../../utils/decrypt/header.h \
	../../utils/libfat/bit_ops.h \
	../../utils/libfat/cache.cpp \
	../../utils/libfat/cache.h \
	../../utils/libfat/common.h \
	../../utils/libfat/directory.cpp \
	../../utils/libfat/directory.h \
	../../utils/libfat/disc.cpp \
	../../utils/libfat/disc.h \
	../../utils/libfat/disc_io.h \
	../../utils/libfat/fat.h \
	../../utils/libfat/fatdir.cpp \
	../../utils/libfat/fatdir.h \
	../../utils/libfat/fatfile.cpp \
	../../utils/libfat/fatfile.h \
	../../utils/libfat/filetime.cpp \
	../../utils/libfat/filetime.h \
	../../utils/libfat/file_allocation_table.cpp \
	../../utils/libfat/file_allocation_table.h \
	../../utils/libfat/libfat.cpp \
	../../utils/libfat/libfat_pc.h \
	../../utils/libfat/libfat_public_api.cpp \
	../../utils/libfat/libfat_public_api.h \
	../../utils/libfat/lock.cpp \
	../../utils/libfat/lock.h \
	../../utils/libfat/mem_allocate.h \
	../../utils/libfat/partition.cpp \
	../../utils/libfat/partition.h \
	../../utils/tinyxml/tinystr.cpp \
	../../utils/tinyxml/tinystr.h \
	../../utils/tinyxml/tinyxml.cpp \
	../../utils/tinyxml/tinyxml.h \
	../../utils/tinyxml/tinyxmlerror.cpp \
	../../utils/tinyxml/tinyxmlparser.cpp \
	../../utils/glcorearb.h \
	../../libretro-common/compat/compat_getopt.c \
	../../libretro-common/file/file_path.c \
	../../libretro-common/compat/compat_strl.c \
	../../libretro-common/features/features_cpu.c \
	../../libretro-common/file/retro_dirent.c \
	../../libretro-common/file/retro_stat.c \
	../../platform/threading/libretro-common/rthreads/async_job.c \
	../../platform/threading/libretro-common/rthreads/rsemaphore.c \
	../../platform/threading/libretro-common/rthreads/rthreads.c \
	../../libretro-common/encodings/encoding_utf.c \
	../../agg2d.h ../../agg2d.inl \
	../../bits.h

if HAVE_JIT
libdesmume_a_SOURCES += \
	../../arm_jit.h ../../instruction_attributes.h

if HOST_CPU_KIND_X86
libdesmume_a_SOURCES += \
	../../arm_jit.cpp \
	../../utils/AsmJit/AsmJit.h \
	../../utils/AsmJit/Config.h \
	../../utils/AsmJit/core.h \
	../../utils/AsmJit/x86.h \
	../../utils/AsmJit/core/apibegin.h \
	../../utils/AsmJit/core/apiend.h \
	../../utils/AsmJit/core/assembler.cpp \
	../../utils/AsmJit/core/assembler.h \
	../../utils/AsmJit/core/assert.cpp \
	../../utils/AsmJit/core/assert.h \
	../../utils/AsmJit/core/buffer.cpp \
	../../utils/AsmJit/core/buffer.h \
	../../utils/AsmJit/core/build.h \
	../../utils/AsmJit/core/compiler.cpp \
	../../utils/AsmJit/core/compiler.h \
	../../utils/AsmJit/core/compilercontext.cpp \
	../../utils/AsmJit/core/compilercontext.h \
	../../utils/AsmJit/core/compilerfunc.cpp \
	../../utils/AsmJit/core/compilerfunc.h \
	../../utils/AsmJit/core/compileritem.cpp \
	../../utils/AsmJit/core/compileritem.h \
	../../utils/AsmJit/core/context.cpp \
	../../utils/AsmJit/core/context.h \
	../../utils/AsmJit/core/cpuinfo.cpp \
	../../utils/AsmJit/core/cpuinfo.h \
	../../utils/AsmJit/core/defs.cpp \
	../../utils/AsmJit/core/defs.h \
	../../utils/AsmJit/core/func.cpp \
	../../utils/AsmJit/core/func.h \
	../../utils/AsmJit/core/intutil.h \
	../../utils/AsmJit/core/lock.h \
	../../utils/AsmJit/core/logger.cpp \
	../../utils/AsmJit/core/logger.h \
	../../utils/AsmJit/core/memorymanager.cpp \
	../../utils/AsmJit/core/memorymanager.h \
	../../utils/AsmJit/core/memorymarker.cpp \
	../../utils/AsmJit/core/memorymarker.h \
	../../utils/AsmJit/core/operand.cpp \
	../../utils/AsmJit/core/operand.h \
	../../utils/AsmJit/core/podvector.h \
	../../utils/AsmJit/core/stringbuilder.cpp \
	../../utils/AsmJit/core/stringbuilder.h \
	../../utils/AsmJit/core/stringutil.cpp \
	../../utils/AsmJit/core/stringutil.h \
	../../utils/AsmJit/core/virtualmemory.cpp \
	../../utils/AsmJit/core/virtualmemory.h \
	../../utils/AsmJit/core/zonememory.cpp \
	../../utils/AsmJit/core/zonememory.h \
	../../utils/AsmJit/x86/x86assembler.cpp \
	../../utils/AsmJit/x86/x86assembler.h \
	../../utils/AsmJit/x86/x86compiler.cpp \
	../../utils/AsmJit/x86/x86compiler.h \
	../../utils/AsmJit/x86/x86compilercontext.cpp \
	../../utils/AsmJit/x86/x86compilercontext.h \
	../../utils/AsmJit/x86/x86compilerfunc.cpp \
	../../utils/AsmJit/x86/x86compilerfunc.h \
	../../utils/AsmJit/x86/x86compileritem.cpp \
	../../utils/AsmJit/x86/x86compileritem.h \
	../../utils/AsmJit/x86/x86cpuinfo.cpp \
	../../utils/AsmJit/x86/x86cpuinfo.h \
	../../utils/AsmJit/x86/x86defs.cpp \
	../../utils/AsmJit/x86/x86defs.h \
	../../utils/AsmJit/x86/x86func.cpp \
	../../utils/AsmJit/x86/x86func.h \
	../../utils/AsmJit/x86/x86operand.cpp \
	../../utils/AsmJit/x86/x86operand.h \
	../../utils/AsmJit/x86/x86util.cpp \
	../../utils/AsmJit/x86/x86util.h
else
if HOST_CPU_KIND_ARM
libdesmume_a_SOURCES += \
	../../utils/arm_jit/arm_jit_arm.cpp
endif
endif

endif

libdesmume_a_SOURCES += shared/desmume_config.cpp shared/desmume_config.h

if ENABLE_OPENGL_ES
# OpenGL files are already included in the main libdesmume_a_SOURCES list
else
if ENABLE_OPENGL_STANDARD
# OpenGL files are already included in the main libdesmume_a_SOURCES list
endif
endif

if HAVE_OPENAL
libdesmume_a_SOURCES += shared/mic_openal.cpp
else
if HAVE_ALSA
libdesmume_a_SOURCES += shared/mic_alsa.cpp
else
# mic.cpp is already included in the main libdesmume_a_SOURCES list
endif
endif

if HAVE_LIBSOUNDTOUCH
libdesmume_a_SOURCES += ../../core/audio/metaspu/SndOut.cpp ../../core/audio/metaspu/SndOut.h ../../core/audio/metaspu/Timestretcher.cpp
endif

if HAVE_LIBAGG
libdesmume_a_SOURCES += ../../frontend/modules/osd/agg/aggdraw.cpp ../../frontend/modules/osd/agg/aggdraw.h ../../frontend/modules/osd/agg/agg_osd.cpp
endif
if HAVE_LUA
AM_CPPFLAGS += $(LUA_CFLAGS)
libdesmume_a_SOURCES += ../../lua-engine.cpp
endif
if HAVE_GDB_STUB
libdesmume_a_SOURCES += ../../utils/debug/gdbstub/gdbstub.cpp
libdesmume_a_SOURCES += ../../utils/debug/gdbstub/gdbstub_internal.h
libdesmume_a_SOURCES += ../../utils/debug/gdbstub.h
endif
