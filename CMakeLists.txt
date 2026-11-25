cmake_minimum_required(VERSION 3.20)

# A parent's normal variables (XXBYTE_FAMILIES, XXBYTE_XXFCLIB_SOURCE_DIR)
# must win over the cache defaults on the first configure as well.
if(POLICY CMP0126)
    cmake_policy(SET CMP0126 NEW)
endif()

project(xxbyte
    VERSION 0.1.0
    DESCRIPTION "Pure C disassembler for virtual-machine bytecode"
    LANGUAGES C
)

include(GNUInstallDirs)

set(_xxbyte_top_level OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(_xxbyte_top_level ON)
endif()

# Tests and command-line tools are development support trees. Release
# exports may contain only the library headers and sources.
set(_xxbyte_has_tests OFF)
if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    set(_xxbyte_has_tests ON)
    if(_xxbyte_top_level)
        include(CTest)
    endif()
endif()

set(_xxbyte_has_tools OFF)
if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tools")
    set(_xxbyte_has_tools ON)
endif()

option(BUILD_SHARED_LIBS "Build xxbyte as a shared library" OFF)
option(XXBYTE_INSTALL "Generate the xxbyte install rules" ${_xxbyte_top_level})
if(_xxbyte_has_tools)
    option(XXBYTE_BUILD_CLI "Build the xxbyte command-line disassembler" ${_xxbyte_top_level})
else()
    set(XXBYTE_BUILD_CLI OFF)
endif()
if(_xxbyte_has_tests)
    option(XXBYTE_BUILD_TESTS "Build xxbyte tests" ${_xxbyte_top_level})
else()
    set(XXBYTE_BUILD_TESTS OFF)
endif()

# Bytecode families. Each XXBYTE_USE_<FAMILY> option compiles one decoder.
# XXBYTE_FAMILIES, when not empty, overrides all of them with a list, e.g.
#   -DXXBYTE_FAMILIES="dotnet;dex"
# builds only the .NET and Dalvik decoders.
set(_xxbyte_all_families
    DOTNET DEX JAVA PYTHON VB WASM EBPF LUA LUAJIT NSIS IFPS EVM EBC YARV AVM2
    AVM1 AUTOIT INSTALLSHIELD HERMES BEAM OCAML GDSCRIPT SQUIRREL PHP QUICKJS V8)
option(XXBYTE_USE_DOTNET "Build the .NET CIL (MSIL) decoder" ON)
option(XXBYTE_USE_DEX "Build the Dalvik (DEX) decoder" ON)
option(XXBYTE_USE_JAVA "Build the JVM (Java class) decoder" ON)
option(XXBYTE_USE_PYTHON "Build the CPython bytecode decoder" ON)
option(XXBYTE_USE_VB "Build the Visual Basic 5/6 P-code decoder" ON)
option(XXBYTE_USE_WASM "Build the WebAssembly decoder" ON)
option(XXBYTE_USE_EBPF "Build the eBPF decoder" ON)
option(XXBYTE_USE_LUA "Build the Lua 5.x bytecode decoder" ON)
option(XXBYTE_USE_LUAJIT "Build the LuaJIT bytecode decoder" ON)
option(XXBYTE_USE_NSIS "Build the NSIS script decoder" ON)
option(XXBYTE_USE_IFPS "Build the PascalScript (Inno Setup) decoder" ON)
option(XXBYTE_USE_EVM "Build the Ethereum EVM decoder" ON)
option(XXBYTE_USE_EBC "Build the EFI Byte Code decoder" ON)
option(XXBYTE_USE_YARV "Build the Ruby YARV decoder" ON)
option(XXBYTE_USE_AVM2 "Build the ActionScript 3 (AVM2) decoder" ON)
option(XXBYTE_USE_AVM1 "Build the ActionScript 1/2 (AVM1) decoder" ON)
option(XXBYTE_USE_AUTOIT "Build the AutoIt v3 compiled script (A3X) decoder" ON)
option(XXBYTE_USE_INSTALLSHIELD "Build the InstallShield compiled script (.inx) decoder" ON)
option(XXBYTE_USE_HERMES "Build the Hermes (React Native) bytecode decoder" ON)
option(XXBYTE_USE_BEAM "Build the Erlang/OTP BEAM decoder" ON)
option(XXBYTE_USE_OCAML "Build the OCaml bytecode decoder" ON)
option(XXBYTE_USE_GDSCRIPT "Build the Godot GDScript bytecode decoder" ON)
option(XXBYTE_USE_SQUIRREL "Build the Squirrel bytecode decoder" ON)
option(XXBYTE_USE_PHP "Build the PHP Zend opcodes (OPcache) decoder" ON)
option(XXBYTE_USE_QUICKJS "Build the QuickJS bytecode decoder" ON)
option(XXBYTE_USE_V8 "Build the V8 Ignition bytecode decoder" ON)
set(XXBYTE_FAMILIES "" CACHE STRING
    "Families to build (e.g. dotnet;dex;wasm, or all); empty uses the XXBYTE_USE_<FAMILY> options")

if(XXBYTE_FAMILIES)
    foreach(_family IN LISTS _xxbyte_all_families)
        set(_xxbyte_enable_${_family} OFF)
    endforeach()
    foreach(_item IN LISTS XXBYTE_FAMILIES)
        string(STRIP "${_item}" _item)
        string(TOUPPER "${_item}" _item)
        if(_item STREQUAL "ALL")
            foreach(_family IN LISTS _xxbyte_all_families)
                set(_xxbyte_enable_${_family} ON)
            endforeach()
        elseif(_item STREQUAL "CIL" OR _item STREQUAL "MSIL" OR _item STREQUAL "NET")
            set(_xxbyte_enable_DOTNET ON)
        elseif(_item STREQUAL "DALVIK")
            set(_xxbyte_enable_DEX ON)
        elseif(_item STREQUAL "JVM")
            set(_xxbyte_enable_JAVA ON)
        elseif(_item STREQUAL "PYC")
            set(_xxbyte_enable_PYTHON ON)
        elseif(_item STREQUAL "PCODE" OR _item STREQUAL "P-CODE")
            set(_xxbyte_enable_VB ON)
        elseif(_item STREQUAL "WEBASSEMBLY")
            set(_xxbyte_enable_WASM ON)
        elseif(_item STREQUAL "BPF")
            set(_xxbyte_enable_EBPF ON)
        elseif(_item STREQUAL "INNOSETUP" OR _item STREQUAL "PASCALSCRIPT")
            set(_xxbyte_enable_IFPS ON)
        elseif(_item STREQUAL "ETHEREUM")
            set(_xxbyte_enable_EVM ON)
        elseif(_item STREQUAL "EFI")
            set(_xxbyte_enable_EBC ON)
        elseif(_item STREQUAL "RUBY")
            set(_xxbyte_enable_YARV ON)
        elseif(_item STREQUAL "ABC" OR _item STREQUAL "ACTIONSCRIPT")
            set(_xxbyte_enable_AVM2 ON)
        elseif(_item STREQUAL "SWF" OR _item STREQUAL "AS2")
            set(_xxbyte_enable_AVM1 ON)
        elseif(_item STREQUAL "A3X" OR _item STREQUAL "AU3")
            set(_xxbyte_enable_AUTOIT ON)
        elseif(_item STREQUAL "INX" OR _item STREQUAL "ISSCRIPT")
            set(_xxbyte_enable_INSTALLSHIELD ON)
        elseif(_item STREQUAL "HBC")
            set(_xxbyte_enable_HERMES ON)
        elseif(_item STREQUAL "ERLANG")
            set(_xxbyte_enable_BEAM ON)
        elseif(_item STREQUAL "OCAMLRUN")
            set(_xxbyte_enable_OCAML ON)
        elseif(_item STREQUAL "GODOT")
            set(_xxbyte_enable_GDSCRIPT ON)
        elseif(_item STREQUAL "CNUT")
            set(_xxbyte_enable_SQUIRREL ON)
        elseif(_item STREQUAL "OPCACHE" OR _item STREQUAL "ZEND")
            set(_xxbyte_enable_PHP ON)
        elseif(_item STREQUAL "QJS")
            set(_xxbyte_enable_QUICKJS ON)
        elseif(_item STREQUAL "IGNITION")
            set(_xxbyte_enable_V8 ON)
        elseif(_item IN_LIST _xxbyte_all_families)
            set(_xxbyte_enable_${_item} ON)
        else()
            message(FATAL_ERROR "XXBYTE_FAMILIES: unknown family '${_item}'")
        endif()
    endforeach()
else()
    foreach(_family IN LISTS _xxbyte_all_families)
        if(XXBYTE_USE_${_family})
            set(_xxbyte_enable_${_family} ON)
        else()
            set(_xxbyte_enable_${_family} OFF)
        endif()
    endforeach()
endif()

set(_xxbyte_enabled_families "")
foreach(_family IN LISTS _xxbyte_all_families)
    if(_xxbyte_enable_${_family})
        list(APPEND _xxbyte_enabled_families ${_family})
        set(XXBYTE_CONFIG_USE_${_family} 1)
    else()
        set(XXBYTE_CONFIG_USE_${_family} 0)
    endif()
endforeach()
if(NOT _xxbyte_enabled_families)
    message(FATAL_ERROR "xxbyte needs at least one enabled bytecode family")
endif()
string(REPLACE ";" ", " _xxbyte_family_text "${_xxbyte_enabled_families}")
message(STATUS "xxbyte families: ${_xxbyte_family_text}")

set(XXBYTE_XXFCLIB_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../xxfclib"
    CACHE PATH "Path to the xxfclib source tree")

set(_xxbyte_added_xxfclib OFF)
if(NOT TARGET xxfclib::xxfclib)
    set(_xxbyte_added_xxfclib ON)
    if(NOT EXISTS "${XXBYTE_XXFCLIB_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "xxfclib was not found at ${XXBYTE_XXFCLIB_SOURCE_DIR}")
    endif()
    set(XXFC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(XXFC_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(_xxbyte_build_testing "${BUILD_TESTING}")
    set(BUILD_TESTING OFF)
    add_subdirectory(
        "${XXBYTE_XXFCLIB_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/_deps/xxfclib"
        EXCLUDE_FROM_ALL)
    set(BUILD_TESTING "${_xxbyte_build_testing}")
    unset(_xxbyte_build_testing)
endif()

# The installed config header records the selected families; the build tree
# passes the same values as compile definitions.
set(_xxbyte_generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
    cmake/xxbyte_config.h.in
    "${_xxbyte_generated_include_dir}/xxbyte/xxbyte_config.h"
    @ONLY)

set(XXBYTE_PUBLIC_HEADERS
    include/xxbyte/xxbyte.h
    include/xxbyte/xxbyte_common.h
    include/xxbyte/xxbyte_config.h
    include/xxbyte/xxbyte_xxfc.h
)
set(XXBYTE_SOURCES
    src/xxbyte_internal.h
    src/xxbyte.c
    src/xxbyte_float.c
    src/xxbyte_xxfc.c
    src/xxbyte_inflate.c
)
foreach(_family IN LISTS _xxbyte_all_families)
    string(TOLOWER "${_family}" _xxbyte_family_${_family})
endforeach()
foreach(_family IN LISTS _xxbyte_enabled_families)
    set(_name ${_xxbyte_family_${_family}})
    list(APPEND XXBYTE_PUBLIC_HEADERS include/xxbyte/xxbyte_${_name}.h)
    file(GLOB _xxbyte_family_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/xxbyte_${_name}.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/xxbyte_${_name}_*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/xxbyte_${_name}_*.h")
    list(APPEND XXBYTE_SOURCES ${_xxbyte_family_sources})
endforeach()

add_library(xxbyte ${XXBYTE_PUBLIC_HEADERS} ${XXBYTE_SOURCES})
add_library(xxbyte::xxbyte ALIAS xxbyte)

target_compile_features(xxbyte PRIVATE c_std_11)
set_target_properties(xxbyte PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_compile_definitions(xxbyte PRIVATE XXBYTE_BUILDING_LIBRARY)
foreach(_family IN LISTS _xxbyte_all_families)
    target_compile_definitions(xxbyte PUBLIC
        XXBYTE_USE_${_family}=${XXBYTE_CONFIG_USE_${_family}})
endforeach()
target_include_directories(xxbyte
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# xxbyte_xxfc.h exposes xxfclib buffers and devices.
target_link_libraries(xxbyte PUBLIC xxfclib::xxfclib)

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(xxbyte PUBLIC XXBYTE_STATIC)
endif()

if(MSVC)
    target_compile_options(xxbyte PRIVATE /W4 /permissive-)
else()
    target_compile_options(xxbyte PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(XXBYTE_BUILD_CLI)
    add_executable(xxbyte_cli tools/xxbyte_cli.c)
    target_compile_features(xxbyte_cli PRIVATE c_std_11)
    target_link_libraries(xxbyte_cli PRIVATE xxbyte::xxbyte)
    if(XXBYTE_INSTALL)
        install(TARGETS xxbyte_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

# Windows has no RUNPATH: keep a shared xxfclib that xxbyte added itself next
# to the test and tool executables.
if(WIN32 AND _xxbyte_added_xxfclib)
    get_target_property(_xxbyte_xxfclib_target xxfclib::xxfclib ALIASED_TARGET)
    if(_xxbyte_xxfclib_target)
        get_target_property(_xxbyte_xxfclib_type ${_xxbyte_xxfclib_target} TYPE)
        if(_xxbyte_xxfclib_type STREQUAL "SHARED_LIBRARY")
            set_target_properties(${_xxbyte_xxfclib_target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    endif()
endif()

if(XXBYTE_BUILD_TESTS AND _xxbyte_has_tests
   AND (BUILD_TESTING OR NOT _xxbyte_top_level))
    if(NOT _xxbyte_top_level)
        enable_testing()
    endif()
    add_executable(xxbyte_test_api tests/test_api.c)
    target_compile_features(xxbyte_test_api PRIVATE c_std_11)
    target_link_libraries(xxbyte_test_api PRIVATE xxbyte::xxbyte)
    add_test(NAME xxbyte_api COMMAND xxbyte_test_api)
    # The inflater is internal (hidden in shared builds): test its sources.
    add_executable(xxbyte_test_inflate tests/test_inflate.c src/xxbyte_inflate.c)
    target_compile_features(xxbyte_test_inflate PRIVATE c_std_11)
    target_include_directories(xxbyte_test_inflate PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(xxbyte_test_inflate PRIVATE xxbyte::xxbyte)
    add_test(NAME xxbyte_inflate COMMAND xxbyte_test_inflate)
    foreach(_family IN LISTS _xxbyte_enabled_families)
        set(_name ${_xxbyte_family_${_family}})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_${_name}.c")
            add_executable(xxbyte_test_${_name} tests/test_${_name}.c)
            target_compile_features(xxbyte_test_${_name} PRIVATE c_std_11)
            target_link_libraries(xxbyte_test_${_name} PRIVATE xxbyte::xxbyte)
            add_test(NAME xxbyte_${_name} COMMAND xxbyte_test_${_name})
        endif()
    endforeach()
endif()

# Local-only Radare2 bytecode corpus. Published source trees omit
# private_tests, and embedding xxbyte must not pull it in.
if(_xxbyte_top_level AND BUILD_TESTING AND
   IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/private_tests")
    if(_xxbyte_enable_DEX AND _xxbyte_enable_JAVA AND _xxbyte_enable_WASM AND
       _xxbyte_enable_EBPF AND _xxbyte_enable_EVM AND _xxbyte_enable_EBC)
        add_subdirectory(private_tests)
    else()
        message(STATUS "Private radare2 bytecode test needs DEX, JAVA, WASM, EBPF, EVM and EBC")
    endif()
endif()

# Installation covers xxbyte only; install xxfclib (and its cdisasm) from
# its own tree. A static consumer links libxxfclib and libcdisasm as well.
if(XXBYTE_INSTALL)
    install(TARGETS xxbyte
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    # Only the enabled families' headers, and the configured xxbyte_config.h.
    set(_xxbyte_install_headers ${XXBYTE_PUBLIC_HEADERS})
    list(REMOVE_ITEM _xxbyte_install_headers include/xxbyte/xxbyte_config.h)
    install(FILES ${_xxbyte_install_headers}
        "${_xxbyte_generated_include_dir}/xxbyte/xxbyte_config.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xxbyte)
    install(FILES LICENSE README.md
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/xxbyte)
    install(FILES docs/BUILD.md
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/xxbyte/docs)
endif()
