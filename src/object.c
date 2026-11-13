#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->next = vm.objects;
  vm.objects = object;
  return object;
}

ObjFunction* newFunction() {
  ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  function->arity = 0;
  function->name = NULL;
  initChunk(&function->chunk);
  return function;
}

ObjNative* newNative(NativeFn function, int arity, const char* name) {
  ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = function;
  native->arity = arity;
  native->name = name;
  return native;
}

static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

// A string only goes on the object list once it is known to be unique, so
// finishString() can throw away a duplicate without unlinking it.
static ObjString* addString(ObjString* string, uint32_t hash) {
  string->hash = hash;
  string->obj.next = vm.objects;
  vm.objects = (Obj*)string;
  tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
  return string;
}

ObjString* makeString(int length) {
#ifdef CONSTANT_STRINGS
  ObjString* string = ALLOCATE(ObjString, 1);
  string->ownsChars = true;
  string->chars = ALLOCATE(char, length + 1);
#else
  ObjString* string = (ObjString*)reallocate(
      NULL, 0, sizeof(ObjString) + length + 1);
#endif
  string->obj.type = OBJ_STRING;
  string->length = length;
  return string;
}

char* stringBuffer(ObjString* string) {
  return (char*)string->chars;
}

ObjString* finishString(ObjString* string) {
  char* chars = stringBuffer(string);
  chars[string->length] = '\0';

  uint32_t hash = hashString(chars, string->length);
  ObjString* interned = tableFindString(&vm.strings, chars, string->length,
                                        hash);
  if (interned != NULL) {
    freeString(string);
    return interned;
  }

  return addString(string, hash);
}

ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;

  ObjString* string = makeString(length);
  memcpy(stringBuffer(string), chars, length);
  stringBuffer(string)[length] = '\0';
  return addString(string, hash);
}

ObjString* literalString(const char* chars, int length) {
#ifdef CONSTANT_STRINGS
  uint32_t hash = hashString(chars, length);
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;

  ObjString* string = ALLOCATE(ObjString, 1);
  string->obj.type = OBJ_STRING;
  string->ownsChars = false;
  string->length = length;
  string->chars = chars;
  return addString(string, hash);
#else
  return copyString(chars, length);
#endif
}

void freeString(ObjString* string) {
#ifdef CONSTANT_STRINGS
  if (string->ownsChars) {
    FREE_ARRAY(char, (char*)string->chars, string->length + 1);
  }
  FREE(ObjString, string);
#else
  reallocate(string, sizeof(ObjString) + string->length + 1, 0);
#endif
}

static void printFunction(ObjFunction* function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %.*s>", function->name->length, function->name->chars);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_FUNCTION:
      printFunction(AS_FUNCTION(value));
      break;
    case OBJ_NATIVE:
      printf("<native fn>");
      break;
    case OBJ_STRING:
      printf("%.*s", AS_STRING(value)->length, AS_CSTRING(value));
      break;
  }
}
