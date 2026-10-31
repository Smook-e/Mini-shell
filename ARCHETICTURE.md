# Mini Shell Architecture

> The filename is intentionally `ARCHETICTURE.md`, matching the project
> requirement.

This document describes the current design and implementation of the mini
shell. The shell is a small POSIX-style command interpreter written in C++17.
It supports interactive Readline input, environment-variable expansion,
wildcard expansion, pipelines, redirection, command lists, background jobs,
and basic terminal job control.

## 1. Repository structure

| File | Responsibility |
| --- | --- |
| [`command.cc`](command.cc) | Parsing, built-ins, process creation, pipelines, redirection, command lists, job control, and the interactive prompt |
| [`command.h`](command.h) | `Command` and `SimpleCommand` declarations and public execution APIs |
| [`tokenizer.cc`](tokenizer.cc) | Converts an input line into typed tokens |
| [`tokenizer.h`](tokenizer.h) | Token types and `Token` structure |
| [`command_tests.cc`](command_tests.cc) | C++ unit and integration tests |
| [`tests/run_sanitizer.sh`](tests/run_sanitizer.sh) | AddressSanitizer, UndefinedBehaviorSanitizer, and leak checks |
| [`tests/check_file_descriptors.sh`](tests/check_file_descriptors.sh) | Repeated pipeline/redirection descriptor check |
| [`tests/check_job_control.py`](tests/check_job_control.py) | Pseudo-terminal job-control smoke test |
| [`Makefile`](Makefile) | Build and repeatable test targets |
| [`.github/workflows/ci.yml`](.github/workflows/ci.yml) | GitHub Actions build and test workflow |

## 2. High-level execution flow

Interactive execution follows this path:

```text
main
 ├─ setupSignals()
 └─ prompt()
     ├─ reapChildren()
     ├─ build colored current-directory prompt
     ├─ readline()
     ├─ tokenize()
     └─ executeLine()
         ├─ validate command-list separators
         ├─ split at ;, &&, and ||
         ├─ parse each pipeline
         └─ Command::execute()
             ├─ run a parent-side built-in, or
             ├─ create a process group and pipeline
             ├─ transfer terminal ownership for foreground jobs
             └─ wait or return for background jobs
```

Non-interactive input follows the same parsing and execution path. The
terminal-specific setup is enabled only when standard input is a terminal.

## 3. Tokenization

`tokenize()` scans one input line from left to right and emits a vector ending
in `TOKEN_EOF`.

### Recognized tokens

- Commands and arguments
- `|` pipeline operator
- `>` output redirection
- `>>` append redirection
- `<` input redirection
- `2>` standard-error redirection
- `>>&` combined output and error redirection
- `&` background marker
- `;` unconditional command separator
- `&&` success-conditional separator
- `||` failure-conditional separator

Operators are recognized even when no whitespace surrounds them. For example:

```sh
ls>out|cat
```

is emitted as `ls`, `>`, `out`, `|`, `cat`.

### Quoting metadata

Each `Token` includes a `quoted` flag. Quotes are removed from the token value,
but the flag records that some part of the token was quoted. The parser uses
this metadata to prevent wildcard expansion for quoted arguments:

```sh
echo "*.cc"   # literal *.cc
echo *.cc     # expands matching files
```

The current tokenizer treats a token containing any quote as quoted. This is
deliberately simple, but means mixed quoted and unquoted portions of one word
share the same expansion decision.

### Environment expansion

Unquoted and double-quoted variables are expanded during tokenization:

```sh
$HOME
${HOME}
```

Single-quoted variables remain literal. Undefined variables expand to an empty
string. Expansion is performed before parsing and wildcard expansion.

## 4. Parsing

`parse()` converts one pipeline segment into a `Command` object. Command lists
are split by `executeLine()` first, so `parse()` receives only one pipeline
and does not need to understand `;`, `&&`, or `||`.

The resulting `Command` contains:

- A vector of `SimpleCommand` pipeline stages
- Input redirection filename
- Output redirection filename
- Error redirection filename
- Append and combined-output flags
- Background execution flag

Each `SimpleCommand` owns a vector of argument strings. `insertArgument()`
performs `glob()` expansion unless the parser marks the argument as quoted.
Unmatched wildcards are retained literally.

Invalid syntax is reported through `parseError()`, which prints a diagnostic,
deletes the partially constructed command, and returns `nullptr`.

## 5. Command lists and exit status

`executeLine()` scans the complete token vector and validates separator
placement before executing anything. It rejects cases such as:

```sh
&& echo invalid
echo invalid &&
echo one || ; echo two
```

Pipeline segments are executed from left to right. The previous status controls
conditional execution:

| Separator | Next segment runs when |
| --- | --- |
| `;` | Always |
| `&&` | Previous status is `0` |
| `||` | Previous status is nonzero |

`Command::execute()` returns the status of the foreground pipeline. Built-ins
also return status values, allowing commands such as:

```sh
cd /missing && echo success || echo failure
```

to behave conditionally.

## 6. Built-ins

Built-ins run in the shell process when the command is a single simple
command. This is required for `cd`, because changing directory in a child
would not change the shell's directory.

Supported built-ins:

- `cd [directory]`
- `exit`
- `jobs`
- `bg %n`
- `fg %n`

`cd` uses `$HOME` when no directory is supplied. `exit` terminates the shell
immediately. Job-control built-ins operate on the in-memory job table.

## 7. Process creation and pipelines

For an external command, `Command::execute()` creates one child per pipeline
stage:

```text
stage 0 ── pipe ──> stage 1 ── pipe ──> stage 2
```

The parent closes each pipe endpoint as soon as it is no longer needed. Each
child:

1. Restores default signal dispositions.
2. Joins the pipeline process group.
3. Connects pipe descriptors to standard input/output.
4. Applies input/output/error redirections.
5. Builds the `argv` array.
6. Calls `execvp()`.

The first child PID becomes the process-group ID. `setpgid()` is called in both
the child and parent. This is intentional: the calls race, and performing the
operation from both sides avoids a window in which the child could receive a
terminal signal before the group is established.

## 8. Job-control model

### Shell startup

When standard input is a terminal, `setupSignals()`:

1. Ignores `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`, and `SIGTTOU`.
2. Places the shell in its own process group with `setpgid()`.
3. Makes the shell process group foreground with `tcsetpgrp()`.
4. Installs a lightweight `SIGCHLD` handler.
5. Sets `rl_catch_signals = 0`, preventing Readline from replacing the
   shell's signal policy.

The signal ignores must happen before `tcsetpgrp()`. Otherwise a shell started
in a background process group can receive `SIGTTOU` and stop before printing
its prompt.

### Job representation

The global job table stores:

- Shell job ID
- Process-group ID
- Original command text
- `Running`, `Stopped`, or `Done` state
- Remaining child PIDs

Foreground jobs temporarily own the terminal. Background jobs keep the shell
as the foreground terminal group and are announced as:

```text
[1] 4521
```

### Foreground jobs

The shell:

1. Calls `tcsetpgrp(STDIN_FILENO, job.pgid)`.
2. Waits for members of the process group with `WUNTRACED`.
3. Detects a stopped process group.
4. Restores the terminal to `shellPgid`.
5. Returns the pipeline status.

`Ctrl-C` and `Ctrl-Z` therefore target the foreground pipeline rather than the
shell.

### Background jobs

The shell does not transfer terminal ownership for a background job and
returns immediately. Its children are later observed by the reaping path.

### Reaping

The `SIGCHLD` handler does not call `waitpid()`. It only sets a
`sig_atomic_t` flag. Actual status collection happens synchronously through
`reapChildren()`, which uses:

```text
waitpid(-1, ..., WNOHANG | WUNTRACED | WCONTINUED)
```

This avoids the classic race where a signal handler reaps a child that a
foreground wait is expecting to observe as stopped.

The prompt calls `reapChildren()` before displaying each new prompt, and the
`jobs` built-in also refreshes job states before printing them.

## 9. Interactive prompt

The prompt is generated for every command, so the current directory remains
accurate after `cd`. It uses GNU Readline:

- Line editing
- Arrow-key history navigation
- History entries for non-empty lines

The displayed path is cyan and the input line appears below it:

```text
/home/user/project
$ 
```

Readline's `\001` and `\002` markers surround ANSI escape sequences so
Readline does not count color-control bytes as visible prompt width.

## 10. Error handling

The implementation reports errors for:

- Invalid pipeline syntax
- Missing redirection filenames
- Invalid command-list separators
- Failed `fork()`, `pipe()`, `open()`, `dup2()`, `setpgid()`, and `tcsetpgrp()`
- Missing commands
- Failed `cd`
- Unknown job IDs and malformed job-control arguments
- Failed external command execution

External commands that cannot be found return status `127`.

## 11. Testing architecture

### C++ tests

`make test` builds [`command_tests.cc`](command_tests.cc) with
`COMMAND_TEST_BUILD`. These tests cover:

- Tokenization
- Operators and command lists
- Environment expansion
- Quoting and wildcard behavior
- No-space redirections and pipelines
- Parser errors
- `cd`
- Redirection
- Pipelines

### Sanitizer test

`make sanitizer` compiles the C++ tests with AddressSanitizer and
UndefinedBehaviorSanitizer and enables leak detection.

### File-descriptor test

`make fd-check` runs repeated redirections and pipelines, takes `lsof`
snapshots, compares the shell's descriptor set during the workload, and
checks that all expected output files exist.

### Job-control test

`make job-control` runs the shell under a pseudo-terminal using Python's
`pty` module. It exercises:

- `Ctrl-Z`
- `jobs`
- `bg %1`
- `fg %1`
- `Ctrl-C` against a foreground job
- Protection of a background job from `Ctrl-C` typed at the shell prompt

The pseudo-terminal is necessary because ordinary pipes cannot reproduce
terminal foreground process-group behavior.

### Continuous integration

The GitHub Actions workflow in
[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on every push. It
installs GNU Readline and `lsof`, builds the shell, and runs all four Makefile
test targets.

## 12. Bugs found and fixes

Several concrete problems were found during implementation and testing.

### 12.1 Readline integration was incomplete

The initial prompt used `std::getline()`, so it had no line editing or
history. It was replaced with `readline()`, `using_history()`, and
`add_history()`, and the build was updated to link `-lreadline`.

### 12.2 Prompt color confused Readline's width calculation

Raw ANSI color sequences count as characters to Readline unless they are
wrapped in Readline's non-printing markers. The prompt now uses `\001` and
`\002` around escape sequences.

### 12.3 Operators failed without whitespace

The first tokenizer design depended too heavily on whitespace boundaries.
Operator scanning was updated so expressions such as `ls>out|cat` split into
the expected redirection and pipeline tokens.

### 12.4 Quoted wildcards were expanded incorrectly

Wildcard expansion originally saw only the final string value, so
`echo "*.cc"` could expand files just like `echo *.cc`. Token quote metadata
was added, and quoted arguments now bypass `glob()`.

### 12.5 Child reaping conflicted with job control

The original `SIGCHLD` handler called `waitpid(-1, ..., WNOHANG)`. That could
consume the status that foreground job-control code needed to detect a stop.
The handler now only records that a child-status event occurred; the main
execution path performs the actual `waitpid()` calls.

### 12.6 Startup could stop before the prompt

The shell initially called `tcsetpgrp()` before ignoring `SIGTTOU`. In a
background process group this could stop the shell before Readline displayed
anything. Signal ignores now happen first.

### 12.7 `%1` was rejected by job built-ins

Job parsing incorrectly required at least three characters, rejecting the
normal one-digit form `%1`. The minimum is two characters: `%` plus one digit.

### 12.8 Tests did not check job-control command failures

The first pseudo-terminal test only waited for another prompt. `bg %1` and
`fg %1` could print an error while the test still passed. The test was
strengthened to reject usage and “job not found” errors and to verify the
expected job states.

### 12.9 Test-owned `getcwd()` allocations leaked

The tests used `getcwd(nullptr, 0)` and converted the result directly to
`std::string` without freeing the allocated buffer. LeakSanitizer caught this.
The tests now retain the pointer, construct the string, and call `free()`.

## 13. Current limitations

This is an educational shell, not a complete POSIX shell. Known limitations
include:

- No shell grammar for grouping, subshells, or operator precedence beyond
  left-to-right command-list evaluation.
- No command substitution.
- No wildcard expansion inside a partially quoted word model.
- No persistent job table across shell restarts.
- No `%+`, `%-`, or PID-based job specifications.
- No `disown`, `wait`, or `kill` built-ins.
- Completed background jobs are removed when the job table is refreshed rather
  than displayed with a full shell-style notification system.
- Redirection syntax is intentionally limited to the operators implemented by
  the tokenizer.
- Pipeline status is based on the status observed from the final process.

These limitations are deliberate simplifications for the lab implementation.
