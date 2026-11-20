cmake_minimum_required(VERSION 3.10)
project(psp-homelab C ASM)

include(${PSPDEV}/psp/share/AddPrxModule.cmake OPTIONAL RESULT_VARIABLE HAVE_PRX_HELPER)
if(NOT HAVE_PRX_HELPER)
    include($ENV{PSPDEV}/psp/share/AddPrxModule.cmake)
endif()

# ---------------------------------------------------------------------------
# Pliki osadzane w programie
# ---------------------------------------------------------------------------

function(embed_file name input)
    set(out ${CMAKE_BINARY_DIR}/embed_${name}.c)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -DIN=${input} -DOUT=${out} -DNAME=${name}
                -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS ${input} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
        COMMENT "Osadzanie ${name}"
    )
    set(EMBED_${name} ${out} PARENT_SCOPE)
endfunction()

# Lista zaufanych urzędów certyfikacji (z systemu)
set(CA_BUNDLE "/etc/ssl/certs/ca-certificates.crt" CACHE FILEPATH "Plik PEM z zaufanymi CA")
embed_file(ca_bundle ${CA_BUNDLE})
# Czcionki Noto Sans (licencja OFL, assets/fonts/OFL.txt)
embed_file(font_regular ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf)
embed_file(font_bold ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Bold.ttf)

# ---------------------------------------------------------------------------
# Moduł jądra z generatorem KIRK
# ---------------------------------------------------------------------------

add_prx_module(kirkrng
    ${CMAKE_SOURCE_DIR}/kirkrng/kirkrng.c
    ${CMAKE_SOURCE_DIR}/kirkrng/kirkrng.exp
)
target_compile_options(kirkrng PRIVATE -Wall -O2 -G0)
# Moduł jądra nie korzysta z libc ani bibliotek trybu użytkownika.
target_link_options(kirkrng PRIVATE -nostdlib)
target_link_libraries(kirkrng PRIVATE pspmodinfo pspsemaphore_660 pspsdk pspkernel gcc)

# ---------------------------------------------------------------------------
# Moduł jądra sterujący podświetleniem (tryb nocny)
# ---------------------------------------------------------------------------

add_prx_module(backlight
    ${CMAKE_SOURCE_DIR}/backlight/backlight.c
    ${CMAKE_SOURCE_DIR}/backlight/backlight.exp
)
target_compile_options(backlight PRIVATE -Wall -O2 -G0)
target_link_options(backlight PRIVATE -nostdlib)
target_link_libraries(backlight PRIVATE pspmodinfo pspdisplay_driver pspsdk pspkernel gcc)

# ---------------------------------------------------------------------------
# Program główny
# ---------------------------------------------------------------------------

find_package(SDL2 REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.c
    src/ui.c
    src/worker.c
    src/config.c
    src/net.c
    src/https.c
    src/ha.c
    src/rng.c
    src/kmod.c
    src/night.c
    src/timeutil.c
    src/chart.c
    kirkrng/KirkRng.S
    backlight/Backlight.S
    ${EMBED_ca_bundle}
    ${EMBED_font_regular}
    ${EMBED_font_bold}
)

target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIRS} ${PSPDEV}/psp/sdk/include)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wno-unused-parameter -Wno-format-truncation>)

# Uwaga: psputility, psprtc, pspnet_inet i pspnet_resolver dodaje sam psp-gcc
# (po libc). Nie wpisuj ich tutaj: podwójne linkowanie rozdziela tablicę importów
# modułu sceNetInet i socket() przestaje działać.
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARIES}
    SDL2_ttf
    harfbuzz
    freetype
    bz2
    png16
    z
    m
    stdc++
    cjson
    mbedtls
    mbedx509
    mbedcrypto
    pspdebug
    pspnet_apctl
    pspnet
)
# pspdisplay, pspge, pspctrl, pspgu, pspaudio itd. dopisuje na końcu samo SDL2.
# Gdyby pojawiły się też tutaj, importy np. sceDisplay rozpadną się na dwa
# kawałki ("stubs out of order" z psp-fixup-imports) i na PSP funkcje
# z drugiego kawałka nie zostaną podpięte.

# Tworzy EBOOT.PBP — plik, który uruchamia PSP i PPSSPP
create_pbp_file(
    TARGET ${PROJECT_NAME}
    TITLE "psp-homelab"
    BUILD_PRX
)

# Moduły jądra muszą leżeć obok EBOOT.PBP
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kirkrng>.prx ${CMAKE_BINARY_DIR}/kirkrng.prx
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:backlight>.prx ${CMAKE_BINARY_DIR}/backlight.prx
    COMMENT "Kopiowanie kirkrng.prx i backlight.prx obok EBOOT.PBP"
)
add_dependencies(${PROJECT_NAME} kirkrng backlight)
