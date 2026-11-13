#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64

typedef struct {
  ObjFunction* function;
  // 24.1: while run() is executing a frame, the up to date ip is in a local
  // variable there, and it is only written back here before a call or a
  // runtime error. Built with FRAME_IP, run() uses this field directly, as
  // the book does.
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  // 22.4: the book's stack holds a fixed number of values, which isn't
  // enough for more than 256 locals. Like my 15.3, it is a dynamic array
  // that grows. When it moves, push() moves each frame's slots pointer too.
  Value* stack;
  Value* stackTop;
  int stackCapacity;
  // 22.3: the names of global constants, so later lines of the REPL can't
  // assign to them either.
  Table constGlobals;
#ifdef GLOBAL_TABLE
  Table globals;
#else
  // 21.2: the compiler gives each global name a slot. globalNames maps the
  // name to its slot number, and globalValues holds the values, with
  // EMPTY_VAL in a slot that isn't defined yet. globalIdentifiers has the
  // name for each slot, for error messages. globalConsts is true for a slot
  // declared with const, so OP_SET_GLOBAL can check it at runtime.
  Table globalNames;
  ValueArray globalValues;
  ValueArray globalIdentifiers;
  ValueArray globalConsts;
#endif
  Table strings;
  Obj* objects;
#ifdef CONSTANT_STRINGS
  struct KeptSource* sources;
#endif
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
// Runs a compiled script. interpret() is compile() and then this.
InterpretResult runFunction(ObjFunction* function);
// 19.2: takes ownership of a heap allocated source string and frees it in
// freeVM(), since constant strings can point into it.
void keepSource(char* source);
void push(Value value);
Value pop();

#ifndef GLOBAL_TABLE
// 21.2: the slot for a global name, given a new undefined slot if it doesn't
// have one. Returns -1 if all 65,536 slots are used.
int globalSlot(ObjString* name);
#endif

// Adds a native function as a global, which takes arity arguments (24.2).
void defineNative(const char* name, int arity, NativeFn function);
// 24.3: reports a runtime error from inside a native function, with the
// usual stack trace, and returns EMPTY_VAL. A native returns that value to
// tell the VM the call failed:
//   if (!IS_NUMBER(args[0])) return nativeError("Expect a number.");
Value nativeError(const char* format, ...);

// natives.c: the native functions (24.4).
void defineNatives();

#endif
