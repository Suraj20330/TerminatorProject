# Terminator — multi-file build

The same Terminator as `src/task11_nocomments.c` (all 11 tasks plus the
dual thread/process modes), split into one file per task plus a main
file. No code was rewritten: every function body is identical to the
single-file version, only the linkage changed. There are no comments
in the source files, as requested.

## Files

| File | Contents |
| --- | --- |
| `terminator.h` | Shared header: all includes, macros, struct types, global declarations (`extern`), and prototypes for every function. Every `.c` file includes it. |
| `task1.c` | Task 1: X11 window, tabs, drawing, output buffer, tab open/close/switch. |
| `task2.c` | Task 2: built-ins (cd), external commands (fork + exec), the `execute_line` dispatcher, exit-status handling. |
| `task3.c` | Task 3: multiline input, quoting, tokenizer, `$VAR`/`$?` expansion, `$(...)` command substitution, Enter handling. |
| `task4.c` | Task 4: redirection (`<`, `>`, `>>`, `2>`). |
| `task5.c` | Task 5: pipelines of any length, including the synchronous engine used by substitution. |
| `task6.c` | Task 6: multiWatch (parallel commands, one pipe each, re-run every 2 seconds). |
| `task7.c` | Task 7: hotkeys, `~/.terminatorrc` bindings, input editing, history browsing, clipboard copy/paste, key dispatch. |
| `task8.c` | Task 8: signals and job control (Ctrl+C, Ctrl+Z, `&`, `jobs`, `fg`, `bg`). |
| `task9.c` | Task 9: shared history store, `~/.terminator_history` persistence, Ctrl+R search. |
| `task10.c` | Task 10: Tab completion. |
| `task11.c` | Task 11: bash script interpreter (`./script.sh`, `bash script.sh`, `source script.sh`, variables, `if`/`fi`, `exit`). |
| `proj.c` | Main file: `main()`, the GUI event loop, all global state, and the dual-mode engine (thread workers, process workers, GUI-worker protocol, tab-process cleanup). |
| `Makefile` | Builds everything into `./terminator`. |

## How the files link together

C compiles each `.c` file separately and then links them into one
program. Two things make that work here:

1. `terminator.h` declares everything shared: the `TerminalTab`,
   `Job`, `Watch`, `Segment`, `Redirection` types, all `#define`
   constants, every global variable as `extern`, and a prototype for
   every function. So any task file can call any other task's
   functions.
2. The functions are global, not `static`. In the single-file version
   most helpers were `static` (visible only inside that file). A
   `static` function cannot be called from another file, so the
   `static` keyword was removed when splitting. The bodies are
   otherwise untouched.
3. `proj.c` holds the single definition of every global variable
   (tabs, window, display, history store, bindings tables, and so
   on). The other files see them through the `extern` declarations
   in the header, so there is exactly one copy of each at link time.

## Build

Needs a C compiler and X11 development headers. On Ubuntu:

```
sudo apt install libx11-dev
```

Then, inside this folder:

```
make
```

That compiles all 12 `.c` files and links them into `./terminator`.
To rebuild from scratch: `make clean && make`. The direct compile
line, without `make`, is:

```
gcc -Wall -Wextra -g -pthread proj.c task1.c task2.c task3.c task4.c task5.c task6.c task7.c task8.c task9.c task10.c task11.c -o terminator -lX11 -pthread
```

## Run

Thread mode (default, one process, one thread per tab):

```
./terminator
```

Process mode (one process per tab, GUI talks to workers over pipes):

```
./terminator useProcess=True
```

The mode word must be typed exactly, with no spaces around `=`.
On WSL, run from an Ubuntu terminal with WSLg running (`DISPLAY`
should be `:0`); if the window does not appear, check `echo $DISPLAY`
and test with `xclock` first.

## Verified

- `make` compiles with zero warnings under `-Wall -Wextra`.
- Every function body in these files is byte-identical to the same
  function in `src/task11_nocomments.c` (checked mechanically), so the
  split changes linkage only, not behaviour.
- A 35-check harness linked against these objects passes 35/35,
  covering all 11 tasks (tokenizer, scanner, expansion, redirection,
  pipeline split, multiWatch parse, hotkeys, history, completion,
  script validation and tests, substitution, capture).
- The built binary starts and keeps running under Xvfb in both
  thread mode and process mode.
