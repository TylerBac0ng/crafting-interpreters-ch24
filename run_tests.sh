#!/bin/bash
# Builds every test in tests/ against the clox sources, runs it, and compares
# its output with tests/<name>.expected, then runs every Lox program in
# examples/ with clox. Everything runs in six builds, since each one changes
# how something works:
#   default   flexible array strings (19.1) and indexed globals (21.2)
#   minimal   the reduced instruction set (18.1)
#   constant  strings that point at the source (19.2)
#   table     the book's table of globals with reused constants (21.1)
#   hashlocal the compiler finds locals with a hash table (22.1)
#   frameip   the book's run(), which reads the ip through the frame (24.1)
# A test uses tests/<name>.<build>.expected if there is one. A test can ask
# for extra compiler flags with a "// flags:" first line. CC and SANITIZE can
# be set from the environment. An example program reads examples/<name>.input
# as its standard input if there is one.
cd "$(dirname "$0")"
CC=${CC:-cc}
CFLAGS="-std=c99 -Wall -Wextra -Wno-unused-parameter -O2 $SANITIZE"
SOURCES=$(ls src/*.c | grep -v main.c)
mkdir -p build
pass=0
fail=0

check() {
  local name=$1 expected=$2 actual=$3
  if diff -u "$expected" "$actual" > build/diff.txt; then
    echo "PASS $name"
    pass=$((pass + 1))
  else
    echo "FAIL $name"
    cat build/diff.txt
    fail=$((fail + 1))
  fi
}

for mode in default minimal constant table hashlocal frameip; do
  flags=$CFLAGS
  [ $mode = minimal ] && flags="$flags -DMINIMAL_INSTRUCTIONS"
  [ $mode = constant ] && flags="$flags -DCONSTANT_STRINGS"
  [ $mode = table ] && flags="$flags -DGLOBAL_TABLE"
  [ $mode = hashlocal ] && flags="$flags -DLOCAL_HASH"
  [ $mode = frameip ] && flags="$flags -DFRAME_IP"

  $CC $flags src/*.c -o build/clox-$mode -lm || exit 1

  for test in tests/*.c; do
    name=$(basename "$test" .c)
    extra=$(sed -n '1s|^// flags: ||p' "$test")
    $CC $flags $extra $SOURCES "$test" -o build/$name-$mode -lm || exit 1
    build/$name-$mode > build/$name.out 2>&1
    expected=tests/$name.expected
    [ -f tests/$name.$mode.expected ] && expected=tests/$name.$mode.expected
    check "$name ($mode)" $expected build/$name.out
  done

  # The REPL reuses one line buffer, which matters for 19.2.
  build/clox-$mode < tests/repl.input > build/repl.out 2>&1
  check "repl ($mode)" tests/repl.expected build/repl.out

  # 24.4: programs that use the native functions.
  for program in examples/*.lox; do
    name=$(basename "$program" .lox)
    input=/dev/null
    [ -f examples/$name.input ] && input=examples/$name.input
    build/clox-$mode "$program" < $input > build/$name.out 2>&1
    echo "exit $?" >> build/$name.out
    check "examples/$name ($mode)" examples/$name.expected build/$name.out
  done
done

echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
