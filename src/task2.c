#include "terminator.h"

void save_exit_status(TerminalTab *tab, int wait_status)
{
    if (WIFEXITED(wait_status)) {
        tab->last_exit_status = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        tab->last_exit_status = 128 + WTERMSIG(wait_status);
    }
}

const char *cd_target(TerminalTab *tab, char *arguments[],
                             int argument_count,
                             char *expanded, size_t expanded_size)
{
    const char *home = getenv("HOME");
    if (home == NULL) home = "/";

    if (argument_count == 1) return home;
    if (strcmp(arguments[1], "-") == 0) {
        if (tab->previous_cwd[0] == '\0') return NULL;
        return tab->previous_cwd;
    }
    if (arguments[1][0] == '~') {

        snprintf(expanded, expanded_size, "%s%s", home, arguments[1] + 1);
        return expanded;
    }
    return arguments[1];
}

void builtin_cd(TerminalTab *tab, char *arguments[], int argument_count)
{
    const char *target;
    char expanded_target[1024];

    target = cd_target(tab, arguments, argument_count,
                       expanded_target, sizeof(expanded_target));
    if (target == NULL) {
        add_line(tab, "terminator: cd: OLDPWD not set");
        tab->last_exit_status = 1;
        return;
    }
    if (argument_count > 1 && strcmp(arguments[1], "-") == 0)
        add_line(tab, target);

    char home_base[512];
    getcwd(home_base, sizeof(home_base));

    char new_cwd[512];
    if (chdir(tab->cwd) != 0 || chdir(target) != 0) {

        char message[MAX_INPUT + 64];
        snprintf(message, sizeof(message), "terminator: cd: %s: %s",
                 argument_count > 1 ? arguments[1] : target, strerror(errno));
        add_line(tab, message);
        tab->last_exit_status = 1;
        chdir(home_base);
        return;
    }
    getcwd(new_cwd, sizeof(new_cwd));
    chdir(home_base);

    pthread_mutex_lock(&tab->buffer_lock);
    snprintf(tab->previous_cwd, sizeof(tab->previous_cwd), "%s", tab->cwd);
    snprintf(tab->cwd, sizeof(tab->cwd), "%s", new_cwd);
    pthread_mutex_unlock(&tab->buffer_lock);
    tab->last_exit_status = 0;
}

int builtin_to_fd(TerminalTab *tab, char *arguments[],
                         int argument_count)
{
    if (strcmp(arguments[0], "cd") == 0) {
        char expanded[1024];
        const char *target = cd_target(tab, arguments, argument_count,
                                       expanded, sizeof(expanded));
        if (target == NULL) {
            dprintf(STDERR_FILENO, "terminator: cd: OLDPWD not set\n");
            return 1;
        }
        if (chdir(target) != 0) {
            dprintf(STDERR_FILENO, "terminator: cd: %s: %s\n",
                    argument_count > 1 ? arguments[1] : target,
                    strerror(errno));
            return 1;
        }
        if (argument_count > 1 && strcmp(arguments[1], "-") == 0) {
            char now[512];
            if (getcwd(now, sizeof(now)) != NULL)
                dprintf(STDOUT_FILENO, "%s\n", now);
        }
        return 0;
    }

    if (strcmp(arguments[0], "jobs") == 0) {
        jobs_print_fd(tab);
        return 0;
    }
    if (strcmp(arguments[0], "pwd") == 0) {
        dprintf(STDOUT_FILENO, "%s\n", tab->cwd);
        return 0;
    }
    if (strcmp(arguments[0], "history") == 0) {

        int total = hist_count();
        int first = total > 1000 ? total - 1000 : 0;
        for (int i = first; i < total; i++) {
            char entry[HIST_LEN];
            if (hist_get(i, entry))
                dprintf(STDOUT_FILENO, "%s\n", entry);
        }
        return 0;
    }

    if (strcmp(arguments[0], "clear") == 0) return 0;

    if (strcmp(arguments[0], "exit") == 0) return 0;

    if (strcmp(arguments[0], "bash") == 0 ||
        strcmp(arguments[0], "source") == 0) {
        int is_bash = (arguments[0][0] == 'b');
        if (argument_count < 2) {
            dprintf(STDERR_FILENO, "terminator: %s: missing script file\n",
                    arguments[0]);
            return 1;
        }
        if (is_bash) tab->var_count = 0;
        set_script_positional(tab, is_bash ? arguments[1] : NULL,
                              arguments + 2, argument_count - 2);
        tab->script_sink = 1;
        return run_script_file(tab, arguments[1], 0, is_bash ? 1 : 0);
    }
    return -1;
}

void exec_or_report(TerminalTab *tab, char *arguments[])
{
    if (strchr(arguments[0], '/') != NULL) {
        struct stat st;
        if (stat(arguments[0], &st) == 0 && S_ISREG(st.st_mode) &&
            access(arguments[0], X_OK) == 0) {
            FILE *f = fopen(arguments[0], "r");
            if (f != NULL) {
                int c1 = fgetc(f);
                int c2 = fgetc(f);
                fclose(f);
                if (c1 == '#' && c2 == '!') {
                    tab->script_sink = 1;
                    tab->var_count = 0;
                    int argc = 0;
                    while (arguments[argc] != NULL) argc++;
                    set_script_positional(tab, arguments[0],
                                          arguments + 1, argc - 1);
                    _exit(run_script_file(tab, arguments[0], 0, 1));
                }
            }
        }
    }

    execvp(arguments[0], arguments);

    if (errno == ENOENT) {
        dprintf(STDERR_FILENO, "terminator: %s: command not found\n",
                arguments[0]);
        _exit(127);
    } else if (errno == EACCES) {
        dprintf(STDERR_FILENO, "terminator: %s: Permission denied\n",
                arguments[0]);
        _exit(126);
    } else if (errno == ENOEXEC) {

        tab->script_sink = 1;
        tab->var_count = 0;
        int argc = 0;
        while (arguments[argc] != NULL) argc++;
        set_script_positional(tab, arguments[0], arguments + 1, argc - 1);
        _exit(run_script_file(tab, arguments[0], 0, 1));
    } else {
        dprintf(STDERR_FILENO, "terminator: %s: %s\n",
                arguments[0], strerror(errno));
        _exit(127);
    }
}

int launch_command_into(TerminalTab *tab, char *arguments[],
                               int argument_count, Redirection *redirection,
                               int run_builtin, pid_t *pid_out)
{
    int output_pipe[2];
    if (pipe(output_pipe) == -1) {
        add_line(tab, "Error: Cannot create output pipe.");
        return -1;
    }

    pid_t child_pid = fork();

    if (child_pid == -1) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        add_line(tab, "Error: Cannot create child process.");
        return -1;
    }

    if (child_pid == 0) {

        close(output_pipe[0]);

        setpgid(0, 0);

        dup2(output_pipe[1], STDERR_FILENO);

        if (chdir(tab->cwd) != 0) {
            dprintf(STDERR_FILENO, "terminator: cannot enter folder %s\n",
                    tab->cwd);
            _exit(1);
        }

        if (apply_redirections(redirection) != 0) _exit(1);

        if (redirection->output_file == NULL)
            dup2(output_pipe[1], STDOUT_FILENO);
        close(output_pipe[1]);

        if (run_builtin)
            _exit(builtin_to_fd(tab, arguments, argument_count));

        exec_or_report(tab, arguments);
    }

    setpgid(child_pid, child_pid);
    close(output_pipe[1]);

    int flags = fcntl(output_pipe[0], F_GETFL, 0);
    if (flags != -1) fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);

    *pid_out = child_pid;
    return output_pipe[0];
}

void launch_command(TerminalTab *tab, char *arguments[],
                           int argument_count, Redirection *redirection,
                           int run_builtin)
{
    pid_t child_pid = -1;
    int read_fd = launch_command_into(tab, arguments, argument_count,
                                      redirection, run_builtin, &child_pid);
    if (read_fd < 0) return;
    tab->output_fd = read_fd;
    tab->child_pid = child_pid;
    tab->fg_pgid = child_pid;
}

void execute_line(TerminalTab *tab, char *line)
{

    {
        const char *scan = line;
        while (*scan == ' ' || *scan == '\t') scan++;
        if (strncmp(scan, "multiWatch", 10) == 0 &&
            (scan[10] == '\0' || scan[10] == ' ' || scan[10] == '\t' ||
             scan[10] == '[')) {
            start_multiwatch(tab, scan);
            return;
        }
    }

char *globbed = expand_globs(tab, line);
char *parse_line = (globbed != NULL) ? globbed : line;
char storage[MAX_TOKEN_STORAGE];
char *arguments[MAX_ARGUMENTS];
int argument_count = parse_tokens(tab, parse_line, arguments, storage);
free(globbed);

    if (argument_count == 0) return;

    if (argument_count == 1) {
        char *equals = strchr(arguments[0], '=');
        if (equals != NULL && equals != arguments[0] &&
            is_name_start(arguments[0][0])) {
            int valid = 1;
            for (char *p = arguments[0]; p != equals; p++) {
                if (!is_name_char(*p)) { valid = 0; break; }
            }
            if (valid) {
                char name[SCRIPT_VAR_NAME];
                size_t name_length = (size_t)(equals - arguments[0]);
                if (name_length >= sizeof(name))
                    name_length = sizeof(name) - 1;
                memcpy(name, arguments[0], name_length);
                name[name_length] = '\0';
                script_var_set(tab, name, equals + 1);
                tab->last_exit_status = 0;
                return;
            }
        }
    }

    int background = 0;
    for (int i = 0; i < argument_count; i++) {
        if (strcmp(arguments[i], "&") == 0) {
            if (i != argument_count - 1) {
                add_line(tab, "terminator: syntax error near unexpected "
                              "token '&'");
                tab->last_exit_status = 2;
                return;
            }
            background = 1;
        }
    }
    if (background) {
        argument_count--;
        arguments[argument_count] = NULL;
        if (argument_count == 0) return;
    }

    int has_pipe = 0;
    for (int i = 0; i < argument_count; i++) {
        if (strcmp(arguments[i], "|") == 0) { has_pipe = 1; break; }
    }
    if (has_pipe) {
        Segment segments[MAX_SEGMENTS];
        char bad_operator[8];
        int segment_count = split_pipeline(arguments, argument_count,
                                           segments, MAX_SEGMENTS,
                                           bad_operator,
                                           sizeof(bad_operator));
        if (segment_count == -1) {
            add_line(tab, "terminator: syntax error near unexpected token '|'");
            tab->last_exit_status = 2;
            return;
        }
        if (segment_count == -2) {
            char message[MAX_INPUT + 64];
            snprintf(message, sizeof(message),
                     "terminator: syntax error: missing file after '%s'",
                     bad_operator);
            add_line(tab, message);
            tab->last_exit_status = 2;
            return;
        }
        if (segment_count == -3) {
            add_line(tab, "terminator: too many commands in one pipeline");
            tab->last_exit_status = 2;
            return;
        }
        if (!background) {

            if (tab->output_fd >= 0) {
                add_line(tab, "A command is already running in this tab.");
                return;
            }
            make_cmd_text(tab->fg_cmd, sizeof(tab->fg_cmd), line);
            launch_pipeline(tab, segments, segment_count);
            return;
        }

        if (tab->job_count >= MAX_JOBS) {
            add_line(tab, "terminator: too many background jobs");
            tab->last_exit_status = 1;
            return;
        }
        {
            pid_t job_pids[MAX_SEGMENTS];
            int job_forked = 0;
            int job_fd = launch_pipeline_into(tab, segments, segment_count,
                                              job_pids, &job_forked);
            if (job_fd >= 0) {
                char cmd_text[256];
                make_cmd_text(cmd_text, sizeof(cmd_text), line);
                Job *job = add_job(tab, job_pids, job_forked, job_fd,
                                   cmd_text, JOB_RUNNING_BG);
                if (job != NULL) print_job_start_line(tab, job);
            }
        }
        return;
    }

    Redirection redirection;
    char bad_operator[8];
    if (extract_redirections(arguments, &argument_count, &redirection,
                             bad_operator, sizeof(bad_operator)) != 0) {
        char message[MAX_INPUT + 64];
        snprintf(message, sizeof(message),
                 "terminator: syntax error: missing file after '%s'",
                 bad_operator);
        add_line(tab, message);
        tab->last_exit_status = 2;
        return;
    }
    int has_redirection = redirection.input_file != NULL ||
                          redirection.output_file != NULL ||
                          redirection.error_file != NULL;

    if (argument_count == 0) {
        if (has_redirection)
            tab->last_exit_status =
                run_redirection_setup(tab, &redirection) == 0 ? 0 : 1;
        return;
    }

    if (strcmp(arguments[0], "jobs") == 0) {
        if (has_redirection) run_redirection_setup(tab, &redirection);
        builtin_jobs(tab);
        return;
    }
    if (strcmp(arguments[0], "fg") == 0) {
        if (has_redirection) run_redirection_setup(tab, &redirection);
        builtin_fg(tab, arguments, argument_count);
        return;
    }
    if (strcmp(arguments[0], "bg") == 0) {
        if (has_redirection) run_redirection_setup(tab, &redirection);
        builtin_bg(tab, arguments, argument_count);
        return;
    }
    if (strcmp(arguments[0], "cd") == 0) {

        if (has_redirection) run_redirection_setup(tab, &redirection);
        builtin_cd(tab, arguments, argument_count);
        return;
    }
    if (strcmp(arguments[0], "exit") == 0) {

        if (has_redirection) run_redirection_setup(tab, &redirection);

        request_tab_close(tab);
        return;
    }
    if (strcmp(arguments[0], "clear") == 0) {
        if (has_redirection) run_redirection_setup(tab, &redirection);
        buffer_clear(tab);
        tab->last_exit_status = 0;
        return;
    }

    if (strcmp(arguments[0], "bash") == 0 ||
        strcmp(arguments[0], "source") == 0) {
        int is_bash = (arguments[0][0] == 'b');
        if (has_redirection) {
            if (background) {
                if (tab->job_count >= MAX_JOBS) {
                    add_line(tab, "terminator: too many background jobs");
                    tab->last_exit_status = 1;
                    return;
                }
                {
                    pid_t job_pid = -1;
                    int job_fd = launch_command_into(tab, arguments,
                                                     argument_count,
                                                     &redirection, 1,
                                                     &job_pid);
                    if (job_fd >= 0) {
                        pid_t job_pids[1];
                        char cmd_text[256];
                        job_pids[0] = job_pid;
                        make_cmd_text(cmd_text, sizeof(cmd_text), line);
                        Job *job = add_job(tab, job_pids, 1, job_fd,
                                           cmd_text, JOB_RUNNING_BG);
                        if (job != NULL) print_job_start_line(tab, job);
                    }
                }
                return;
            }
            if (tab->output_fd >= 0) {
                add_line(tab, "A command is already running in this tab.");
                return;
            }
            make_cmd_text(tab->fg_cmd, sizeof(tab->fg_cmd), line);
            launch_command(tab, arguments, argument_count, &redirection, 1);
            return;
        }
        builtin_run_script(tab, arguments, argument_count, is_bash);
        return;
    }
    if (strcmp(arguments[0], "pwd") == 0 ||
        strcmp(arguments[0], "history") == 0) {

        if (!has_redirection) {
            if (strcmp(arguments[0], "pwd") == 0) {
                add_line(tab, tab->cwd);
            } else {

                int total = hist_count();
                int first = total > 1000 ? total - 1000 : 0;
                for (int i = first; i < total; i++) {
                    char entry[HIST_LEN];
                    if (hist_get(i, entry)) add_line(tab, entry);
                }
            }
            tab->last_exit_status = 0;
            return;
        }
        if (background) {

            if (tab->job_count >= MAX_JOBS) {
                add_line(tab, "terminator: too many background jobs");
                tab->last_exit_status = 1;
                return;
            }
            {
                pid_t job_pid = -1;
                int job_fd = launch_command_into(tab, arguments,
                                                 argument_count,
                                                 &redirection, 1, &job_pid);
                if (job_fd >= 0) {
                    pid_t job_pids[1];
                    char cmd_text[256];
                    job_pids[0] = job_pid;
                    make_cmd_text(cmd_text, sizeof(cmd_text), line);
                    Job *job = add_job(tab, job_pids, 1, job_fd,
                                       cmd_text, JOB_RUNNING_BG);
                    if (job != NULL) print_job_start_line(tab, job);
                }
            }
            return;
        }
        if (tab->output_fd >= 0) {
            add_line(tab, "A command is already running in this tab.");
            return;
        }
        make_cmd_text(tab->fg_cmd, sizeof(tab->fg_cmd), line);
        launch_command(tab, arguments, argument_count, &redirection, 1);
        return;
    }

    if (background) {

        if (tab->job_count >= MAX_JOBS) {
            add_line(tab, "terminator: too many background jobs");
            tab->last_exit_status = 1;
            return;
        }
        {
            pid_t job_pid = -1;
            int job_fd = launch_command_into(tab, arguments,
                                             argument_count,
                                             &redirection, 0, &job_pid);
            if (job_fd >= 0) {
                pid_t job_pids[1];
                char cmd_text[256];
                job_pids[0] = job_pid;
                make_cmd_text(cmd_text, sizeof(cmd_text), line);
                Job *job = add_job(tab, job_pids, 1, job_fd,
                                   cmd_text, JOB_RUNNING_BG);
                if (job != NULL) print_job_start_line(tab, job);
            }
        }
        return;
    }
    if (tab->output_fd >= 0) {
        add_line(tab, "A command is already running in this tab.");
        return;
    }

    make_cmd_text(tab->fg_cmd, sizeof(tab->fg_cmd), line);

    launch_command(tab, arguments, argument_count, &redirection, 0);
}

void read_command_output(TerminalTab *tab)
{
    char output[4096];
    ssize_t bytes_read = read(tab->output_fd, output, sizeof(output) - 1);

    if (bytes_read > 0) {
        tab->gui_dirty = 1;

        output[bytes_read] = '\0';
        add_output(tab, output);
    } else if (bytes_read == 0) {
        close(tab->output_fd);
        tab->output_fd = -1;

    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        close(tab->output_fd);
        tab->output_fd = -1;
        add_line(tab, "Error while reading command output.");
    }

    if (tab->child_pid > 0) {
        int status;
        pid_t result = waitpid(tab->child_pid, &status, WNOHANG);
        if (result == tab->child_pid) {
            save_exit_status(tab, status);
            tab->child_pid = -1;
        }
    }

    reap_pipeline(tab);
}
