/*
	Copyright (C) 2006 yopyop
	Copyright (C) 2006-2007 shash
	Copyright (C) 2008-2021 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef INCLUDE_SDL
#include <SDL.h>
#endif
#include "interface.h"

#include <stdio.h>
#include "../../NDSSystem.h"
#include "../../GPU.h"
#include "../../SPU.h"
#include "../../MMU.h"
#include "../../rasterize.h"
#include "../../saves.h"
#include "../../movie.h"
#include "../../mc.h"
#include "../../firmware.h"
#include "../../armcpu.h"
#include "../../arm_jit.h"
#ifdef ENABLE_APPLE_METAL
#include "../../MetalRender.h"
#endif
#ifdef INCLUDE_SDL
#include "../posix/shared/sndsdl.h"
#include "../posix/shared/ctrlssdl.h"
#endif
#include <locale>
#include <codecvt>
#include <string>

#ifdef ENABLE_APPLE_METAL
extern "C" {
    extern int desmume_metal_bootstrap_init(void);
    extern void desmume_metal_bootstrap_cleanup(void);
}
#endif

#define SCREENS_PIXEL_SIZE 98304
volatile bool execute = false;
TieredRegion hooked_regions [HOOK_COUNT];
std::map<unsigned int, memory_cb_fnc> hooks[HOOK_COUNT];

SoundInterface_struct *SNDCoreList[] = {
        &SNDDummy,
#ifdef INCLUDE_SDL
        &SNDSDL,
#else
        &SNDDummy,
#endif
        NULL
};
GPU3DInterface *core3DList[] = {
        &gpu3DNull,
        &gpu3DRasterize,
#ifdef ENABLE_APPLE_METAL
        &gpu3DMetal,
#endif
        NULL
};

std::wstring s2ws(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
    return converter.from_bytes(str);
}

EXPORTED int desmume_init()
{
    // Pure headless initialization - no SDL, dummy audio
    NDS_Init();
    
    // Use dummy audio by default (silent, no SDL dependency)
    SPU_ChangeSoundCore(SNDCORE_DUMMY, 0);
    
    // Use software rasterizer by default
    GPU->Change3DRendererByID(RENDERID_SOFTRASTERIZER);
    
    execute = false;
    return 0;
}

EXPORTED int desmume_init_sdl()
{
#ifdef INCLUDE_SDL
    // Only init timer - video is for draw_window_init()
    if (SDL_WasInit(SDL_INIT_TIMER) == 0) {
        if (SDL_Init(SDL_INIT_TIMER) == -1) {
            fprintf(stderr, "SDL_Init(TIMER) failed: %s\n", SDL_GetError());
            return -1;
        }
    }
    return 0;
#else
    return -1;  // SDL not compiled in
#endif
}

EXPORTED int desmume_init_with_options(const DesmumeInitOptions *options)
{
    if (options == NULL) {
        return desmume_init();  // Fall back to defaults
    }
    
    NDS_Init();
    
    // Audio core selection
    int audio_core = options->audio_core;
    int buffer_size = options->audio_buffer_size > 0 
                      ? options->audio_buffer_size 
                      : 735 * 4;  // Default buffer
    
#ifndef INCLUDE_SDL
    // Force dummy if SDL requested but not compiled in
    if (audio_core == DESMUME_AUDIO_SDL) {
        audio_core = DESMUME_AUDIO_DUMMY;
    }
#endif
    
    if (SPU_ChangeSoundCore(audio_core, buffer_size) == 0) {
        // Success: configure the audio core
        SPU_SetSynchMode(0, 0);
        SPU_SetVolume(100);
    } else {
        // Failure: fallback to dummy (no configuration needed)
        SPU_ChangeSoundCore(SNDCORE_DUMMY, 0);
    }
    
    // 3D renderer selection (enum values match RENDERID_* constants)
    int renderer = options->renderer_3d;
#ifndef ENABLE_APPLE_METAL
    if (renderer == DESMUME_RENDERER_METAL) {
        renderer = DESMUME_RENDERER_SOFTRASTERIZER;
    }
#endif
    GPU->Change3DRendererByID(renderer);
    
    // SDL timer (optional)
    if (options->init_sdl_timer) {
#ifdef INCLUDE_SDL
        if (SDL_Init(SDL_INIT_TIMER) == -1) {
            fprintf(stderr, "SDL_Init(TIMER) failed: %s\n", SDL_GetError());
            // Non-fatal: continue without timer
        }
#endif
    }
    
    execute = false;
    return 0;
}

EXPORTED int desmume_audio_set_core(int core_id, int buffer_size)
{
#ifndef INCLUDE_SDL
    if (core_id == DESMUME_AUDIO_SDL) {
        return -1;  // SDL not available
    }
#endif
    
    int buf = buffer_size > 0 ? buffer_size : 735 * 4;
    return SPU_ChangeSoundCore(core_id, buf);
}

EXPORTED int desmume_audio_get_core(void)
{
    return SPU_currentCoreNum;
}

EXPORTED void desmume_free()
{
    execute = false;
    NDS_DeInit();
    
#ifdef ENABLE_APPLE_METAL
    desmume_metal_bootstrap_cleanup();
#endif

#ifdef INCLUDE_SDL
    // Only quit if SDL was initialized
    if (SDL_WasInit(0) != 0) {
        SDL_Quit();
    }
#endif
}

EXPORTED void desmume_set_language(u8 lang)
{
    CommonSettings.fwConfig.language = lang;
}

EXPORTED int desmume_open(const char *filename)
{
    int i;
    clear_savestates();
    i = NDS_LoadROM(filename);
    return i;
}

EXPORTED void desmume_set_savetype(int type) {
    backup_setManualBackupType(type);
}


EXPORTED void desmume_pause()
{
    execute = false;
    SPU_Pause(1);
}

EXPORTED void desmume_resume()
{
    execute = true;
    SPU_Pause(0);
}

EXPORTED void desmume_reset()
{
    NDS_Reset();
    desmume_resume();
}

EXPORTED BOOL desmume_running()
{
    return execute;
}

EXPORTED void desmume_skip_next_frame()
{
    NDS_SkipNextFrame();
}

EXPORTED void desmume_cycle(BOOL with_joystick)
{
#ifdef INCLUDE_SDL
    u16 keypad;
    /* Joystick events */
    if (with_joystick) {
        /* Retrieve old value: can use joysticks w/ another device (from our side) */
        keypad = get_keypad();
        /* Process joystick events if any */
        process_joystick_events(&keypad);
        /* Update keypad value */
        update_keypad(keypad);
    }
#endif
    NDS_beginProcessingInput();
    {
        FCEUMOV_AddInputState();
    }
    NDS_endProcessingInput();

    NDS_exec<false>();
    SPU_Emulate_user();
}

EXPORTED int desmume_sdl_get_ticks()
{
#ifdef INCLUDE_SDL
    if (SDL_WasInit(SDL_INIT_TIMER)) {
        return SDL_GetTicks();
    }
#endif
    return 0;  // Return 0 if SDL timer not initialized
}

#ifdef INCLUDE_OPENGL_2D
EXPORTED void desmume_draw_opengl(GLuint *texture)
{
#ifdef HAVE_LIBAGG
    //TODO : osd->update();
    //TODO : DrawHUD();
#endif
    const NDSDisplayInfo &displayInfo = GPU->GetDisplayInfo();

    /* Clear The Screen And The Depth Buffer */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Move Into The Screen 5 Units */
    glLoadIdentity();

    /* Draw the main screen as a textured quad */
    glBindTexture(GL_TEXTURE_2D, texture[NDSDisplayID_Main]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT,
                  GL_RGBA,
                  GL_UNSIGNED_SHORT_1_5_5_5_REV,
                  displayInfo.renderedBuffer[NDSDisplayID_Main]);

    GLfloat backlightIntensity = displayInfo.backlightIntensity[NDSDisplayID_Main];

    glBegin(GL_QUADS);
        glTexCoord2f(0.00f, 0.00f); glVertex2f(  0.0f,   0.0f); glColor4f(backlightIntensity, backlightIntensity, backlightIntensity, 1.0f);
        glTexCoord2f(1.00f, 0.00f); glVertex2f(256.0f,   0.0f); glColor4f(backlightIntensity, backlightIntensity, backlightIntensity, 1.0f);
        glTexCoord2f(1.00f, 0.75f); glVertex2f(256.0f, 192.0f); glColor4f(backlightIntensity, backlightIntensity, backlightIntensity, 1.0f);
        glTexCoord2f(0.00f, 0.75f); glVertex2f(  0.0f, 192.0f); glColor4f(backlightIntensity, backlightIntensity, backlightIntensity, 1.0f);
    glEnd();

    /* Draw the touch screen as a textured quad */
    glBindTexture(GL_TEXTURE_2D, texture[NDSDisplayID_Touch]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT,
                  GL_RGBA,
                  GL_UNSIGNED_SHORT_1_5_5_5_REV,
                  displayInfo.renderedBuffer[NDSDisplayID_Touch]);

    backlightIntensity = displayInfo.backlightIntensity[NDSDisplayID_Touch];

    glBegin(GL_QUADS);
        glTexCoord2f(0.00f, 0.00f); glVertex2f(  0.0f, 192.0f); glColor4f(backlightIntensity, backlightIntensity, backlightIntensity, 1.0f);
        glTexCoord2f(1.00f, 0.00f); glVertex2f(256.0f, 192.0f); glColor4f(backlightIntensity, backlightIntensity, backlightIntensity, 1.0f);
        glTexCoord2f(1.00f, 0.75f); glVertex2f(256.0f, 384.0f); glColor4f(backlightIntensity, backlightIntensity, backlightIntensity, 1.0f);
        glTexCoord2f(0.00f, 0.75f); glVertex2f(  0.0f, 384.0f); glColor4f(backlightIntensity, backlightIntensity, backlightIntensity, 1.0f);
    glEnd();
#ifdef HAVE_LIBAGG
    //TODO : osd->clear();
#endif
}
EXPORTED extern BOOL desmume_has_opengl()
{
    return true;
}
#else
EXPORTED extern BOOL desmume_has_opengl()
{
    return false;
}
#endif

#ifdef ENABLE_APPLE_METAL
EXPORTED int desmume_init_metal()
{
    int result = desmume_metal_bootstrap_init();
    if (result == 0) {
        // Try to switch to Metal renderer
        if (!GPU->Change3DRendererByID(RENDERID_METAL)) {
            // Fall back to software rasterizer if Metal renderer fails
            GPU->Change3DRendererByID(RENDERID_SOFTRASTERIZER);
            return -1;
        }
    }
    return result;
}

EXPORTED BOOL desmume_has_metal()
{
    return true;
}
#else
EXPORTED int desmume_init_metal()
{
    return -1;
}

EXPORTED BOOL desmume_has_metal()
{
    return false;
}
#endif

// SDL drawing is in draw_sdl_window.cpp

EXPORTED u16 *desmume_draw_raw()
{
    const NDSDisplayInfo &displayInfo = GPU->GetDisplayInfo();
    const size_t pixCount = GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT;
    ColorspaceApplyIntensityToBuffer16<false, false>(displayInfo.nativeBuffer16[NDSDisplayID_Main],  pixCount, displayInfo.backlightIntensity[NDSDisplayID_Main]);
    ColorspaceApplyIntensityToBuffer16<false, false>(displayInfo.nativeBuffer16[NDSDisplayID_Touch], pixCount, displayInfo.backlightIntensity[NDSDisplayID_Touch]);

    return displayInfo.masterNativeBuffer16;
}

EXPORTED void desmume_draw_raw_as_rgbx(u8 *buffer)
{
    u16 *gpuFramebuffer = desmume_draw_raw();

    for (int i = 0; i < SCREENS_PIXEL_SIZE; i++) {
        buffer[(i * 4) + 2] = ((gpuFramebuffer[i] >> 0) & 0x1f) << 3;
        buffer[(i * 4) + 1] = ((gpuFramebuffer[i] >> 5) & 0x1f) << 3;
        buffer[(i * 4) + 0] = ((gpuFramebuffer[i] >> 10) & 0x1f) << 3;
    }
}

EXPORTED void desmume_savestate_clear()
{
    clear_savestates();
}

EXPORTED BOOL desmume_savestate_load(const char *file_name)
{
    return savestate_load(file_name);
}

EXPORTED BOOL desmume_savestate_save(const char *file_name)
{
    return savestate_save(file_name);
}

EXPORTED void desmume_savestate_scan()
{
    scan_savestates();
}

EXPORTED void desmume_savestate_slot_load(int index)
{
    loadstate_slot(index);
}

EXPORTED void desmume_savestate_slot_save(int index)
{
    savestate_slot(index);
}

EXPORTED BOOL desmume_savestate_slot_exists(int index)
{
    return savestates[index].exists;
}

EXPORTED char* desmume_savestate_slot_date(int index)
{
    return savestates[index].date;
}

EXPORTED BOOL desmume_gpu_get_layer_main_enable_state(int layer_index)
{
    return GPU->GetEngineMain()->GetLayerEnableState(layer_index);
}

EXPORTED BOOL desmume_gpu_get_layer_sub_enable_state(int layer_index)
{
    return GPU->GetEngineSub()->GetLayerEnableState(layer_index);
}

EXPORTED void desmume_gpu_set_layer_main_enable_state(int layer_index, BOOL the_state)
{
    GPU->GetEngineMain()->SetLayerEnableState(layer_index, the_state);
}

EXPORTED void desmume_gpu_set_layer_sub_enable_state(int layer_index, BOOL the_state)
{
    GPU->GetEngineSub()->SetLayerEnableState(layer_index, the_state);
}

EXPORTED int desmume_volume_get()
{
    // Use SPU's internal volume tracking (works with any backend)
    return SPU_GetVolume();
}

EXPORTED void desmume_volume_set(int volume)
{
    SPU_SetVolume(volume);
}

EXPORTED unsigned char desmume_memory_read_byte(int address)
{
    return (unsigned char)(_MMU_read08<ARMCPU_ARM9>(address) & 0xFF);
}

EXPORTED signed char desmume_memory_read_byte_signed(int address)
{
    return (signed char)(_MMU_read08<ARMCPU_ARM9>(address) & 0xFF);
}

EXPORTED unsigned short desmume_memory_read_short(int address)
{
    return (unsigned short)(_MMU_read16<ARMCPU_ARM9>(address) & 0xFFFF);
}

EXPORTED signed short desmume_memory_read_short_signed(int address)
{
    return (signed short)(_MMU_read16<ARMCPU_ARM9>(address) & 0xFFFF);
}

EXPORTED unsigned long desmume_memory_read_long(int address)
{
    return (unsigned long)(_MMU_read32<ARMCPU_ARM9>(address));
}

EXPORTED signed long desmume_memory_read_long_signed(int address)
{
    return (signed long)(_MMU_read32<ARMCPU_ARM9>(address));
}

EXPORTED void desmume_memory_write_byte(int address, unsigned char value)
{
    _MMU_write08<ARMCPU_ARM9>(address, value);
}

EXPORTED void desmume_memory_write_short(int address, unsigned short value)
{
    _MMU_write16<ARMCPU_ARM9>(address, value);
}

EXPORTED void desmume_memory_write_long(int address, unsigned long value)
{
    _MMU_write32<ARMCPU_ARM9>(address, value);
}

struct registerPointerMap
{
    const char* registerName;
    unsigned int* pointer;
    int dataSize;
};

#define RPM_ENTRY(name,var) {name, (unsigned int*)&var, sizeof(var)},

registerPointerMap arm9PointerMap [] = {
	RPM_ENTRY("r0", NDS_ARM9.R[0])
	RPM_ENTRY("r1", NDS_ARM9.R[1])
	RPM_ENTRY("r2", NDS_ARM9.R[2])
	RPM_ENTRY("r3", NDS_ARM9.R[3])
	RPM_ENTRY("r4", NDS_ARM9.R[4])
	RPM_ENTRY("r5", NDS_ARM9.R[5])
	RPM_ENTRY("r6", NDS_ARM9.R[6])
	RPM_ENTRY("r7", NDS_ARM9.R[7])
	RPM_ENTRY("r8", NDS_ARM9.R[8])
	RPM_ENTRY("r9", NDS_ARM9.R[9])
	RPM_ENTRY("r10", NDS_ARM9.R[10])
	RPM_ENTRY("r11", NDS_ARM9.R[11])
	RPM_ENTRY("r12", NDS_ARM9.R[12])
	RPM_ENTRY("r13", NDS_ARM9.R[13])
	RPM_ENTRY("r14", NDS_ARM9.R[14])
	RPM_ENTRY("r15", NDS_ARM9.R[15])
	RPM_ENTRY("cpsr", NDS_ARM9.CPSR.val)
	RPM_ENTRY("spsr", NDS_ARM9.SPSR.val)
	{}
};
registerPointerMap arm7PointerMap [] = {
	RPM_ENTRY("r0", NDS_ARM7.R[0])
	RPM_ENTRY("r1", NDS_ARM7.R[1])
	RPM_ENTRY("r2", NDS_ARM7.R[2])
	RPM_ENTRY("r3", NDS_ARM7.R[3])
	RPM_ENTRY("r4", NDS_ARM7.R[4])
	RPM_ENTRY("r5", NDS_ARM7.R[5])
	RPM_ENTRY("r6", NDS_ARM7.R[6])
	RPM_ENTRY("r7", NDS_ARM7.R[7])
	RPM_ENTRY("r8", NDS_ARM7.R[8])
	RPM_ENTRY("r9", NDS_ARM7.R[9])
	RPM_ENTRY("r10", NDS_ARM7.R[10])
	RPM_ENTRY("r11", NDS_ARM7.R[11])
	RPM_ENTRY("r12", NDS_ARM7.R[12])
	RPM_ENTRY("r13", NDS_ARM7.R[13])
	RPM_ENTRY("r14", NDS_ARM7.R[14])
	RPM_ENTRY("r15", NDS_ARM7.R[15])
	RPM_ENTRY("cpsr", NDS_ARM7.CPSR.val)
	RPM_ENTRY("spsr", NDS_ARM7.SPSR.val)
	{}
};

struct cpuToRegisterMap
{
	const char* cpuName;
	registerPointerMap* rpmap;
}
cpuToRegisterMaps [] =
{
	{"arm9.", arm9PointerMap},
	{"main.", arm9PointerMap},
	{"arm7.", arm7PointerMap},
	{"sub.",  arm7PointerMap},
	{"", arm9PointerMap},
};

EXPORTED u32 desmume_memory_read_register(char* register_name)
{
	for(int cpu = 0; cpu < sizeof(cpuToRegisterMaps)/sizeof(*cpuToRegisterMaps); cpu++)
	{
		cpuToRegisterMap ctrm = cpuToRegisterMaps[cpu];
		int cpuNameLen = strlen(ctrm.cpuName);
		if(!strncasecmp(register_name, ctrm.cpuName, cpuNameLen))
		{
            register_name += cpuNameLen;
			for(int reg = 0; ctrm.rpmap[reg].dataSize; reg++)
			{
				registerPointerMap rpm = ctrm.rpmap[reg];
				if(!strcasecmp(register_name, rpm.registerName))
				{
					switch(rpm.dataSize)
					{ default:
					case 1: return *(unsigned char*)rpm.pointer;
					case 2: return *(u16*)rpm.pointer;
					case 4: return *(u32*)rpm.pointer;
					}
				}
			}
			return 0;
		}
	}
	return 0;
}

EXPORTED void desmume_memory_write_register(char* register_name, u32 value)
{
	for(int cpu = 0; cpu < sizeof(cpuToRegisterMaps)/sizeof(*cpuToRegisterMaps); cpu++)
	{
		cpuToRegisterMap ctrm = cpuToRegisterMaps[cpu];
		int cpuNameLen = strlen(ctrm.cpuName);
		if(!strncasecmp(register_name, ctrm.cpuName, cpuNameLen))
		{
			register_name += cpuNameLen;
			for(int reg = 0; ctrm.rpmap[reg].dataSize; reg++)
			{
				registerPointerMap rpm = ctrm.rpmap[reg];
				if(!strcasecmp(register_name, rpm.registerName))
				{
					switch(rpm.dataSize)
					{ default:
					case 1: *(unsigned char*)rpm.pointer = (unsigned char)(value & 0xFF); break;
					case 2: *(u16*)rpm.pointer = (u16)(value & 0xFFFF); break;
					case 4: *(u32*)rpm.pointer = value; break;
					}
				}
			}
			return;
		}
	}
}

EXPORTED u32 desmume_memory_get_next_instruction()
{
    return CommonSettings.use_jit ? 0 : NDS_ARM9.next_instruction;
}

EXPORTED void desmume_memory_set_next_instruction(u32 value)
{
    if (!CommonSettings.use_jit) {
        NDS_ARM9.next_instruction = value;
    }
}

// CPU Emulation Engine (JIT) Configuration
EXPORTED BOOL desmume_get_jit_enabled(void)
{
#ifdef HAVE_JIT
    return CommonSettings.use_jit;
#else
    return false;
#endif
}

EXPORTED void desmume_set_jit_enabled(BOOL enabled)
{
#ifdef HAVE_JIT
    CommonSettings.use_jit = (enabled != 0);
    arm_jit_reset(CommonSettings.use_jit, true);  // suppress_msg = true for headless
#else
    // JIT not available on this platform, silently ignore
#endif
}

EXPORTED u32 desmume_get_jit_max_block_size(void)
{
#ifdef HAVE_JIT
    return CommonSettings.jit_max_block_size;
#else
    return 0;
#endif
}

EXPORTED void desmume_set_jit_max_block_size(u32 size)
{
#ifdef HAVE_JIT
    // Validate range: 1-100 (matching Windows frontend validation)
    if (size < 1) {
        CommonSettings.jit_max_block_size = 1;
    } else if (size > 100) {
        CommonSettings.jit_max_block_size = 100;
    } else {
        CommonSettings.jit_max_block_size = size;
    }
    
    // If JIT is currently enabled, reset it to apply new block size
    if (CommonSettings.use_jit) {
        arm_jit_reset(CommonSettings.use_jit, true);
    }
#else
    // JIT not available on this platform, silently ignore
#endif
}

INLINE void memory_register_hook(int addr, MemHookType hook_type, int size, memory_cb_fnc cb)
{
	for(unsigned int i = addr; i != addr+size; i++)
	{
		hooks[hook_type][i] = cb;
	}
    std::vector<unsigned int> hooked_bytes;
    for(std::map<unsigned int, memory_cb_fnc>::iterator it = hooks[hook_type].begin(); it != hooks[hook_type].end(); ++it) {
        hooked_bytes.push_back(it->first);
    }
    hooked_regions[hook_type].Calculate(hooked_bytes);
}

EXPORTED void desmume_memory_register_write(int address, int size, memory_cb_fnc cb)
{
	memory_register_hook(address, HOOK_WRITE, size, cb);
}

EXPORTED void desmume_memory_register_read(int address, int size, memory_cb_fnc cb)
{
	memory_register_hook(address, HOOK_READ, size, cb);
}

EXPORTED void desmume_memory_register_exec(int address, int size, memory_cb_fnc cb)
{
	memory_register_hook(address, HOOK_EXEC, size, cb);
}

EXPORTED void desmume_screenshot(char *screenshot_buffer)
{
    u16 *gpuFramebuffer = GPU->GetDisplayInfo().masterNativeBuffer16;
    static int seq = 0;

    for (int i = 0; i < SCREENS_PIXEL_SIZE; i++) {
        screenshot_buffer[(i * 3) + 0] = ((gpuFramebuffer[i] >> 0) & 0x1f) << 3;
        screenshot_buffer[(i * 3) + 1] = ((gpuFramebuffer[i] >> 5) & 0x1f) << 3;
        screenshot_buffer[(i * 3) + 2] = ((gpuFramebuffer[i] >> 10) & 0x1f) << 3;
    }

}

#ifdef INCLUDE_SDL
EXPORTED BOOL desmume_input_joy_init(void)
{
    return (BOOL) init_joy();
}

EXPORTED void desmume_input_joy_uninit(void)
{
    return uninit_joy();
}

EXPORTED u16 desmume_input_joy_number_connected(void)
{
    return get_number_of_joysticks();
}

EXPORTED u16 desmume_input_joy_get_key(int index)
{
    return get_joy_key(index);
}

EXPORTED u16 desmume_input_joy_get_set_key(int index)
{
    return get_set_joy_key(index);
}

EXPORTED void desmume_input_joy_set_key(int index, int joystick_key_index)
{
    joypad_cfg[index] = joystick_key_index;
}

EXPORTED void desmume_input_keypad_update(u16 keys)
{
    update_keypad(keys);
}

EXPORTED u16 desmume_input_keypad_get(void)
{
    return get_keypad();
}
#endif

EXPORTED void desmume_input_set_touch_pos(u16 x, u16 y)
{
    NDS_setTouchPos(x, y);
}

EXPORTED void desmume_input_release_touch()
{
    NDS_releaseTouch();
}

EXPORTED BOOL desmume_movie_is_active()
{
    return movieMode != MOVIEMODE_INACTIVE;
}

EXPORTED BOOL desmume_movie_is_recording()
{
    return movieMode == MOVIEMODE_RECORD;
}

EXPORTED BOOL desmume_movie_is_playing()
{
    return movieMode == MOVIEMODE_PLAY;
}

EXPORTED BOOL desmume_movie_is_finished()
{
    return movieMode == MOVIEMODE_FINISHED;
}

EXPORTED int desmume_movie_get_length()
{
    return currMovieData.records.size();
}

EXPORTED char *desmume_movie_get_name()
{
    extern char curMovieFilename[512];
    return curMovieFilename;
}

EXPORTED int desmume_movie_get_rerecord_count()
{
    return currMovieData.rerecordCount;
}

EXPORTED void desmume_movie_set_rerecord_count(int count)
{
    currMovieData.rerecordCount = count;
}

EXPORTED BOOL desmume_movie_get_readonly()
{
    return movie_readonly;
}

EXPORTED void desmume_movie_set_readonly(BOOL state)
{
    movie_readonly = state;
}

EXPORTED const char* desmume_movie_play(const char *file_name)
{
    return FCEUI_LoadMovie(file_name, true, false, 0);
}

EXPORTED void desmume_movie_record_simple(const char *save_file_name, const char *author_name)
{
    std::string s_author_name = author_name;
    FCEUI_SaveMovie(save_file_name, s2ws(s_author_name), START_BLANK, "", DateTime::get_Now());
}

EXPORTED void desmume_movie_record(const char *save_file_name, const char *author_name, START_FROM start_from, const char* sram_file_name)
{
    std::string s_author_name = author_name;
    std::string s_sram_file_name = sram_file_name;
    FCEUI_SaveMovie(save_file_name, s2ws(s_author_name), start_from, s_sram_file_name, DateTime::get_Now());
}

EXPORTED void desmume_movie_record_from_date(const char *save_file_name, const char *author_name, START_FROM start_from, const char* sram_file_name, SimpleDate date)
{
    std::string s_author_name = author_name;
    std::string s_sram_file_name = sram_file_name;
    FCEUI_SaveMovie(save_file_name, s2ws(s_author_name), start_from, s_sram_file_name,
            DateTime(date.year, date.month, date.day, date.hour, date.minute, date.second, date.millisecond));
}

EXPORTED void desmume_movie_replay()
{
    if (movieMode != MOVIEMODE_INACTIVE) {
        extern char curMovieFilename[512];
        desmume_movie_play(curMovieFilename);
    }
}
EXPORTED void desmume_movie_stop()
{
    FCEUI_StopMovie();
}
