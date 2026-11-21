cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(SC4LoadCache VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "SimCity 4 DLLs are 32-bit Windows DLLs: configure with -A Win32.")
endif()

# Copy the DLLs into this folder after each build (empty: no copy).
set(SC4_PLUGINS_DIR "" CACHE PATH "SimCity 4 Plugins folder to copy the built DLLs to")

set(GZCOM_DIR "${CMAKE_SOURCE_DIR}/vendor/gzcom-dll/gzcom-dll")
if(NOT EXISTS "${GZCOM_DIR}/include")
    message(FATAL_ERROR "vendor/gzcom-dll is missing: run git submodule update --init")
endif()
file(GLOB GZCOM_SOURCES CONFIGURE_DEPENDS "${GZCOM_DIR}/src/*.cpp")
list(FILTER GZCOM_SOURCES EXCLUDE REGEX "/(main|cGZExampleDllDirector|cGZExtraCheatsDllDirector|cGZExtraExtraCheatsDllDirector|cGZCityHallUpgradeDllDirector)\\.cpp$")
add_library(gzcom2 STATIC ${GZCOM_SOURCES})
target_include_directories(gzcom2 PUBLIC "${GZCOM_DIR}/include")

add_subdirectory(vendor/minhook EXCLUDE_FROM_ALL)

function(sc4_plugin target)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
    if(SC4_PLUGINS_DIR)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${SC4_PLUGINS_DIR}")
    endif()
endfunction()

# The cache: shipped.
add_library(SC4LoadCache SHARED src/loadcache/LoadCache.cpp src/loadcache/SC4LoadCache.def)
target_link_libraries(SC4LoadCache PRIVATE gzcom2 minhook version)
sc4_plugin(SC4LoadCache)

# The raw record reader of the cache on its own: ctest, and tests/check_exemplar_types.py.
add_executable(exemplar_type_test tests/exemplar_type_test.cpp)
target_include_directories(exemplar_type_test PRIVATE src/loadcache)
target_compile_options(exemplar_type_test PRIVATE /W4 /utf-8)
enable_testing()
add_test(NAME exemplar_type COMMAND exemplar_type_test)

# The profiler: a test DLL that times city loads, not shipped.
add_library(SC4LoadProfiler SHARED src/profiler/LoadProfiler.cpp src/profiler/SC4LoadProfiler.def)
target_link_libraries(SC4LoadProfiler PRIVATE gzcom2 minhook winmm)
sc4_plugin(SC4LoadProfiler)
