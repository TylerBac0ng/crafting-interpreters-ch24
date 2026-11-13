#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)   isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)   isObjType(value, OBJ_STRING)

#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)   ((ObjNative*)AS_OBJ(value))
#define AS_STRING(value)  ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_STRING,
} ObjType;

struct Obj {
  ObjType type;
  struct Obj* next;
};

typedef struct {
  Obj obj;
  int arity;
  Chunk chunk;
  ObjString* name;
} ObjFunction;

// 24.3: a native reports a runtime error by calling nativeError(), which
// returns EMPTY_VAL, and returning that. Lox values are never empty, so the
// VM can tell an error from any real result.
typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
  Obj obj;
  NativeFn function;
  // 24.2: how many arguments it takes.
  int arity;
  const char* name;
} ObjNative;

// 19.1: by default the characters are a flexible array member, stored right
// after the header in the same allocation.
//
// 19.2: with CONSTANT_STRINGS, chars is a pointer, and a string either owns
// the array it points to or points into the source code. Those characters
// aren't terminated, so always print them with "%.*s" and the length.
struct ObjString {
  Obj obj;
  int length;
  uint32_t hash;
#ifdef CONSTANT_STRINGS
  bool ownsChars;
  const char* chars;
#else
  char chars[];
#endif
};

// Allocates a string with room for length characters. The caller writes them
// to stringBuffer(), then must call finishString().
ObjFunction* newFunction();
ObjNative* newNative(NativeFn function, int arity, const char* name);

ObjString* makeString(int length);
char* stringBuffer(ObjString* string);
// Terminates, hashes and interns the string. If an equal string already
// exists, this frees the new one and returns the old one.
ObjString* finishString(ObjString* string);

ObjString* copyString(const char* chars, int length);
// The string for a literal or identifier in the source. With CONSTANT_STRINGS
// it points at the source, which must outlive the VM (see keepSource()).
ObjString* literalString(const char* chars, int length);
void freeString(ObjString* string);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
