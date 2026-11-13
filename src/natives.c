// The native functions. clock() is the book's. The rest are 24.4, and they
// use 24.2 (each one declares its arity, which the VM checks) and 24.3 (they
// report bad arguments with nativeError()).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "memory.h"
#include "object.h"
#include "vm.h"

static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

// sqrt(number)
static Value sqrtNative(int argCount, Value* args) {
  if (!IS_NUMBER(args[0])) {
    return nativeError("sqrt() expects a number.");
  }
  if (AS_NUMBER(args[0]) < 0) {
    return nativeError("sqrt() expects a number that isn't negative.");
  }
  return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

// floor(number): Lox has no integer division or modulo, so this is how a
// program gets them: floor(a / b) and a - b * floor(a / b).
static Value floorNative(int argCount, Value* args) {
  if (!IS_NUMBER(args[0])) {
    return nativeError("floor() expects a number.");
  }
  return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

// len(string)
static Value lenNative(int argCount, Value* args) {
  if (!IS_STRING(args[0])) {
    return nativeError("len() expects a string.");
  }
  return NUMBER_VAL(AS_STRING(args[0])->length);
}

// Writes a value the way print shows it.
static ObjString* valueToString(Value value) {
  char buffer[64];
  int length;
  switch (value.type) {
    case VAL_BOOL:
      length = snprintf(buffer, sizeof(buffer), "%s",
                        AS_BOOL(value) ? "true" : "false");
      break;
    case VAL_NIL:
      length = snprintf(buffer, sizeof(buffer), "nil");
      break;
    case VAL_NUMBER:
      length = snprintf(buffer, sizeof(buffer), "%g", AS_NUMBER(value));
      break;
    case VAL_OBJ:
      switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
          return AS_STRING(value);
        case OBJ_FUNCTION: {
          ObjString* name = AS_FUNCTION(value)->name;
          if (name == NULL) return copyString("<script>", 8);
          ObjString* result = makeString(name->length + 5);
          char* chars = stringBuffer(result);
          memcpy(chars, "<fn ", 4);
          memcpy(chars + 4, name->chars, name->length);
          chars[name->length + 4] = '>';
          return finishString(result);
        }
        case OBJ_NATIVE:
          return copyString("<native fn>", 11);
      }
      length = 0;
      break;
    default:
      length = 0;
      break;
  }
  return copyString(buffer, length);
}

// str(value): any value as a string, the way print shows it.
static Value strNative(int argCount, Value* args) {
  return OBJ_VAL(valueToString(args[0]));
}

// num(string): the number in a string, or nil if it isn't one, so a program
// can check input from readLine().
static Value numNative(int argCount, Value* args) {
  if (!IS_STRING(args[0])) {
    return nativeError("num() expects a string.");
  }
  ObjString* string = AS_STRING(args[0]);
  // With CONSTANT_STRINGS the characters might not end with '\0'.
  char buffer[64];
  if (string->length == 0 || string->length >= (int)sizeof(buffer)) {
    return NIL_VAL;
  }
  memcpy(buffer, string->chars, string->length);
  buffer[string->length] = '\0';

  // strtod() also takes things Lox doesn't, like "inf" and " 1", so only
  // digits, one '.', and a leading '-' are allowed.
  int i = buffer[0] == '-' ? 1 : 0;
  bool digits = false;
  bool dot = false;
  for (; i < string->length; i++) {
    if (buffer[i] >= '0' && buffer[i] <= '9') {
      digits = true;
    } else if (buffer[i] == '.' && !dot) {
      dot = true;
    } else {
      return NIL_VAL;
    }
  }
  if (!digits) return NIL_VAL;
  return NUMBER_VAL(strtod(buffer, NULL));
}

static bool isWholeNumber(Value value) {
  return IS_NUMBER(value) && AS_NUMBER(value) == floor(AS_NUMBER(value));
}

// substr(string, start, length): length characters starting at index start,
// counting from 0.
static Value substrNative(int argCount, Value* args) {
  if (!IS_STRING(args[0])) {
    return nativeError("substr() expects a string.");
  }
  if (!isWholeNumber(args[1]) || !isWholeNumber(args[2])) {
    return nativeError("substr() expects a whole number start and length.");
  }
  ObjString* string = AS_STRING(args[0]);
  double start = AS_NUMBER(args[1]);
  double length = AS_NUMBER(args[2]);
  if (start < 0 || length < 0 || start + length > string->length) {
    return nativeError("substr() range %g to %g is outside a string of "
                       "length %d.", start, start + length, string->length);
  }
  return OBJ_VAL(copyString(string->chars + (int)start, (int)length));
}

// type(value): "bool", "nil", "number", "string" or "function".
static Value typeNative(int argCount, Value* args) {
  const char* name = "function";
  switch (args[0].type) {
    case VAL_BOOL: name = "bool"; break;
    case VAL_NIL: name = "nil"; break;
    case VAL_NUMBER: name = "number"; break;
    case VAL_OBJ:
      if (IS_STRING(args[0])) name = "string";
      break;
    default: break;
  }
  return OBJ_VAL(copyString(name, (int)strlen(name)));
}

// readLine(): the next line of standard input without its newline, or nil
// at the end of the input.
static Value readLineNative(int argCount, Value* args) {
  int capacity = 64;
  int length = 0;
  char* line = ALLOCATE(char, capacity);
  int c;
  while ((c = getchar()) != EOF && c != '\n') {
    if (length + 1 == capacity) {
      line = GROW_ARRAY(char, line, capacity, capacity * 2);
      capacity *= 2;
    }
    line[length++] = (char)c;
  }

  Value result = NIL_VAL;
  if (c != EOF || length > 0) {
    if (length > 0 && line[length - 1] == '\r') length--;
    result = OBJ_VAL(copyString(line, length));
  }
  FREE_ARRAY(char, line, capacity);
  return result;
}

// error(message): stops the program with a runtime error, so Lox code can
// report bad arguments the same way natives do.
static Value errorNative(int argCount, Value* args) {
  ObjString* message = valueToString(args[0]);
  return nativeError("%.*s", message->length, message->chars);
}

void defineNatives() {
  defineNative("clock", 0, clockNative);
  defineNative("sqrt", 1, sqrtNative);
  defineNative("floor", 1, floorNative);
  defineNative("len", 1, lenNative);
  defineNative("str", 1, strNative);
  defineNative("num", 1, numNative);
  defineNative("substr", 3, substrNative);
  defineNative("type", 1, typeNative);
  defineNative("readLine", 0, readLineNative);
  defineNative("error", 1, errorNative);
}
