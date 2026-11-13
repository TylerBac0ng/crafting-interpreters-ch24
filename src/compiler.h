#ifndef clox_compiler_h
#define clox_compiler_h

#include "object.h"
#include "vm.h"

// Compiles a script into a function with no name. Returns NULL if there was
// a compile error.
ObjFunction* compile(const char* source);

#endif
