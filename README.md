# Minishell

## Description

Minishell is a custom command-line interpreter designed to emulate the core functionalities of the Bash shell.
This project focuses on process management, I/O redirection, and the implementation of a robust command execution
engine. It demonstrates the use of Linux system calls to create child processes, manage environment variables and
handle command chaining using pipes and conditional operators.


## Objectives

* Master process creation and synchronization using `fork()`, `execvp()`, and `waitpid()`.
* Implement I/O redirection and inter-process communication using `open()`, `dup2()`, and `pipe()`.
* Understand shell-specific logic such as built-in commands and environment variable expansion.
* Handle complex command syntax including sequential, parallel, and conditional execution.

## Shell Functionalities

### Built-in Commands
* `cd <path>`: Changes the current working directory. Supports both absolute and relative paths.
* `pwd`: Prints the current absolute working directory path.
* `exit` / `quit`: Gracefully terminates the minishell process.

### Command Execution
Every external application is executed within a separate child process. The shell searches for executables in the
current directory or via absolute paths provided by the user.


### Environment Variables
The shell inherits variables from the parent process and supports dynamic assignment:
* Assignment: `VAR=value`
* Expansion: `$VAR` (expands to an empty string if undefined).

### Supported Operators
The shell parses and executes commands based on the following operator priorities (from highest to lowest):

| Priority | Operator | Description |
| :--- | :--- | :--- |
| 1 | `\|` | **Pipe**: Redirects `stdout` of one command to `stdin` of the next. |
| 2 | `&&` / `\|\|` | **Conditional**: `&&` stops on first error; `\|\|` stops on first success. |
| 3 | `&` | **Parallel**: Executes commands simultaneously in the background. |
| 4 | `;` | **Sequential**: Executes commands one after another. |



### I/O Redirection
* `<`: Input redirection from a file.
* `>` / `>>`: Output redirection (overwrite / append).
* `2>` / `2>>`: Standard error redirection (overwrite / append).
* `&>`: Redirects both `stdout` and `stderr` to a single file.

## Project Structure

* `src/`: Skeleton implementation containing the main shell loop and execution logic.
* `util/`: Contains a pre-built parser to transform command strings into an execution tree.
* `tests/`: Automated testing infrastructure and reference inputs/outputs.

## Building and Running

### Compilation
Build the minishell executable by running `make` in the `src/` directory:
```
cd src/
make
```

### Running the Shell
Start the shell in interactive mode:

```
./minishell
```

## Testing and Grading

The test script validates accuracy, operator logic and redirection.
```
cd tests/
make check
```

## Technical Implementation Notes

 - **Process Management:** Uses `fork()` to clone the shell process and `execvp()` to replace the child's image with the target application.

 - **Redirection:** Implemented using `dup2()` to clone file descriptors into the standard slots (0, 1, 2) before execution.

 - **Piping:** Uses `pipe()` to create a unidirectional data channel, ensuring the writing end is closed in the reading process to prevent hangs.

