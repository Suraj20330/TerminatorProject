#include "terminator.h"

int split_pipeline(char *arguments[], int argument_count,
                          Segment segments[], int max_segments,
                          char *bad_operator, size_t bad_operator_size)
{

    int segment_count = 0;
    int start = 0;
    for (int i = 0; i <= argument_count; i++) {
        if (i == argument_count || strcmp(arguments[i], "|") == 0) {
            if (segment_count >= max_segments) return -3;
            segments[segment_count].argv = arguments + start;
            segments[segment_count].argc = i - start;
            if (i < argument_count)
                arguments[i] = NULL;
            segment_count++;
            start = i + 1;
        }
    }

    for (int s = 0; s < segment_count; s++) {
        if (extract_redirections(segments[s].argv, &segments[s].argc,
                                 &segments[s].redir,
                                 bad_operator, bad_operator_size) != 0)
            return -2;
        if (segments[s].argc == 0 && segment_count > 1)
            return -1;
    }
    return segment_count;
}

int launch_pipeline_into(TerminalTab *tab, Segment segments[],
                                int segment_count, pid_t pids_out[],
                                int *forked_out)
{
    int stage_pipes[MAX_SEGMENTS - 1][2];
    int output_pipe[2];

    if (pipe(output_pipe) == -1) {
        add_line(tab, "Error: Cannot create output pipe.");
        return -1;
    }
    for (int i = 0; i < segment_count - 1; i++) {
        if (pipe(stage_pipes[i]) == -1) {
            for (int j = 0; j < i; j++) {
                close(stage_pipes[j][0]);
                close(stage_pipes[j][1]);
            }
            close(output_pipe[0]);
            close(output_pipe[1]);
            add_line(tab, "Error: Cannot create pipeline.");
            return -1;
        }
    }

    pid_t group_leader = -1;
    int forked = 0;
    for (int i = 0; i < segment_count; i++) {
        pid_t child_pid = fork();
        if (child_pid == -1) break;
        if (child_pid == 0) {

            Segment *segment = &segments[i];
            close(output_pipe[0]);

            if (i == 0) setpgid(0, 0);
            else if (group_leader > 0) setpgid(0, group_leader);

            dup2(output_pipe[1], STDERR_FILENO);

            if (chdir(tab->cwd) != 0) {
                dprintf(STDERR_FILENO, "terminator: cannot enter folder %s\n",
                        tab->cwd);
                _exit(1);
            }

            if (i > 0) dup2(stage_pipes[i - 1][0], STDIN_FILENO);
            if (i < segment_count - 1)
                dup2(stage_pipes[i][1], STDOUT_FILENO);

            if (apply_redirections(&segment->redir) != 0) _exit(1);

            if (i == segment_count - 1 && segment->redir.output_file == NULL)
                dup2(output_pipe[1], STDOUT_FILENO);

            for (int j = 0; j < segment_count - 1; j++) {
                close(stage_pipes[j][0]);
                close(stage_pipes[j][1]);
            }
            close(output_pipe[1]);

            if (segment->argc > 0) {
                int builtin_status =
                    builtin_to_fd(tab, segment->argv, segment->argc);
                if (builtin_status != -1) _exit(builtin_status);
            }

            exec_or_report(tab, segment->argv);
        }

        if (forked == 0) {
            group_leader = child_pid;
            setpgid(child_pid, child_pid);
        } else if (group_leader > 0) {
            setpgid(child_pid, group_leader);
        }
        pids_out[forked++] = child_pid;
    }

    if (forked == 0) {
        for (int j = 0; j < segment_count - 1; j++) {
            close(stage_pipes[j][0]);
            close(stage_pipes[j][1]);
        }
        close(output_pipe[0]);
        close(output_pipe[1]);
        add_line(tab, "Error: Cannot create child process.");
        return -1;
    }

    for (int j = 0; j < segment_count - 1; j++) {
        close(stage_pipes[j][0]);
        close(stage_pipes[j][1]);
    }
    close(output_pipe[1]);

    int flags = fcntl(output_pipe[0], F_GETFL, 0);
    if (flags != -1) fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);

    *forked_out = forked;
    return output_pipe[0];
}

void launch_pipeline(TerminalTab *tab, Segment segments[],
                            int segment_count)
{
    pid_t pids[MAX_SEGMENTS];
    int forked = 0;
    int read_fd = launch_pipeline_into(tab, segments, segment_count,
                                       pids, &forked);
    if (read_fd < 0) return;
    tab->output_fd = read_fd;
    tab->pipe_count = forked;
    for (int i = 0; i < forked; i++) tab->pipe_pids[i] = pids[i];
    tab->last_pid = pids[forked - 1];
    tab->fg_pgid = pids[0];
}

void reap_pipeline(TerminalTab *tab)
{
    if (tab->pipe_count == 0) return;

    int still_alive = 0;
    for (int i = 0; i < tab->pipe_count; i++) {
        if (tab->pipe_pids[i] <= 0) continue;
        int status;
        pid_t result = waitpid(tab->pipe_pids[i], &status, WNOHANG);
        if (result == tab->pipe_pids[i]) {
            if (tab->pipe_pids[i] == tab->last_pid) {
                save_exit_status(tab, status);
                tab->last_pid = -1;
            }
            tab->pipe_pids[i] = -1;
        } else {
            still_alive = 1;
        }
    }
    if (!still_alive && tab->last_pid == -1) {
        tab->pipe_count = 0;

        if (tab->fg_preset_valid) {
            tab->last_exit_status = tab->fg_preset_code;
            tab->fg_preset_valid = 0;
        }
    }
}

void run_pipeline_sync(TerminalTab *tab, Segment segments[],
                              int segment_count)
{
    int stage_pipes[MAX_SEGMENTS - 1][2];
    for (int i = 0; i < segment_count - 1; i++) {
        if (pipe(stage_pipes[i]) == -1) _exit(1);
    }

    pid_t pids[MAX_SEGMENTS];
    int forked = 0;
    for (int i = 0; i < segment_count; i++) {
        pid_t stage_pid = fork();
        if (stage_pid == -1) break;
        if (stage_pid == 0) {

            Segment *segment = &segments[i];

            if (i > 0) dup2(stage_pipes[i - 1][0], STDIN_FILENO);
            if (i < segment_count - 1)
                dup2(stage_pipes[i][1], STDOUT_FILENO);

            if (apply_redirections(&segment->redir) != 0) _exit(1);

            for (int j = 0; j < segment_count - 1; j++) {
                close(stage_pipes[j][0]);
                close(stage_pipes[j][1]);
            }

            if (segment->argc > 0) {
                int builtin_status =
                    builtin_to_fd(tab, segment->argv, segment->argc);
                if (builtin_status != -1) _exit(builtin_status);
            }
            exec_or_report(tab, segment->argv);
        }
        pids[forked++] = stage_pid;
    }

    for (int j = 0; j < segment_count - 1; j++) {
        close(stage_pipes[j][0]);
        close(stage_pipes[j][1]);
    }

    int last_status = 1;
    for (int i = 0; i < forked; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == forked - 1) {
            if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
        }
    }
    _exit(last_status);
}
