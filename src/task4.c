#include "terminator.h"

int classify_redirection(char *token, const char **attached)
{
    *attached = NULL;
    if (token[0] == '<') {
        if (token[1] != '\0') *attached = token + 1;
        return 1;
    }
    if (token[0] == '>' && token[1] == '>') {
        if (token[2] != '\0') *attached = token + 2;
        return 3;
    }
    if (token[0] == '>') {
        if (token[1] != '\0') *attached = token + 1;
        return 2;
    }
    if (token[0] == '2' && token[1] == '>') {
        if (token[2] != '\0') *attached = token + 2;
        return 4;
    }
    return 0;
}

int extract_redirections(char *arguments[], int *argument_count_io,
                                Redirection *redirection,
                                char *bad_operator, size_t bad_operator_size)
{
    redirection->input_file = NULL;
    redirection->output_file = NULL;
    redirection->error_file = NULL;
    redirection->append = 0;

    int count = *argument_count_io;
    int write_at = 0;

    for (int read_at = 0; read_at < count; read_at++) {
        const char *attached = NULL;
        int kind = classify_redirection(arguments[read_at], &attached);
        if (kind == 0) {
            arguments[write_at++] = arguments[read_at];
            continue;
        }

        const char *file_name = attached;
        if (file_name == NULL) {
            if (read_at + 1 >= count) {

                snprintf(bad_operator, bad_operator_size, "%s",
                         arguments[read_at]);
                return -1;
            }
            file_name = arguments[++read_at];
        }

        if (kind == 1) {
            redirection->input_file = file_name;
        } else if (kind == 2) {
            redirection->output_file = file_name;
            redirection->append = 0;
        } else if (kind == 3) {
            redirection->output_file = file_name;
            redirection->append = 1;
        } else {
            redirection->error_file = file_name;
        }

    }

    arguments[write_at] = NULL;
    *argument_count_io = write_at;
    return 0;
}

int apply_redirections(Redirection *redirection)
{
    if (redirection->input_file != NULL) {
        int fd = open(redirection->input_file, O_RDONLY);
        if (fd == -1) {
            dprintf(STDERR_FILENO, "terminator: %s: %s\n",
                    redirection->input_file, strerror(errno));
            return -1;
        }
        if (fd != STDIN_FILENO) { dup2(fd, STDIN_FILENO); close(fd); }
    }

    if (redirection->output_file != NULL) {
        int flags = O_WRONLY | O_CREAT |
                    (redirection->append ? O_APPEND : O_TRUNC);
        int fd = open(redirection->output_file, flags, 0644);
        if (fd == -1) {
            dprintf(STDERR_FILENO, "terminator: %s: %s\n",
                    redirection->output_file, strerror(errno));
            return -1;
        }
        if (fd != STDOUT_FILENO) { dup2(fd, STDOUT_FILENO); close(fd); }
    }

    if (redirection->error_file != NULL) {
        int fd = open(redirection->error_file,
                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            dprintf(STDERR_FILENO, "terminator: %s: %s\n",
                    redirection->error_file, strerror(errno));
            return -1;
        }
        if (fd != STDERR_FILENO) { dup2(fd, STDERR_FILENO); close(fd); }
    }

    return 0;
}

int run_redirection_setup(TerminalTab *tab, Redirection *redirection)
{
    int error_pipe[2];
    if (pipe(error_pipe) == -1) return 1;

    pid_t child_pid = fork();
    if (child_pid == -1) {
        close(error_pipe[0]);
        close(error_pipe[1]);
        return 1;
    }

    if (child_pid == 0) {

        close(error_pipe[0]);
        dup2(error_pipe[1], STDERR_FILENO);
        close(error_pipe[1]);
        if (chdir(tab->cwd) != 0) _exit(1);
        _exit(apply_redirections(redirection) == 0 ? 0 : 1);
    }

    close(error_pipe[1]);

    char message[2048];
    size_t used = 0;
    ssize_t bytes;
    while (used < sizeof(message) - 1 &&
           (bytes = read(error_pipe[0], message + used,
                         sizeof(message) - 1 - used)) > 0) {
        used += (size_t)bytes;
    }
    close(error_pipe[0]);
    message[used] = '\0';

    int status;
    waitpid(child_pid, &status, 0);
    if (used > 0) add_output(tab, message);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
}
