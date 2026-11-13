#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

VM vm; // [one]
static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
}
// Prints the message and a stack trace. Every frame's ip must be up to date,
// so with 24.1 run() saves its local ip into the frame before calling this.
static void vRuntimeError(const char* format, va_list args) {
  // Anything the program printed comes first, even when stdout is a pipe.
  fflush(stdout);
  vfprintf(stderr, format, args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%.*s()\n", function->name->length,
              function->name->chars);
    }
  }

  resetStack();
}
static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vRuntimeError(format, args);
  va_end(args);
}
Value nativeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vRuntimeError(format, args);
  va_end(args);
  return EMPTY_VAL;
}

#ifdef CONSTANT_STRINGS
typedef struct KeptSource {
  struct KeptSource* next;
  char* source;
} KeptSource;
#endif

void keepSource(char* source) {
#ifdef CONSTANT_STRINGS
  KeptSource* kept = malloc(sizeof(KeptSource));
  if (kept == NULL) exit(1);
  kept->source = source;
  kept->next = vm.sources;
  vm.sources = kept;
#else
  free(source);
#endif
}

#ifndef GLOBAL_TABLE
int globalSlot(ObjString* name) {
  Value slot;
  if (tableGet(&vm.globalNames, OBJ_VAL(name), &slot)) {
    return (int)AS_NUMBER(slot);
  }

  if (vm.globalValues.count > UINT16_MAX) return -1;

  int index = vm.globalValues.count;
  writeValueArray(&vm.globalValues, EMPTY_VAL);
  writeValueArray(&vm.globalIdentifiers, OBJ_VAL(name));
  writeValueArray(&vm.globalConsts, BOOL_VAL(false));
  tableSet(&vm.globalNames, OBJ_VAL(name), NUMBER_VAL((double)index));
  return index;
}
#endif

void defineNative(const char* name, int arity, NativeFn function) {
  ObjString* string = copyString(name, (int)strlen(name));
  Value native = OBJ_VAL(newNative(function, arity, name));
#ifdef GLOBAL_TABLE
  tableSet(&vm.globals, OBJ_VAL(string), native);
#else
  // globalSlot() can grow globalValues, so it has to run first.
  int slot = globalSlot(string);
  vm.globalValues.values[slot] = native;
#endif
}

void initVM() {
  vm.stackCapacity = 256;
  vm.stack = GROW_ARRAY(Value, NULL, 0, vm.stackCapacity);
  resetStack();
  initTable(&vm.constGlobals);
  vm.objects = NULL;
#ifdef GLOBAL_TABLE
  initTable(&vm.globals);
#else
  initTable(&vm.globalNames);
  initValueArray(&vm.globalValues);
  initValueArray(&vm.globalIdentifiers);
  initValueArray(&vm.globalConsts);
#endif
  initTable(&vm.strings);
#ifdef CONSTANT_STRINGS
  vm.sources = NULL;
#endif

  defineNatives();
}

void freeVM() {
#ifdef GLOBAL_TABLE
  freeTable(&vm.globals);
#else
  freeTable(&vm.globalNames);
  freeValueArray(&vm.globalValues);
  freeValueArray(&vm.globalIdentifiers);
  freeValueArray(&vm.globalConsts);
#endif
  freeTable(&vm.strings);
  freeTable(&vm.constGlobals);
  FREE_ARRAY(Value, vm.stack, vm.stackCapacity);
  vm.stack = NULL;
  vm.stackCapacity = 0;
  freeObjects();
#ifdef CONSTANT_STRINGS
  while (vm.sources != NULL) {
    KeptSource* next = vm.sources->next;
    free(vm.sources->source);
    free(vm.sources);
    vm.sources = next;
  }
#endif
}
void push(Value value) {
  // 22.4: grow the stack when it is full. It may move, so stackTop and every
  // frame's slots pointer are rebuilt from their indexes.
  int count = (int)(vm.stackTop - vm.stack);
  if (count == vm.stackCapacity) {
    int slots[FRAMES_MAX];
    for (int i = 0; i < vm.frameCount; i++) {
      slots[i] = (int)(vm.frames[i].slots - vm.stack);
    }

    int oldCapacity = vm.stackCapacity;
    vm.stackCapacity = GROW_CAPACITY(oldCapacity);
    vm.stack = GROW_ARRAY(Value, vm.stack, oldCapacity, vm.stackCapacity);
    vm.stackTop = vm.stack + count;
    for (int i = 0; i < vm.frameCount; i++) {
      vm.frames[i].slots = vm.stack + slots[i];
    }
  }

  *vm.stackTop = value;
  vm.stackTop++;
}
Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}
static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}
static bool call(ObjFunction* function, int argCount) {
  if (argCount != function->arity) {
    runtimeError("Expected %d arguments but got %d.",
                 function->arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  frame->ip = function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}
static bool callNative(ObjNative* native, int argCount) {
#ifndef UNCHECKED_NATIVES
  // 24.2: without this, sqrt() with no arguments would read whatever is
  // above the top of the stack.
  if (argCount != native->arity) {
    runtimeError("Expected %d arguments but got %d.", native->arity,
                 argCount);
    return false;
  }
#endif

  Value result = native->function(argCount, vm.stackTop - argCount);
#ifndef UNCHECKED_NATIVES
  // 24.3: the native already reported the error.
  if (IS_EMPTY(result)) return false;
#endif

  vm.stackTop -= argCount + 1;
  push(result);
  return true;
}
static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_FUNCTION:
        return call(AS_FUNCTION(callee), argCount);
      case OBJ_NATIVE:
        return callNative(AS_NATIVE(callee), argCount);
      default:
        break; // Non-callable object type.
    }
  }
  runtimeError("Can only call functions and classes.");
  return false;
}
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}
static void concatenate() {
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());

  int length = a->length + b->length;
  ObjString* result = makeString(length);
  char* chars = stringBuffer(result);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  push(OBJ_VAL(finishString(result)));
}
#ifndef GLOBAL_TABLE
// The name of the global in a slot, for an error message.
static ObjString* globalName(int slot) {
  return AS_STRING(vm.globalIdentifiers.values[slot]);
}
#endif
static InterpretResult run() {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

  // 24.1: by default the ip lives in a local variable, which the C compiler
  // can keep in a register. It is written back to the frame before anything
  // that reads frame->ip (a call or a runtime error), and read again after a
  // call or return switches frames. With FRAME_IP, every instruction goes
  // through frame->ip, as in the book.
#ifdef FRAME_IP
#define IP (frame->ip)
#define SAVE_IP() ((void)0)
#define LOAD_FRAME() (frame = &vm.frames[vm.frameCount - 1])
#else
  register uint8_t* ip = frame->ip;
#define IP ip
#define SAVE_IP() (frame->ip = ip)
#define LOAD_FRAME() \
    (frame = &vm.frames[vm.frameCount - 1], ip = frame->ip)
#endif

#define READ_BYTE() (*IP++)
#define READ_CONSTANT() \
    (frame->function->chunk.constants.values[READ_BYTE()])
#define READ_SHORT() \
    (IP += 2, (uint16_t)((IP[-2] << 8) | IP[-1]))
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define RUNTIME_ERROR(...) \
    do { \
      SAVE_IP(); \
      runtimeError(__VA_ARGS__); \
      return INTERPRET_RUNTIME_ERROR; \
    } while (false)
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        RUNTIME_ERROR("Operands must be numbers."); \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(&frame->function->chunk,
        (int)(IP - frame->function->chunk.code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
#ifndef MINIMAL_INSTRUCTIONS
      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
#endif
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
#ifndef MINIMAL_INSTRUCTIONS
      case OP_NOT_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(!valuesEqual(a, b)));
        break;
      }
      case OP_GREATER:       BINARY_OP(BOOL_VAL, >); break;
      case OP_GREATER_EQUAL: BINARY_OP(BOOL_VAL, >=); break;
      case OP_LESS_EQUAL:    BINARY_OP(BOOL_VAL, <=); break;
#endif
      case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          RUNTIME_ERROR("Operands must be two numbers or two strings.");
        }
        break;
      }
#ifndef MINIMAL_INSTRUCTIONS
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
#endif
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;
#ifndef MINIMAL_INSTRUCTIONS
      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          RUNTIME_ERROR("Operand must be a number.");
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
#endif
      case OP_POP: pop(); break;
      case OP_DUP: push(peek(0)); break;
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_GET_LOCAL_LONG: {
        uint16_t slot = READ_SHORT();
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL_LONG: {
        uint16_t slot = READ_SHORT();
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_PRINT: {
        printValue(pop());
        printf("\n");
        break;
      }
#ifdef GLOBAL_TABLE
      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, OBJ_VAL(name), peek(0));
        pop();
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, OBJ_VAL(name), &value)) {
          RUNTIME_ERROR("Undefined variable '%.*s'.", name->length,
                        name->chars);
        }
        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        // 22.3: a function compiled before the const was declared.
        Value unused;
        if (vm.constGlobals.count > 0 &&
            tableGet(&vm.constGlobals, OBJ_VAL(name), &unused)) {
          RUNTIME_ERROR("Can't assign to constant '%.*s'.", name->length,
                        name->chars);
        }
        if (tableSet(&vm.globals, OBJ_VAL(name), peek(0))) {
          tableDelete(&vm.globals, OBJ_VAL(name));
          RUNTIME_ERROR("Undefined variable '%.*s'.", name->length,
                        name->chars);
        }
        break;
      }
#else
      // 21.2: the slot was found at compile time, so no hashing happens here.
      // The only runtime work left is checking that the slot has a value.
      case OP_DEFINE_GLOBAL: {
        vm.globalValues.values[READ_SHORT()] = pop();
        break;
      }
      case OP_GET_GLOBAL: {
        uint16_t slot = READ_SHORT();
        Value value = vm.globalValues.values[slot];
        if (IS_EMPTY(value)) {
          ObjString* name = globalName(slot);
          RUNTIME_ERROR("Undefined variable '%.*s'.", name->length,
                        name->chars);
        }
        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        uint16_t slot = READ_SHORT();
        if (IS_EMPTY(vm.globalValues.values[slot])) {
          ObjString* name = globalName(slot);
          RUNTIME_ERROR("Undefined variable '%.*s'.", name->length,
                        name->chars);
        }
        // 22.3: a function compiled before the const was declared.
        if (AS_BOOL(vm.globalConsts.values[slot])) {
          ObjString* name = globalName(slot);
          RUNTIME_ERROR("Can't assign to constant '%.*s'.", name->length,
                        name->chars);
        }
        vm.globalValues.values[slot] = peek(0);
        break;
      }
#endif
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        IP += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) IP += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        IP -= offset;
        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE();
        SAVE_IP();
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        LOAD_FRAME();
        break;
      }
      case OP_RETURN: {
        Value result = pop();
        vm.frameCount--;
        if (vm.frameCount == 0) {
          pop();
          return INTERPRET_OK;
        }

        vm.stackTop = frame->slots;
        push(result);
        LOAD_FRAME();
        break;
      }
    }
  }

#undef IP
#undef SAVE_IP
#undef LOAD_FRAME
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef RUNTIME_ERROR
#undef BINARY_OP
}
InterpretResult runFunction(ObjFunction* function) {
  push(OBJ_VAL(function));
  call(function, 0);
  return run();
}
InterpretResult interpret(const char* source) {
  ObjFunction* function = compile(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  return runFunction(function);
}
