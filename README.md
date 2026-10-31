# Mini Shell

This project implements a small Unix shell in C++. The shell reads commands
from standard input, expands wildcard arguments, creates pipelines, and
executes external programs.

## Building

Requirements:

- Linux or another POSIX-compatible operating system
- `g++` with C++17 support
- GNU Readline development headers and library
- `make`

Build the shell with:

```sh
make
```

This creates the `myshell` executable. Remove build artifacts with:

```sh
make clean
```

## Running

Start an interactive shell with:

```sh
./myshell
```

The prompt displays the current working directory in color, followed by an
input line:

```text
/home/user/project
$
```

The interactive prompt uses GNU Readline, providing line editing and command
history. Use the arrow keys to navigate previously entered commands. The
displayed path is refreshed after each command, including `cd`.

Commands can also be provided through standard input:

```sh
printf 'echo hello\nexit\n' | ./myshell
```

## Testing

Build and run the C++ test suite with:

```sh
make test
```

Run the test suite with AddressSanitizer, UndefinedBehaviorSanitizer, and
leak detection enabled:

```sh
make sanitizer
```

Run the repeated pipeline and redirection file-descriptor check:

```sh
make fd-check
```

The test suite in [command_tests.cc](command_tests.cc) directly tests:

- Tokenization and command parsing
- Invalid syntax handling
- Pipelines and output redirection
- Built-in `cd`
- Wildcard expansion, including unmatched wildcards
- External command execution

For a broader end-to-end shell run, [test_commands.txt](test_commands.txt)
contains commands covering quoting, pipelines, redirection, wildcard
expansion, background execution, errors, and cleanup. Run it with:

```sh
./myshell < test_commands.txt
```

## Supported features

### Built-in commands

- `cd [directory]` changes the shell's current directory. With no argument,
  it changes to `$HOME`.
- `exit` terminates the shell.

### External commands

Commands are resolved using the system `PATH`, so standard Unix programs such
as `echo`, `cat`, `ls`, and `wc` can be run normally.

Environment variables can be expanded with `$NAME` or `${NAME}`:

```sh
echo $HOME
echo "Running in ${PWD}"
```

Expansion is performed in unquoted and double-quoted text. Variables inside
single quotes remain literal.

### Pipelines

Use `|` to connect the output of one command to the input of the next:

```sh
cat README.md | wc -l
```

### Redirection

The shell supports:

```sh
command < input.txt
command > output.txt
command >> output.txt
command 2> errors.txt
command >>& output.txt
```

`>` replaces the output file, while `>>` appends to it. `>>&` sends both
standard output and standard error to the same file.

### Background execution

Append `&` to run a command without waiting for it to finish:

```sh
sleep 2 &
```

Child-process completion information is recorded in `shell.log`.

### Command lists

Multiple commands can be combined with these operators:

- `;` always runs the next command.
- `&&` runs the next command only if the previous command succeeds.
- `||` runs the next command only if the previous command fails.

The operators are evaluated from left to right:

```sh
make && echo "build succeeded" || echo "build failed"
echo first; echo second
```

### Wildcard expansion

Wildcard expansion follows the csh-style behavior required by the assignment:

- `*` matches zero or more non-whitespace characters.
- `?` matches exactly one non-whitespace character.
- Each matching filename becomes a separate command argument.
- Wildcards can appear in path components.
- An unmatched wildcard remains unchanged.
- Wildcards inside quotes are treated literally.
- Filenames containing whitespace are not returned as wildcard matches.

Examples:

echo "*.cc"
```sh
echo *
echo *.cc
echo c*.cc
echo M*f*
echo /tmp/*
echo /*t*/*
echo /dev/*
```

## Source files

- `command.cc` contains command parsing, wildcard expansion, process
  execution, pipelines, redirection, built-ins, and signal handling.
- `command.h` declares the command and simple-command data structures.
- `tokenizer.cc` and `tokenizer.h` tokenize input into commands, arguments,
  operators, and redirection tokens.
- `Makefile` defines the build and cleanup targets.
