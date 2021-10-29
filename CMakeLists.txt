cmake_minimum_required(VERSION 3.19)
project(sdlquake)

find_package(SDL2 REQUIRED)
find_package(fmt REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(LIB_FOLDER lib)

if (CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _upper_build_type)
    set(BUILD_${_upper_build_type} 1)
endif ()

if (BUILD_DEBUG)
    add_compile_options("-O0" "-fno-strict-aliasing" "-fno-rtti" "-g" "-Wall" "-Wextra" "-Wpedantic" "-Wconversion" "-w" "-fsanitize=address,leak,undefined" "-fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=address,leak,undefined -fno-omit-frame-pointer -flto")
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
#        src/cd_sdl.cpp
        src/chase.cpp
        src/cl_demo.cpp
        src/cl_input.cpp
        src/cl_main.cpp
        src/cl_parse.cpp
        src/cl_tent.cpp
        src/cmd.cpp
        src/common.cpp
        src/console.cpp
        src/crc.cpp
        src/cvar.cpp
        src/d_copy.cpp
        src/d_edge.cpp
        src/d_fill.cpp
        src/d_init.cpp
        src/d_modech.cpp
        src/d_part.cpp
        src/d_polyse.cpp
        src/d_scan.cpp
        src/d_sky.cpp
        src/d_sprite.cpp
        src/d_surf.cpp
        src/d_vars.cpp
        src/d_zpoint.cpp
        src/draw.cpp
        src/host.cpp
        src/host_cmd.cpp
        src/keys.cpp
        src/mathlib.cpp
        src/menu.cpp
        src/model.cpp
        src/net_bsd.cpp
        src/net_dgrm.cpp
        src/net_loop.cpp
        src/net_main.cpp
        src/net_udp.cpp
        src/net_vcr.cpp
        src/net_wso.cpp
        src/nonintel.cpp
        src/pr_cmds.cpp
        src/pr_edict.cpp
        src/pr_exec.cpp
        src/r_aclip.cpp
        src/r_alias.cpp
        src/r_bsp.cpp
        src/r_draw.cpp
        src/r_edge.cpp
        src/r_efrag.cpp
        src/r_light.cpp
        src/r_main.cpp
        src/r_misc.cpp
        src/r_part.cpp
        src/r_sky.cpp
        src/r_sprite.cpp
        src/r_surf.cpp
        src/r_vars.cpp
        src/sbar.cpp
        src/screen.cpp
        src/snd_dma.cpp
        src/snd_mem.cpp
        src/snd_mix.cpp
        src/snd_sdl.cpp
        src/sv_main.cpp
        src/sv_move.cpp
        src/sv_phys.cpp
        src/sv_user.cpp
        src/sys_sdl.cpp
        src/util.cpp
        src/vid_sdl.cpp
        src/view.cpp
        src/wad.cpp
        src/world.cpp
        src/zone.cpp
        )

target_link_libraries(sdlquake
        ${SDL_LIBRARY}
        fmt::fmt
        SDL2::SDL2
        )
set_target_properties(sdlquake PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ~/quake
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ~/quake
        )