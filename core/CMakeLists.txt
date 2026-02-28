cmake_minimum_required(VERSION 3.18)
project(qaryx C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Auto-download single-header libs ─────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# ── Find system libraries ─────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

pkg_check_modules(DRM      REQUIRED libdrm)
pkg_check_modules(GBM      REQUIRED gbm)
pkg_check_modules(EGL      REQUIRED egl)
pkg_check_modules(GLES2    REQUIRED glesv2)
pkg_check_modules(MPV      REQUIRED mpv)
pkg_check_modules(LIBINPUT REQUIRED libinput)
pkg_check_modules(UDEV     REQUIRED libudev)
pkg_check_modules(CURL     REQUIRED libcurl)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.c
    src/drm.c
    src/egl.c
    src/render.c
    src/font.c
    src/input.c
    src/mpv.c
    src/ws.c
    src/ytdlp.c
    src/iptv.c
    src/history.c
    src/config.c
    src/http_dl.c
    src/ui/home.c
    src/ui/youtube.c
    src/ui/iptv.c
    third_party/cjson.c
    third_party/sha1.c
)

add_executable(qaryx ${SOURCES})

target_include_directories(qaryx PRIVATE
    src
    third_party
    ${stb_SOURCE_DIR}          # stb_truetype.h, stb_image.h
    ${DRM_INCLUDE_DIRS}
    ${GBM_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLES2_INCLUDE_DIRS}
    ${MPV_INCLUDE_DIRS}
    ${LIBINPUT_INCLUDE_DIRS}
    ${UDEV_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(qaryx PRIVATE
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES2_LIBRARIES}
    ${MPV_LIBRARIES}
    ${LIBINPUT_LIBRARIES}
    ${UDEV_LIBRARIES}
    ${CURL_LIBRARIES}
    m
)

target_compile_options(qaryx PRIVATE
    ${DRM_CFLAGS_OTHER}
    ${GBM_CFLAGS_OTHER}
    ${LIBINPUT_CFLAGS_OTHER}
    -Wall -Wextra -Wno-unused-parameter
    -D_GNU_SOURCE
)

install(TARGETS qaryx DESTINATION /usr/bin)
