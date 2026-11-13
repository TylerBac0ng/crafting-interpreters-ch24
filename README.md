# Crafting Interpreters — Chapter 24 Challenges

Solutions to the end-of-chapter challenges from [_Crafting Interpreters_](https://craftinginterpreters.com/) by Robert Nystrom, chapter **24 (Calls and Functions)**. This is clox, the bytecode virtual machine in C, with the complete code through chapter 24, and it builds on my chapters 22-23 solutions. Lox now has functions, calls, `return` and native functions, along with everything from before (`const`, `switch`, `continue`, the `do` loop with a middle).

## Build and run

```sh
make                  # builds build/clox (a REPL, or pass a file)
make frameip          # build/clox-frameip, the book's run() with frame->ip (24.1)
make hashlocal        # build/clox-hashlocal, finds locals with a hash table (22.1)
make constant         # build/clox-constant, strings that point at the source (19.2)
make table            # build/clox-table, the book's table of globals (21.1)
make minimal          # build/clox-minimal, the reduced instruction set (18.1)
make trace            # prints the bytecode and each instruction as it runs
./run_tests.sh        # runs the tests in all six builds
make bench            # the benchmarks for 20.3, 21.1, 21.2, 22.1, 24.1 and 24.3
build/clox examples/primes.lox
```

## Chapter 24 — Calls and Functions

1. **The ip in a register.** `run()` keeps the ip in a `register` local and
   writes it back to the frame only before a call or a runtime error. After a
   call or a return, it reads the ip back from the new frame. `-DFRAME_IP`
   builds the book's version for comparison, and both builds are tested.
   `bench/calls_bench.c` times them (results in `bench/results.txt`). Whether
   it's worth it is a written answer.
2. **Native arity.** `ObjNative` has an `arity`, and calling a native with the
   wrong number of arguments gives the same runtime error as a Lox function.
3. **Native runtime errors.** A native calls `nativeError("...")`, which
   reports the error with the usual stack trace and returns `EMPTY_VAL`. The VM
   checks for that value after each native call. Lox values are never empty,
   so the native function type doesn't change. `-DUNCHECKED_NATIVES` removes
   the checks from 24.2 and 24.3, for the benchmark only.
4. **More natives.** `sqrt`, `floor`, `len`, `str`, `num`, `substr`, `type`,
   `readLine` and `error`, in `src/natives.c`. `examples/` has four programs
   that use them: primes and digit sums, string reversal and palindromes,
   statistics on numbers read from standard input, and an assert built on
   `error()`. What they add to the language is a written answer.

Also, functions closed the gap that 22.3 left open. A function compiled
before a global `const` was declared could still assign to it. Now
`OP_SET_GLOBAL` checks for that at runtime, using `vm.globalConsts` with
indexed globals or `vm.constGlobals` with `-DGLOBAL_TABLE`.

## Files changed from my chapters 22-23 code

| File | Change |
|---|---|
| `object.h`, `object.c`, `memory.c` | `ObjFunction` and `ObjNative`, from the book; a native's `arity` and name (24.2) |
| `vm.h`, `vm.c` | Call frames, `call()`, `callValue()`, stack traces, from the book; the ip in a local (24.1); native arity and error checks (24.2, 24.3); `nativeError()`; `push()` also moves each frame's slots when the stack grows (22.4); runtime check for `const` globals |
| `natives.c` | New: the native functions (24.4) |
| `compiler.h`, `compiler.c` | Functions, parameters, calls, `return`, one compiler per function, from the book. The 21.1 table of constants is now per function, since each function has its own chunk |
| `chunk.h`, `debug.c` | `OP_CALL` |
| `common.h`, `Makefile` | The `FRAME_IP` and `UNCHECKED_NATIVES` options, `-lm` |

## Tests

`./run_tests.sh` builds every program in `tests/`, runs it, and compares its
output with its `.expected` file, in six builds: default, minimal, constant,
table, hashlocal and frameip. A test uses `<name>.<build>.expected` if it has
one. It also runs each program in `examples/` with clox, with
`examples/<name>.input` as its standard input if it exists. All 96 checks
pass, and they also pass with `SANITIZE="-fsanitize=address,undefined -g"`.
The new test is `tests/ch24_calls.c`. Each program in it also checks that it
leaves no values on the stack and no frames behind, even after an error.

Some older expected outputs changed for chapter 24, and nothing else:
- Every chunk now ends with `OP_NIL` before `OP_RETURN`.
- The top-level chunk is named `<script>`.
- Stack slot 0 belongs to the script's function, so locals start at slot 1.
  The 22.4 test now fills 65,535 locals instead of 65,536.
- The ch19 allocation test doesn't count a resize of the string table.
