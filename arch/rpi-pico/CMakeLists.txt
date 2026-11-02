cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")
# Pico SDK の場所: 環境変数 PICO_SDK_PATH があればそれ、無ければ既定の置き場
if(DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
else()
    set(PICO_SDK_PATH "/home/tk/tools/picotools/pico-sdk")
endif()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(tizix_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# TICK_HZ: タイマー割り込み周波数 (Hz)
set(TICK_HZ 100 CACHE STRING "Timer tick frequency in Hz")

pico_sdk_init()

add_compile_options(-include plat.h)
add_compile_options(-DTICK_HZ=${TICK_HZ})
# 共通 src/ の Pico 専用部分(TinyUSB の tud_task 等)を有効にする
add_compile_options(-DARCH_RPI_PICO)
if(DEFINED BOOT_COUNTDOWN_SEC)
  add_compile_options(-DBOOT_COUNTDOWN_SEC=${BOOT_COUNTDOWN_SEC})
endif()

add_executable(kernel
    kmain.c
    xmodem_flash.c
    diskio.c
    gpio_hw.c
    flashfs.S
    pcmd.c
    ../../src/gpiodev.c
    console.c
    loader.c
    ../../src/kernel.c
    ../../src/vfs.c
    ../../src/ff.c
    ../../src/fatcmd.c
    ../../src/builtin.c
    ../../src/io.c
    ../../src/kexec.c
    ../../src/ffunicode_min.c
    ../../src/fsbackend_fat.c
    ../../src/sysfile.c
    ../../src/pipe.c
    ../../src/dev.c
    ../../src/sh.c
)

# 内蔵 Flash の FAT イメージ(flashfs.S が .incbin する)。1MB。
set(FLASHFS_KB 1024)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/flashfs.img
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkflashfs.sh ${CMAKE_CURRENT_BINARY_DIR}/flashfs.img ${FLASHFS_KB} ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/etc/rc ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkflashfs.sh
    COMMENT "Building flashfs.img (internal Flash FAT)")
set_source_files_properties(flashfs.S PROPERTIES
    OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/flashfs.img
    COMPILE_FLAGS "-I${CMAKE_CURRENT_BINARY_DIR}")

target_include_directories(kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(kernel 
    pico_stdlib 
    pico_multicore
    pico_cyw43_arch_lwip_poll
    hardware_spi
    hardware_gpio
    hardware_flash
    pico_flash
)

pico_enable_stdio_usb(kernel 1)
pico_enable_stdio_uart(kernel 0)

pico_add_extra_outputs(kernel)



















