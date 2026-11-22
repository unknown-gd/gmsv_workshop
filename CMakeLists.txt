cmake_minimum_required(VERSION 3.16)
project(gmsv_workshop LANGUAGES C CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GM_WORKSHOP_TARGET "auto" CACHE STRING
    "GMod module target: auto, linux32, linux64, win32, win64")
set_property(CACHE GM_WORKSHOP_TARGET PROPERTY STRINGS
    auto linux32 linux64 win32 win64)

if(GM_WORKSHOP_TARGET STREQUAL "auto")
    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(GM_WORKSHOP_TARGET_RESOLVED "win64")
        else()
            set(GM_WORKSHOP_TARGET_RESOLVED "win32")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME MATCHES ".*Linux.*")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(GM_WORKSHOP_TARGET_RESOLVED "linux64")
        else()
            set(GM_WORKSHOP_TARGET_RESOLVED "linux32")
        endif()
    else()
        message(FATAL_ERROR
            "Unsupported host '${CMAKE_SYSTEM_NAME}'. "
            "Set -DGM_WORKSHOP_TARGET=linux32|linux64|win32|win64")
    endif()
else()
    set(GM_WORKSHOP_TARGET_RESOLVED "${GM_WORKSHOP_TARGET}")
endif()

if(GM_WORKSHOP_TARGET_RESOLVED STREQUAL "linux32")
    set(GM_WORKSHOP_PLATFORM_SUFFIX "linux")
    set(GM_WORKSHOP_IS_WINDOWS FALSE)
elseif(GM_WORKSHOP_TARGET_RESOLVED STREQUAL "linux64")
    set(GM_WORKSHOP_PLATFORM_SUFFIX "linux64")
    set(GM_WORKSHOP_IS_WINDOWS FALSE)
elseif(GM_WORKSHOP_TARGET_RESOLVED STREQUAL "win32")
    set(GM_WORKSHOP_PLATFORM_SUFFIX "win32")
    set(GM_WORKSHOP_IS_WINDOWS TRUE)
elseif(GM_WORKSHOP_TARGET_RESOLVED STREQUAL "win64")
    set(GM_WORKSHOP_PLATFORM_SUFFIX "win64")
    set(GM_WORKSHOP_IS_WINDOWS TRUE)
else()
    message(FATAL_ERROR
        "Invalid GM_WORKSHOP_TARGET='${GM_WORKSHOP_TARGET_RESOLVED}'. "
        "Expected auto, linux32, linux64, win32, or win64")
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT workshop_ipo_supported OUTPUT workshop_ipo_error)

set(GM_WORKSHOP_SOURCES
    src/module.cpp
    src/lua_api.cpp
    src/steam.cpp
    src/workshop.cpp
    src/gmod_lzma.cpp
    third_party/lzma/LzmaDec.c
)

set(module_name "gmsv_workshop_${GM_WORKSHOP_PLATFORM_SUFFIX}")
message(STATUS "workshop module: ${module_name}.dll")

add_library(gmsv_workshop SHARED ${GM_WORKSHOP_SOURCES})
target_include_directories(gmsv_workshop PRIVATE src third_party/lzma)
target_compile_definitions(gmsv_workshop PRIVATE
    "GM_WORKSHOP_TARGET_${GM_WORKSHOP_TARGET_RESOLVED}=1"
)

set_target_properties(gmsv_workshop PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${module_name}"
    SUFFIX ".dll"
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(GM_WORKSHOP_IS_WINDOWS)
    if(MSVC)
        set_property(TARGET gmsv_workshop PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(gmsv_workshop PRIVATE
            -static-libgcc
            -static-libstdc++
            -static
        )
    endif()
else()
    target_link_libraries(gmsv_workshop PRIVATE ${CMAKE_DL_LIBS})
endif()

if(NOT MSVC)
    target_compile_options(gmsv_workshop PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
        $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>
    )
endif()

if(workshop_ipo_supported)
    set_property(TARGET gmsv_workshop PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

install(TARGETS gmsv_workshop
    RUNTIME DESTINATION lib
    LIBRARY DESTINATION lib
)

enable_testing()
add_executable(workshop_lzma_test
    tests/lzma_roundtrip.cpp
    src/gmod_lzma.cpp
    third_party/lzma/LzmaDec.c
)
target_include_directories(workshop_lzma_test PRIVATE src third_party/lzma)
if(NOT MSVC)
    target_compile_options(workshop_lzma_test PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
        $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>
    )
endif()
add_test(NAME lzma_roundtrip COMMAND workshop_lzma_test)
