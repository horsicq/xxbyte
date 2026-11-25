#ifndef XXBYTE_XXBYTE_H
#define XXBYTE_XXBYTE_H

#include "xxbyte_common.h"

#if XXBYTE_USE_DOTNET
#include "xxbyte_dotnet.h"
#endif

#if XXBYTE_USE_DEX
#include "xxbyte_dex.h"
#endif

#if XXBYTE_USE_JAVA
#include "xxbyte_java.h"
#endif

#if XXBYTE_USE_PYTHON
#include "xxbyte_python.h"
#endif

#if XXBYTE_USE_VB
#include "xxbyte_vb.h"
#endif

#if XXBYTE_USE_WASM
#include "xxbyte_wasm.h"
#endif

#if XXBYTE_USE_EBPF
#include "xxbyte_ebpf.h"
#endif

#if XXBYTE_USE_LUA
#include "xxbyte_lua.h"
#endif

#if XXBYTE_USE_LUAJIT
#include "xxbyte_luajit.h"
#endif

#if XXBYTE_USE_NSIS
#include "xxbyte_nsis.h"
#endif

#if XXBYTE_USE_IFPS
#include "xxbyte_ifps.h"
#endif

#if XXBYTE_USE_EVM
#include "xxbyte_evm.h"
#endif

#if XXBYTE_USE_EBC
#include "xxbyte_ebc.h"
#endif

#if XXBYTE_USE_YARV
#include "xxbyte_yarv.h"
#endif

#if XXBYTE_USE_AVM2
#include "xxbyte_avm2.h"
#endif

#if XXBYTE_USE_AVM1
#include "xxbyte_avm1.h"
#endif

#if XXBYTE_USE_AUTOIT
#include "xxbyte_autoit.h"
#endif

#if XXBYTE_USE_INSTALLSHIELD
#include "xxbyte_installshield.h"
#endif

#if XXBYTE_USE_HERMES
#include "xxbyte_hermes.h"
#endif

#if XXBYTE_USE_BEAM
#include "xxbyte_beam.h"
#endif

#if XXBYTE_USE_OCAML
#include "xxbyte_ocaml.h"
#endif

#if XXBYTE_USE_GDSCRIPT
#include "xxbyte_gdscript.h"
#endif

#if XXBYTE_USE_SQUIRREL
#include "xxbyte_squirrel.h"
#endif

#if XXBYTE_USE_PHP
#include "xxbyte_php.h"
#endif

#if XXBYTE_USE_QUICKJS
#include "xxbyte_quickjs.h"
#endif

#if XXBYTE_USE_V8
#include "xxbyte_v8.h"
#endif

#endif
