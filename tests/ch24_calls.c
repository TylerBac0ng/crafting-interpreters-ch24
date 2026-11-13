// Chapter 24: functions and calls, the ip in a local variable (24.1), native
// arity checks (24.2), native runtime errors (24.3) and the new natives
// (24.4).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/vm.h"

// Runs a program, then prints how many values and frames it left behind.
// Every program should leave 0 of both, even after an error.
static void run(const char* source) {
  interpret(source);
  printf("stack: %d, frames: %d\n", (int)(vm.stackTop - vm.stack),
         vm.frameCount);
}

// A program with count parameters or arguments, for the 255 limits. The
// arguments are nil, since numbers would fill the constant table first.
static char* manyParameters(int count, bool call) {
  int size = count * 8 + 64;
  char* source = malloc(size);
  int length = snprintf(source, size, call ? "fun f() {} f(" : "fun f(");
  for (int i = 0; i < count; i++) {
    length += snprintf(source + length, size - length,
                       call ? "%snil" : "%sp%d", i > 0 ? "," : "", i);
  }
  snprintf(source + length, size - length, call ? ");" : ") {}");
  return source;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  initVM();

  printf("-- the book's functions\n");
  run("fun sum(a, b, c) { return a + b + c; } print 4 + sum(5, 6, 7);");
  run("fun noReturn() { print \"body\"; } print noReturn();");
  run("fun early(n) { if (n > 0) return \"positive\"; return; }"
      "print early(1); print early(-1);");
  run("fun fib(n) { if (n < 2) return n; return fib(n - 2) + fib(n - 1); }"
      "print fib(20);");
  run("fun f() {} print f; print clock; var g = f; print g == f;");
  // Functions declared in a block are locals, and parameters shadow globals.
  run("var a = \"global\";"
      "{ fun local(a) { var b = a + \"!\"; return b; } print local(\"arg\"); }"
      "print a;");
  // A function can call itself while its global is still being defined, and
  // call one declared after it.
  run("fun isEven(n) { if (n == 0) return true; return isOdd(n - 1); }"
      "fun isOdd(n) { if (n == 0) return false; return isEven(n - 1); }"
      "print isEven(10); print isOdd(7);");
  // return inside loops, switch and nested blocks drops their locals.
  run("fun find(n) {"
      "  for (var i = 0; i < 10; i = i + 1) {"
      "    var square = i * i;"
      "    switch (square) { case n: { var found = i; return found; } }"
      "  }"
      "  return nil;"
      "}"
      "print find(49); print find(50);");
  // continue in a function doesn't see a loop outside the function.
  run("for (var i = 0; i < 1; i = i + 1) { fun f() { continue; } }");
  run("fun f() { var x = 1; for (var i = 0; i < 3; i = i + 1) {"
      "  if (i == 1) continue; x = x + i; } return x; } print f();");
  // A function from one interpret() call, used by the next, as in the REPL.
  run("fun twice(x) { return x * 2; }");
  run("print twice(21);");

  printf("-- the book's errors\n");
  run("return 1;");
  run("fun f(a, b) {} f(1);");
  run("fun f() {} f(1, 2);");
  run("var notFn = \"str\"; notFn();");
  run("nil();");
  run("fun f() { f(); } f();");
  run("fun a() { b(); } fun b() { c(); }"
      "fun c() {\n"
      "  c(\"too\", \"many\");\n"
      "}\n"
      "a();");
  run("fun f(a, a) {}");
  run("fun f( {}");
  char* source = manyParameters(255, false);
  run(source);
  free(source);
  source = manyParameters(256, false);
  run(source);
  free(source);
  source = manyParameters(255, true);
  run(source);
  free(source);
  source = manyParameters(256, true);
  run(source);
  free(source);

  printf("-- 24.1: the saved ip is right in errors and after calls\n");
  // Each frame's line comes from the ip saved when it made its call.
  run("fun inner() {\n"
      "  var x = 1;\n"
      "  return x + nil;\n"
      "}\n"
      "fun outer() {\n"
      "  print \"before\";\n"
      "  inner();\n"
      "}\n"
      "outer();");
  // After a return, the caller carries on from the right instruction.
  run("fun one() { return 1; }"
      "var total = 0;"
      "for (var i = 0; i < 5; i = i + 1) total = total + one() + one();"
      "print total;");

  printf("-- 22.4 with frames: the stack grows during calls\n");
  // 40 frames with 20 locals each is more than the first 256 slots, so the
  // stack moves while calls are active and their slots have to move too.
  run("fun deep(n) {"
      "  var a0 = n; var a1 = n; var a2 = n; var a3 = n; var a4 = n;"
      "  var a5 = n; var a6 = n; var a7 = n; var a8 = n; var a9 = n;"
      "  var b0 = n; var b1 = n; var b2 = n; var b3 = n; var b4 = n;"
      "  var b5 = n; var b6 = n; var b7 = n; var b8 = n; var b9 = n;"
      "  if (n == 0) return 0;"
      "  var below = deep(n - 1);"
      "  return a0 + b9 + below;"
      "}"
      "print deep(40);");

  printf("-- 22.3 with functions: assigning a global constant\n");
  // The compiler catches this one.
  run("const limit = 10; fun f() { limit = 11; }");
  // The function is compiled before the const, so the VM catches this one.
  run("fun setLater() { later = 2; } const later = 1; setLater();");
  run("print later;");
  run("fun setLater2() { later = 3; }");
  run("setLater2();");
  run("fun later() {}");

  printf("-- 24.2: native arity\n");
  run("print clock(1);");
  run("print sqrt();");
  run("print substr(\"abc\", 1);");
  run("print len(\"a\", \"b\");");

  printf("-- 24.3: native runtime errors\n");
  run("print sqrt(\"four\");");
  run("print sqrt(-4);");
  run("print floor(nil);");
  run("print len(5);");
  run("print num(5);");
  run("print substr(5, 0, 1);");
  run("print substr(\"abc\", 0.5, 1);");
  run("print substr(\"abc\", 2, 2);");
  run("print substr(\"abc\", -1, 1);");
  run("fun check(x) {\n"
      "  if (x < 0) error(\"negative: \" + str(x));\n"
      "  return x;\n"
      "}\n"
      "print check(1);\n"
      "print check(-1);");
  run("error(nil);");

  printf("-- 24.4: natives\n");
  run("print type(clock()); print clock() >= 0;");
  run("print sqrt(2); print sqrt(0); print floor(2.9); print floor(-2.1);");
  run("print len(\"\"); print len(\"hello\");");
  run("print str(1) + str(2.5) + str(true) + str(nil) + str(\"s\");"
      "fun f() {} print str(f); print str(clock); print str(-0.000001);");
  run("print num(\"42\") + 1; print num(\"-3.5\"); print num(\"1.\");"
      "print num(\"\"); print num(\"-\"); print num(\"1e5\");"
      "print num(\" 1\"); print num(\"1.2.3\"); print num(\"inf\");");
  run("print substr(\"hello\", 0, 5); print substr(\"hello\", 1, 3);"
      "print substr(\"hello\", 5, 0) == \"\";");
  run("print type(1); print type(\"\"); print type(nil); print type(false);"
      "fun f() {} print type(f); print type(sqrt);");
  // Natives are ordinary globals, so a program can replace one.
  run("fun len(s) { return \"mine\"; } print len(\"abc\");");

  freeVM();
  return 0;
}
