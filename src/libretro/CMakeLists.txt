#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script for the libretro core.
#

add_library(plat OBJECT
    lr_plat.c
    lr_joystick.c
)

# sdl_plat_win.c uses no SDL, so the core shares it with the SDL frontend;
# only the unix half needed the SDL calls taken out.
if(WIN32)
    target_sources(plat PRIVATE
        ../unix/sdl_plat_win.c
        ../qt/win_dynld.c
        ../qt/win_netsocket.c
        ../qt/win_cdrom_ioctl.c
        ../unix/dummy_floppy_ioctl.c
    )
    target_link_libraries(86Box ws2_32)
else()
    target_sources(plat PRIVATE
        lr_plat_unix.c
        ../unix/unix_dynld.c
        ../unix/unix_netsocket.c
        ../unix/dummy_floppy_ioctl.c
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(plat PRIVATE ../unix/linux_cdrom_ioctl.c)
    else()
        target_sources(plat PRIVATE ../unix/dummy_cdrom_ioctl.c)
    endif()
endif()

if(APPLE)
    target_sources(plat PRIVATE ../unix/macOSXGlue.m)
    target_include_directories(plat PRIVATE ../unix)
    target_link_libraries(86Box "-framework Foundation")
endif()

if(NOT CPPTHREADS)
    if(WIN32)
        target_sources(plat PRIVATE ../qt/win_thread.c)
    else()
        target_sources(plat PRIVATE lr_thread.c)
    endif()
endif()

target_compile_definitions(plat PUBLIC _FILE_OFFSET_BITS=64)
target_link_libraries(plat ${CMAKE_DL_LIBS})

add_library(ui OBJECT
    lr_core.c
    lr_video.c
    lr_input.c
    lr_ui.c
    ../unix/sdl_cdrom.c
)

if(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
endif()
find_package(Threads REQUIRED)
target_link_libraries(86Box Threads::Threads)
