// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

/* function that redirects either the input or output
 * to either stderr, stdin, stdout
 */
int redir(char *comm, char **argv, simple_command_t *command)
{
	int fd_in = -1, fd_out = -1, fd_err = -1;
	// define the append flags
	int out_flag = O_TRUNC, err_flag = O_TRUNC, result;

	// check for output append
	if (command->io_flags == IO_OUT_APPEND)
		out_flag = O_APPEND;

	// check for error append
	if (command->io_flags == IO_ERR_APPEND)
		err_flag = O_APPEND;

	// if redir stdout to command->out
	if (command->out) {
		fd_out = open(get_word(command->out), O_WRONLY | O_CREAT | out_flag, S_IRUSR | S_IWUSR);
		DIE(fd_out == -1, "open");

		dup2(fd_out, STDOUT_FILENO);
	}
	// if redir stdin to command
	if (command->in) {
		fd_in = open(get_word(command->in), O_RDONLY, S_IRUSR | S_IWUSR);
		DIE(fd_in == -1, "open");

		dup2(fd_in, STDIN_FILENO);
		close(fd_in);
	}

	// if redir stderr to command->out
	if (command->err) {
		// for the "&>" case
		if (command->out == command->err) {
			dup2(fd_out, STDERR_FILENO);
			close(fd_out);
		} else {
			fd_err = open(get_word(command->err), O_WRONLY | O_CREAT | err_flag, S_IRUSR | S_IWUSR);
			dup2(fd_err, STDERR_FILENO);
			close(fd_err);
		}
	}

	// execute the command
	result = execvp(comm, argv);

	// check the result
	if (result == -1) {
		fprintf(stderr, "Execution failed for '%s'\n", comm);
		return -1;
	}

	return result;
}

/**
 * Internal change-directory command.
 */
static int shell_cd(word_t *dir, simple_command_t *command)
{
	int original_stdout, original_stderr;

	// get the initial fd
	original_stdout = dup(STDOUT_FILENO);
	original_stderr = dup(STDERR_FILENO);

	int fd_out = -1, fd_err = -1, result;

	// define the append flags
	int out_flag = O_TRUNC, err_flag = O_TRUNC;

	// check for output append
	if (command->io_flags == IO_OUT_APPEND)
		out_flag = O_APPEND;

	// check for error append
	if (command->io_flags == IO_ERR_APPEND)
		err_flag = O_APPEND;

	// if redir the output
	if (command->out) {
		fd_out = open(get_word(command->out), O_WRONLY | O_CREAT | out_flag, S_IRUSR | S_IWUSR);
		DIE(fd_out == -1, "open"); // check for errors
		result = dup2(fd_out, STDOUT_FILENO); // redirect stdout to the file
		DIE(result < 0, "dup2"); // check for errors
	}

	// if redir stderr to command->out
	if (command->err) {
		fd_err = open(get_word(command->err), O_WRONLY | O_CREAT | err_flag, S_IRUSR | S_IWUSR);
		if (fd_err == -1) {
			perror("open");
			return -1;
		}
		dup2(fd_err, STDERR_FILENO);
	}

	result = chdir(get_word(dir));

	if (result == -1)
		perror("chdir");
	else
		result = 0;

	// restore to the original stdout
	dup2(original_stdout, STDOUT_FILENO);
	dup2(original_stderr, STDERR_FILENO);

	// close the files
	if (fd_out != -1)
		close(fd_out);

	if (fd_err != -1)
		close(fd_err);

	return result;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	return SHELL_EXIT;
}

// function that executes a simple command
static int exec_command(simple_command_t *command)
{
	int result = 1, size, status;
	char **argv = get_argv(command, &size);

	// if the exit/quit command
	if (!strcmp(argv[0], "exit") || !strcmp(argv[0], "quit"))
		return shell_exit();

	// if the cd command
	if (!strcmp(argv[0], "cd")) {
		// pass the dir name
		return shell_cd(command->params, command);
	}

	// check if an argument was passed
	if (size > 0) {
		// create a new process
		int pid = fork();

		// check the pid of the process
		switch (pid) {
			// in the child process
		case 0:
			// redirect stdin/stdout/stderr if it is needed
			result = redir(argv[0], argv, command);

			exit(result);
			break;

		default: // in the parent process
			// wait for the child process
			waitpid(pid, &status, 0);

			int exit_status = WEXITSTATUS(status);

			// return the exit status
			return exit_status;
		}
	}
	return result;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	if (s == NULL)
		return -1;

	int result = 0, cnt;
	char *comm = get_word(s->verb), *ptr = NULL;

	// replace the env variables with their value
	ptr = strchr(comm, '=');
	if (ptr != NULL) {
		// if it is a variable assignment
		char name[100];

		// copy the name of the variable
		for (cnt = 0; cnt < ptr - comm; cnt++)
			name[cnt] = comm[cnt];

		name[ptr - comm] = '\0';
		// set the value
		setenv(name, ptr + 1, 1);

		// exit the function
		return 0;
	}

	// check if the command is builtin
	result = exec_command(s);

	return result;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	// create a new process
	int status1, status2, result;
	pid_t pid1 = fork(), pid2;

	DIE(pid1 == -1, "fork"); // check for error in fork

	switch (pid1) {
		// in the children process
	case 0:
		result = 0;
		// execute the command
		if (cmd1->scmd)
			result = parse_simple(cmd1->scmd, level, father);
		else
			result = parse_command(cmd1, level + 1, cmd1->up);

		// close the process
		exit(result);
		break;

	// in the parent directory
	default:

		// create a new process
		pid2 = fork();

		switch (pid2) {
		// in the child process
		case 0:

			result = 0;
			// execute the command
			if (cmd2->scmd)
				result = parse_simple(cmd2->scmd, level, father);
			else
				result = parse_command(cmd2, level + 1, cmd2->up);

			// close the process
			exit(result);
					break;

		// in the parent process
		default:

			// wait fot the processes to end
			waitpid(pid1, &status1, 0);
			waitpid(pid2, &status2, 0);

			break;
	}
	break;
	}

	return true;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	// check if the pointer is null
	if (cmd1 == NULL)
		return false;

	int fd_pipe[2], result, status1 = 0, status2 = 0;
	pid_t pid1, pid2;

	// create a new pipe
	result = pipe(fd_pipe);
	DIE(result < 0, "pipe");

	// execute each command if it exists
	pid1 = fork();
	DIE(pid1 == -1, "fork");

	switch (pid1) {
	case 0: // in the first child process - cmd1
		close(fd_pipe[0]);
		dup2(fd_pipe[1], STDOUT_FILENO);
		close(fd_pipe[1]);
		// execute the first command
		if (cmd1->scmd)
			result = parse_simple(cmd1->scmd, level, father);
		else
			run_on_pipe(cmd1->cmd1, cmd1->cmd2, level + 1, cmd1);

		exit(result);

	default: // in the parent process
			// create the second child process
		pid2 = fork();
		DIE(pid2 == -1, "fork");
		close(fd_pipe[1]);

		switch (pid2) {
		case 0: // in the second child process - cmd2
			dup2(fd_pipe[0], STDIN_FILENO);
			close(fd_pipe[0]);
			close(fd_pipe[1]);

			// execute the second command
			result = parse_simple(cmd2->scmd, level, father);
			exit(result);

		default: // in the parent process
			// wait for both child processes to end
			waitpid(pid1, &status1, 0);
			waitpid(pid2, &status2, 0);
			close(fd_pipe[0]);
			close(fd_pipe[1]);

			// return the exit status of the second command (cmd2)
			return WEXITSTATUS(status2);
		}
	}

	return true;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	// check if the pointer is null
	if (c == NULL)
		return -1;

	// execute simple command and return the exit code
	if (c->op == OP_NONE)
		return parse_simple(c->scmd, level, father);

	int result = 0;

	// check the type of operation
	switch (c->op) {
	case OP_SEQUENTIAL:
		// go to the child nodes if they exists
		result = parse_command(c->cmd1, level + 1, c);

		// if the shell is closed
		if (result == SHELL_EXIT)
			return SHELL_EXIT;

		result = parse_command(c->cmd2, level + 1, c);

		// if the shell is closed
		if (result == SHELL_EXIT)
			return SHELL_EXIT;
		break;

	case OP_PARALLEL:
		result = run_in_parallel(c->cmd1, c->cmd2, level, c);

		// if the shell is closed
		if (result == SHELL_EXIT)
			return SHELL_EXIT;
		break;

	case OP_CONDITIONAL_NZERO:
		result = -1;
		// go to the child nodes if they exist
		if (c->cmd1->scmd == NULL && result != 0)
			result = parse_command(c->cmd1, level + 1, c);

		// execute the first command
		if (c->cmd1->scmd != NULL && result != 0)
			result = parse_simple(c->cmd1->scmd, level, c);

		if (c->cmd2->scmd == NULL && result != 0)
			result = parse_command(c->cmd2, level + 1, c);

		// execute the second command only if the first one is 0
		if (c->cmd2->scmd != NULL && result != 0)
			result = parse_simple(c->cmd2->scmd, level, c);
		break;

	case OP_CONDITIONAL_ZERO:
		result = 0;
		// go to the right child node if it exists
		if (c->cmd1->scmd == NULL && result == 0)
			result = parse_command(c->cmd1, level + 1, c);

		// execute the first command
		if (c->cmd1->scmd != NULL && result == 0)
			result = parse_simple(c->cmd1->scmd, level, c);

		// go to the left child node if it exists
		if (c->cmd2->scmd == NULL && result == 0)
			result = parse_command(c->cmd2, level + 1, c);

		// execute the second command only if the first one is 0
		if (c->cmd2->scmd != NULL && result == 0)
			result = parse_simple(c->cmd2->scmd, level, c);
		break;

	case OP_PIPE:
		// call the function that redirects the output and input and calls parse_command
		result = run_on_pipe(c->cmd1, c->cmd2, level, c);
		break;

	default:
		return SHELL_EXIT;
	}

	// return the exit code
	return result;
}
