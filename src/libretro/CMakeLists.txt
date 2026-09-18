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
    lr_plat_unix.c
    ../unix/unix_dynld.c
    ../unix/unix_netsocket.c
    lr_joystick.c
)

if(NOT CPPTHREADS)
    target_sources(plat PRIVATE lr_thread.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(plat PRIVATE ../unix/linux_cdrom_ioctl.c ../unix/dummy_floppy_ioctl.c)
else()
    target_sources(plat PRIVATE ../unix/dummy_cdrom_ioctl.c ../unix/dummy_floppy_ioctl.c)
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

find_package(Threads REQUIRED)
target_link_libraries(86Box Threads::Threads)
