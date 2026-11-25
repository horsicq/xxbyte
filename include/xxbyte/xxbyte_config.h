#ifndef XXBYTE_XXBYTE_CONFIG_H
#define XXBYTE_XXBYTE_CONFIG_H

/*
 * Bytecode family selection.
 *
 * Each XXBYTE_USE_<FAMILY> macro is 1 when that decoder is compiled into the
 * library and 0 when it is left out. The CMake build passes the selected
 * values as public compile definitions and installs a configured copy of this
 * header, so consumers always see the families the library was built with.
 *
 * A build without CMake compiles every family by default. Define a macro to 0
 * on the compiler command line to leave that family out, for example
 *
 *     cc -DXXBYTE_USE_JAVA=0 -DXXBYTE_USE_PYTHON=0 -DXXBYTE_USE_LUA=0 ...
 *
 * leaves those decoders out. The same values must be used for the library
 * and every translation unit that includes the xxbyte headers.
 */

#ifndef XXBYTE_USE_DOTNET
#define XXBYTE_USE_DOTNET 1
#endif

#ifndef XXBYTE_USE_DEX
#define XXBYTE_USE_DEX 1
#endif

#ifndef XXBYTE_USE_JAVA
#define XXBYTE_USE_JAVA 1
#endif

#ifndef XXBYTE_USE_PYTHON
#define XXBYTE_USE_PYTHON 1
#endif

#ifndef XXBYTE_USE_VB
#define XXBYTE_USE_VB 1
#endif

#ifndef XXBYTE_USE_WASM
#define XXBYTE_USE_WASM 1
#endif

#ifndef XXBYTE_USE_EBPF
#define XXBYTE_USE_EBPF 1
#endif

#ifndef XXBYTE_USE_LUA
#define XXBYTE_USE_LUA 1
#endif

#ifndef XXBYTE_USE_LUAJIT
#define XXBYTE_USE_LUAJIT 1
#endif

#ifndef XXBYTE_USE_NSIS
#define XXBYTE_USE_NSIS 1
#endif

#ifndef XXBYTE_USE_IFPS
#define XXBYTE_USE_IFPS 1
#endif

#ifndef XXBYTE_USE_EVM
#define XXBYTE_USE_EVM 1
#endif

#ifndef XXBYTE_USE_EBC
#define XXBYTE_USE_EBC 1
#endif

#ifndef XXBYTE_USE_YARV
#define XXBYTE_USE_YARV 1
#endif

#ifndef XXBYTE_USE_AVM2
#define XXBYTE_USE_AVM2 1
#endif

#ifndef XXBYTE_USE_AVM1
#define XXBYTE_USE_AVM1 1
#endif

#ifndef XXBYTE_USE_AUTOIT
#define XXBYTE_USE_AUTOIT 1
#endif

#ifndef XXBYTE_USE_INSTALLSHIELD
#define XXBYTE_USE_INSTALLSHIELD 1
#endif

#ifndef XXBYTE_USE_HERMES
#define XXBYTE_USE_HERMES 1
#endif

#ifndef XXBYTE_USE_BEAM
#define XXBYTE_USE_BEAM 1
#endif

#ifndef XXBYTE_USE_OCAML
#define XXBYTE_USE_OCAML 1
#endif

#ifndef XXBYTE_USE_GDSCRIPT
#define XXBYTE_USE_GDSCRIPT 1
#endif

#ifndef XXBYTE_USE_SQUIRREL
#define XXBYTE_USE_SQUIRREL 1
#endif

#ifndef XXBYTE_USE_PHP
#define XXBYTE_USE_PHP 1
#endif

#ifndef XXBYTE_USE_QUICKJS
#define XXBYTE_USE_QUICKJS 1
#endif

#ifndef XXBYTE_USE_V8
#define XXBYTE_USE_V8 1
#endif

#if !XXBYTE_USE_DOTNET && !XXBYTE_USE_DEX && !XXBYTE_USE_JAVA && !XXBYTE_USE_PYTHON && !XXBYTE_USE_VB && !XXBYTE_USE_WASM && !XXBYTE_USE_EBPF && !XXBYTE_USE_LUA && \
    !XXBYTE_USE_LUAJIT && !XXBYTE_USE_NSIS && !XXBYTE_USE_IFPS && !XXBYTE_USE_EVM && !XXBYTE_USE_EBC && !XXBYTE_USE_YARV && !XXBYTE_USE_AVM2 && \
    !XXBYTE_USE_AVM1 && !XXBYTE_USE_AUTOIT && !XXBYTE_USE_INSTALLSHIELD && !XXBYTE_USE_HERMES && !XXBYTE_USE_BEAM && !XXBYTE_USE_OCAML && !XXBYTE_USE_GDSCRIPT && !XXBYTE_USE_SQUIRREL && !XXBYTE_USE_PHP && !XXBYTE_USE_QUICKJS && !XXBYTE_USE_V8
#error "xxbyte needs at least one enabled bytecode family"
#endif

#endif
