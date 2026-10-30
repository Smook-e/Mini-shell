# Mini Shell

This project implements a small Unix shell in C++. The shell reads commands
from standard input, expands wildcard arguments, creates pipelines, and
executes external programs.

## Building

Requirements:

- Linux or another POSIX-compatible operating system
- `g++` with C++17 support
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

The prompt is:

```text
myshell>
```

Commands can also be provided through standard input:

```sh
printf 'echo hello\nexit\n' | ./myshell
```

## Testing

Build and run the C++ test suite with:

```sh
make test
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

### Wildcard expansion

Wildcard expansion follows the csh-style behavior required by the assignment:

- `*` matches zero or more non-whitespace characters.
- `?` matches exactly one non-whitespace character.
- Each matching filename becomes a separate command argument.
- Wildcards can appear in path components.
- An unmatched wildcard remains unchanged.
- Filenames containing whitespace are not returned as wildcard matches.

Examples:

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
