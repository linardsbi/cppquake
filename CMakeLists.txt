cmake_minimum_required(VERSION 3.19)
project(sdlquake)

find_package(SDL REQUIRED)
message("Found SDL: ${SDL_LIBRARY}\nInclude dirs: ${SDL_INCLUDE_DIRS}")
find_package(fmt REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(LIB_FOLDER lib)

if (CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _upper_build_type)
    set(BUILD_${_upper_build_type} 1)
endif ()

if (BUILD_DEBUG)
    add_compile_options("-Og" "-Wall" "-Wextra" "-Wpedantic" "-Wconversion" "-fsanitize=address,leak,undefined" "-fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=address,leak,undefined -fno-omit-frame-pointer")
    set(DEBUG_DEFINES "-DPARANOID")
else ()
    add_compile_options("-O3")
endif ()

if (WIN32)
    # TODO
    message("No Windows support as of yet")
    set(PLATFORM_DEFINES "-D_UNIX=0")
elseif (UNIX)
    set(PLATFORM_DEFINES "-D_UNIX=1")
endif ()


include_directories(.)
include_directories(${SDL_INCLUDE_DIRS} ${LIB_FOLDER})

add_definitions(-DSDL ${DEBUG_DEFINES} ${PLATFORM_DEFINES})

add_executable(sdlquake
        src/adivtab.hpp
        src/anorm_dots.hpp
        src/anorms.hpp
        src/asm_draw.hpp
        src/asm_i386.hpp
        src/block16.hpp
        src/block8.hpp
        src/bspfile.hpp
        src/cd_sdl.cpp
        src/cdaudio.hpp
        src/chase.cpp
        src/cl_demo.cpp
        src/cl_input.cpp
        src/cl_main.cpp
        src/cl_parse.cpp
        src/cl_tent.cpp
        src/client.hpp
        src/cmd.cpp
        src/cmd.hpp
        src/common.cpp
        src/common.hpp
        src/conproc.hpp
        src/console.cpp
        src/console.hpp
        src/crc.cpp
        src/crc.hpp
        src/cvar.cpp
        src/cvar.hpp
        src/d_copy.cpp
        src/d_copy.hpp
        src/d_edge.cpp
        src/d_fill.cpp
        src/d_iface.hpp
        src/d_ifacea.hpp
        src/d_init.cpp
        src/d_local.hpp
        src/d_modech.cpp
        src/d_part.cpp
        src/d_polyse.cpp
        src/d_scan.cpp
        src/d_sky.cpp
        src/d_sprite.cpp
        src/d_surf.cpp
        src/d_vars.cpp
        src/d_zpoint.cpp
        src/dosisms.hpp
        src/draw.cpp
        src/draw.hpp
        src/host.cpp
        src/host_cmd.cpp
        src/input.hpp
        src/keys.cpp
        src/keys.hpp
        src/mathlib.cpp
        src/mathlib.hpp
        src/menu.cpp
        src/menu.hpp
        src/model.cpp
        src/model.hpp
        src/modelgen.hpp
        src/mpdosock.hpp
        src/net.hpp
        src/net_bsd.cpp
        src/net_bw.hpp
        src/net_dgrm.cpp
        src/net_dgrm.hpp
        src/net_loop.cpp
        src/net_loop.hpp
        src/net_main.cpp
        src/net_udp.cpp
        src/net_udp.hpp
        src/net_vcr.cpp
        src/net_vcr.hpp
        src/net_wso.cpp
        src/nonintel.cpp
        src/pr_cmds.cpp
        src/pr_comp.hpp
        src/pr_edict.cpp
        src/pr_exec.cpp
        src/progdefs.hpp
        src/progs.hpp
        src/protocol.hpp
        src/quakeasm.hpp
        src/quakedef.hpp
        src/r_aclip.cpp
        src/r_alias.cpp
        src/r_bsp.cpp
        src/r_draw.cpp
        src/r_edge.cpp
        src/r_efrag.cpp
        src/r_light.cpp
        src/r_local.hpp
        src/r_main.cpp
        src/r_misc.cpp
        src/r_part.cpp
        src/r_shared.hpp
        src/r_sky.cpp
        src/r_sprite.cpp
        src/r_surf.cpp
        src/r_vars.cpp
        src/render.hpp
        src/resource.hpp
        src/sbar.cpp
        src/sbar.hpp
        src/screen.cpp
        src/screen.hpp
        src/server.hpp
        src/snd_dma.cpp
        src/snd_mem.cpp
        src/snd_mix.cpp
        src/snd_sdl.cpp
        src/sound.hpp
        src/spritegn.hpp
        src/sv_main.cpp
        src/sv_move.cpp
        src/sv_phys.cpp
        src/sv_user.cpp
        src/sys.hpp
        src/sys_sdl.cpp
        src/util.cpp
        src/util.hpp
        src/vgamodes.hpp
        src/vid.hpp
        src/vid_sdl.cpp
        src/view.cpp
        src/view.hpp
        src/wad.cpp
        src/wad.hpp
        #        winquake.hpp
        src/world.cpp
        src/world.hpp
        src/zone.cpp
        src/zone.hpp)

target_link_libraries(sdlquake
        ${SDL_LIBRARY}
        fmt::fmt)

set_target_properties(sdlquake PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ~/quake
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ~/quake
        )