cmake_minimum_required(VERSION 3.19)
project(sdlquake)

find_package(SDL REQUIRED)
find_package(fmt REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(LIB_FOLDER lib)

if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _upper_build_type)
    set(BUILD_${_upper_build_type} 1)
endif()

if (BUILD_DEBUG)
    add_compile_options("-Og" "-Wall" "-Wextra" "-Wpedantic" "-Wconversion" "-fsanitize=address,leak,undefined")
    set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=address,leak,undefined")
    set(DEBUG_DEFINES "-DPARANOID")
else()
    add_compile_options("-O3")
endif()

message("Found SDL: ${SDL_LIBRARY}\nInclude dirs: ${SDL_INCLUDE_DIRS}")

include_directories(.)
include_directories(${SDL_INCLUDE_DIRS} ${LIB_FOLDER})

add_definitions(-DSDL ${DEBUG_DEFINES})

add_executable(sdlquake
        adivtab.hpp
        anorm_dots.hpp
        anorms.hpp
        asm_draw.hpp
        asm_i386.hpp
        block16.hpp
        block8.hpp
        bspfile.hpp
        cd_sdl.cpp
        cdaudio.hpp
        chase.cpp
        cl_demo.cpp
        cl_input.cpp
        cl_main.cpp
        cl_parse.cpp
        cl_tent.cpp
        client.hpp
        cmd.cpp
        cmd.hpp
        common.cpp
        common.hpp
        conproc.hpp
        console.cpp
        console.hpp
        crc.cpp
        crc.hpp
        cvar.cpp
        cvar.hpp
        d_copy.cpp
        d_copy.hpp
        d_edge.cpp
        d_fill.cpp
        d_iface.hpp
        d_ifacea.hpp
        d_init.cpp
        d_local.hpp
        d_modech.cpp
        d_part.cpp
        d_polyse.cpp
        d_scan.cpp
        d_sky.cpp
        d_sprite.cpp
        d_surf.cpp
        d_vars.cpp
        d_zpoint.cpp
        dosisms.hpp
        draw.cpp
        draw.hpp
        host.cpp
        host_cmd.cpp
        input.hpp
        keys.cpp
        keys.hpp
        mathlib.cpp
        mathlib.hpp
        menu.cpp
        menu.hpp
        model.cpp
        model.hpp
        modelgen.hpp
        mpdosock.hpp
        net.hpp
        net_bsd.cpp
        net_bw.hpp
        net_dgrm.cpp
        net_dgrm.hpp
        net_loop.cpp
        net_loop.hpp
        net_main.cpp
        net_udp.cpp
        net_udp.hpp
        net_vcr.cpp
        net_vcr.hpp
        net_wso.cpp
        nonintel.cpp
        pr_cmds.cpp
        pr_comp.hpp
        pr_edict.cpp
        pr_exec.cpp
        progdefs.hpp
        progs.hpp
        protocol.hpp
        quakeasm.hpp
        quakedef.hpp
        r_aclip.cpp
        r_alias.cpp
        r_bsp.cpp
        r_draw.cpp
        r_edge.cpp
        r_efrag.cpp
        r_light.cpp
        r_local.hpp
        r_main.cpp
        r_misc.cpp
        r_part.cpp
        r_shared.hpp
        r_sky.cpp
        r_sprite.cpp
        r_surf.cpp
        r_vars.cpp
        render.hpp
        resource.hpp
        sbar.cpp
        sbar.hpp
        screen.cpp
        screen.hpp
        server.hpp
        snd_dma.cpp
        snd_mem.cpp
        snd_mix.cpp
        snd_sdl.cpp
        sound.hpp
        spritegn.hpp
        sv_main.cpp
        sv_move.cpp
        sv_phys.cpp
        sv_user.cpp
        sys.hpp
        sys_sdl.cpp
        util.cpp
        util.hpp
        vgamodes.hpp
        vid.hpp
        vid_sdl.cpp
        view.cpp
        view.hpp
        wad.cpp
        wad.hpp
#        winquake.hpp
        world.cpp
        world.hpp
        zone.cpp
        zone.hpp)

target_link_libraries(sdlquake
        ${SDL_LIBRARY}
        fmt::fmt)

set_target_properties(sdlquake PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ~/quake
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ~/quake
        )