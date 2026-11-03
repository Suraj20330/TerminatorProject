#include "terminator.h"

int status_to_code(int wait_status)
{
    if (WIFEXITED(wait_status)) return WEXITSTATUS(wait_status);
    if (WIFSIGNALED(wait_status)) return 128 + WTERMSIG(wait_status);
    return 1;
}

int foreground_running(TerminalTab *tab)
{
    return tab->child_pid > 0 || tab->pipe_count > 0;
}

void signal_group_or_each(pid_t pgid, pid_t pids[], int pid_count,
                                 int signal_number)
{
    if (pgid > 0 && kill(-pgid, signal_number) == 0) return;
    for (int i = 0; i < pid_count; i++)
        if (pids[i] > 0) kill(pids[i], signal_number);
}

void signal_foreground(TerminalTab *tab, int signal_number)
{
    if (tab->pipe_count > 0) {
        signal_group_or_each(tab->fg_pgid, tab->pipe_pids,
                             tab->pipe_count, signal_number);
    } else if (tab->child_pid > 0) {
        pid_t only = tab->child_pid;
        signal_group_or_each(tab->fg_pgid, &only, 1, signal_number);
    }
}

void signal_job(Job *job, int signal_number)
{
    signal_group_or_each(job->pgid, job->pids, job->pid_count,
                         signal_number);
}

void make_cmd_text(char *destination, size_t size, const char *line)
{
    snprintf(destination, size, "%s", line);
    size_t length = strlen(destination);
    while (length > 0 &&
           (destination[length - 1] == ' ' || destination[length - 1] == '\t'))
        destination[--length] = '\0';
    if (length > 0 && destination[length - 1] == '&') {
        destination[--length] = '\0';
        while (length > 0 &&
               (destination[length - 1] == ' ' ||
                destination[length - 1] == '\t'))
            destination[--length] = '\0';
    }
}

Job *add_job(TerminalTab *tab, pid_t pids[], int pid_count,
                    int read_fd, const char *cmd_text, int state)
{
    if (tab->job_count >= MAX_JOBS) return NULL;

    int id = 1;
    for (;;) {
        int used = 0;
        for (int i = 0; i < tab->job_count; i++)
            if (tab->jobs[i].id == id) { used = 1; break; }
        if (!used) break;
        id++;
    }

    Job *job = &tab->jobs[tab->job_count];
    memset(job, 0, sizeof(*job));
    job->id = id;
    job->pid_count = pid_count;
    for (int i = 0; i < pid_count; i++) job->pids[i] = pids[i];
    job->last_pid = pids[pid_count - 1];
    job->pgid = pids[0];
    job->read_fd = read_fd;
    job->partial_len = 0;
    snprintf(job->cmd, sizeof(job->cmd), "%s", cmd_text);
    job->state = state;
    job->last_status_known = 0;
    job->last_status = 0;
    job->pids_reaped = 0;
    tab->job_count++;
    return job;
}

void remove_job(TerminalTab *tab, int index)
{
    for (int i = index; i + 1 < tab->job_count; i++)
        tab->jobs[i] = tab->jobs[i + 1];
    tab->job_count--;
}

void print_job_start_line(TerminalTab *tab, Job *job)
{
    char line_text[64];
    snprintf(line_text, sizeof(line_text), "[%d] %d",
             job->id, (int)job->last_pid);
    add_line(tab, line_text);
}

void format_job_line(TerminalTab *tab, int index,
                            char *out, size_t size)
{
    Job *job = &tab->jobs[index];
    char marker = (index == tab->job_count - 1) ? '+' :
                  (index == tab->job_count - 2) ? '-' : ' ';
    const char *state_word =
        job->state == JOB_STOPPED ? "Stopped" : "Running";
    snprintf(out, size, "[%d]%c  %-24s%s%s", job->id, marker, state_word,
             job->cmd, job->state == JOB_RUNNING_BG ? " &" : "");
}

void jobs_print_fd(TerminalTab *tab)
{
    for (int i = 0; i < tab->job_count; i++) {
        char line_text[512];
        format_job_line(tab, i, line_text, sizeof(line_text));
        dprintf(STDOUT_FILENO, "%s\n", line_text);
    }
}

void builtin_jobs(TerminalTab *tab)
{
    for (int i = 0; i < tab->job_count; i++) {
        char line_text[512];
        format_job_line(tab, i, line_text, sizeof(line_text));
        add_line(tab, line_text);
    }
    tab->last_exit_status = 0;
}

int job_target_index(TerminalTab *tab, char *arguments[],
                            int argument_count, const char *builtin)
{
    char message[512];

    if (tab->job_count == 0) {
        snprintf(message, sizeof(message), "terminator: %s: no current job",
                 builtin);
        add_line(tab, message);
        tab->last_exit_status = 1;
        return -1;
    }
    if (argument_count < 2) return tab->job_count - 1;

    const char *digits = arguments[1];
    if (*digits == '%') digits++;
    int id = 0;
    int well_formed = *digits != '\0';
    for (const char *p = digits; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') { well_formed = 0; break; }
        id = id * 10 + (*p - '0');
    }
    if (well_formed) {
        for (int i = 0; i < tab->job_count; i++)
            if (tab->jobs[i].id == id) return i;
    }
    snprintf(message, sizeof(message), "terminator: %s: %s%s: no such job",
             builtin, arguments[1][0] == '%' ? "" : "%", arguments[1]);
    add_line(tab, message);
    tab->last_exit_status = 1;
    return -1;
}

void builtin_bg(TerminalTab *tab, char *arguments[],
                       int argument_count)
{
    int index = job_target_index(tab, arguments, argument_count, "bg");
    if (index < 0) return;
    Job *job = &tab->jobs[index];

    if (job->state == JOB_RUNNING_BG) {
        char message[512];
        snprintf(message, sizeof(message),
                 "terminator: bg: job %d already in background", job->id);
        add_line(tab, message);
        tab->last_exit_status = 1;
        return;
    }

    signal_job(job, SIGCONT);
    job->state = JOB_RUNNING_BG;

    char marker = (index == tab->job_count - 1) ? '+' :
                  (index == tab->job_count - 2) ? '-' : ' ';
    char line_text[320];
    snprintf(line_text, sizeof(line_text), "[%d]%c %s &",
             job->id, marker, job->cmd);
    add_line(tab, line_text);
    tab->last_exit_status = 0;
}

void builtin_fg(TerminalTab *tab, char *arguments[],
                       int argument_count)
{
    if (foreground_running(tab)) {
        add_line(tab, "terminator: fg: another command is already "
                      "running in this tab");
        tab->last_exit_status = 1;
        return;
    }
    int index = job_target_index(tab, arguments, argument_count, "fg");
    if (index < 0) return;
    Job *job = &tab->jobs[index];

    add_line(tab, job->cmd);
    signal_job(job, SIGCONT);

    if (job->pids_reaped >= job->pid_count) {

        if (job->last_status_known)
            tab->last_exit_status = job->last_status;
        tab->output_fd = job->read_fd;
        tab->child_pid = -1;
        tab->pipe_count = 0;
        tab->last_pid = -1;
        tab->fg_pgid = -1;
        snprintf(tab->fg_cmd, sizeof(tab->fg_cmd), "%s", job->cmd);
        remove_job(tab, index);
        return;
    }

    tab->output_fd = job->read_fd;
    tab->child_pid = -1;
    tab->pipe_count = job->pid_count;
    for (int i = 0; i < job->pid_count; i++)
        tab->pipe_pids[i] = job->pids[i];
    tab->fg_pgid = job->pgid;
    snprintf(tab->fg_cmd, sizeof(tab->fg_cmd), "%s", job->cmd);
    if (job->last_status_known) {

        tab->fg_preset_valid = 1;
        tab->fg_preset_code = job->last_status;
        tab->last_pid = -1;
    } else {
        tab->last_pid = job->last_pid;
    }
    remove_job(tab, index);
}

void suspend_foreground(TerminalTab *tab)
{
    if (!foreground_running(tab)) return;
    if (tab->job_count >= MAX_JOBS) {
        add_line(tab, "terminator: too many background jobs");
        return;
    }

    signal_foreground(tab, SIGTSTP);

    pid_t pids[MAX_SEGMENTS];
    int pid_count;
    if (tab->pipe_count > 0) {
        pid_count = tab->pipe_count;
        for (int i = 0; i < pid_count; i++) pids[i] = tab->pipe_pids[i];
    } else {
        pid_count = 1;
        pids[0] = tab->child_pid;
    }
    Job *job = add_job(tab, pids, pid_count, tab->output_fd,
                       tab->fg_cmd, JOB_STOPPED);
    if (job == NULL) return;
    if (tab->fg_pgid > 0) job->pgid = tab->fg_pgid;
    if (tab->pipe_count > 0) job->last_pid = tab->last_pid;
    int reaped = 0;
    for (int i = 0; i < pid_count; i++)
        if (pids[i] <= 0) reaped++;
    job->pids_reaped = reaped;
    if (tab->pipe_count > 0 && tab->last_pid == -1) {
        job->last_status_known = 1;
        job->last_status = tab->last_exit_status;
    }

    tab->output_fd = -1;
    tab->child_pid = -1;
    tab->pipe_count = 0;
    tab->last_pid = -1;
    tab->fg_pgid = -1;
    tab->fg_cmd[0] = '\0';

    char notice[320];
    snprintf(notice, sizeof(notice), "[%d]+  %-24s%s",
             job->id, "Stopped", job->cmd);
    add_line(tab, notice);
}

void drain_job(TerminalTab *tab, Job *job)
{
    if (job->read_fd < 0) return;

    char buffer[4096];
    ssize_t bytes_read = read(job->read_fd, buffer, sizeof(buffer));

    if (bytes_read > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buffer[i];
            if (c == '\n') {
                job->partial[job->partial_len] = '\0';
                add_line(tab, job->partial);
                job->partial_len = 0;
            } else if (job->partial_len < (int)sizeof(job->partial) - 1) {
                job->partial[job->partial_len] = c;
                job->partial_len++;
            }
        }
        return;
    }

    if (bytes_read < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return;

    if (job->partial_len > 0) {
        job->partial[job->partial_len] = '\0';
        add_line(tab, job->partial);
        job->partial_len = 0;
    }
    close(job->read_fd);
    job->read_fd = -1;
}

int service_jobs_tab(TerminalTab *tab)
{

    int changed = 0;
    for (int j = 0; j < tab->job_count; j++) {
        Job *job = &tab->jobs[j];

        for (int i = 0; i < job->pid_count; i++) {
            if (job->pids[i] <= 0) continue;
            int status;
            pid_t result = waitpid(job->pids[i], &status,
                                   WNOHANG | WUNTRACED);
            if (result != job->pids[i]) continue;
            if (WIFSTOPPED(status)) {
                if (job->state == JOB_RUNNING_BG) {
                    job->state = JOB_STOPPED;
                    changed = 1;
                }

            } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                if (job->pids[i] == job->last_pid) {
                    job->last_status_known = 1;
                    job->last_status = status_to_code(status);
                }
                job->pids[i] = -1;
                job->pids_reaped++;
            }
        }

        if (job->read_fd < 0 && job->pids_reaped >= job->pid_count) {
            char marker = (j == tab->job_count - 1) ? '+' :
                          (j == tab->job_count - 2) ? '-' : ' ';
            char notice[320];
            snprintf(notice, sizeof(notice), "[%d]%c  %-24s%s",
                     job->id, marker, "Done", job->cmd);
            add_line(tab, notice);
            remove_job(tab, j);
            j--;
            changed = 1;
        }
    }
    return changed;
}

void kill_all_jobs(TerminalTab *tab)
{
    for (int j = 0; j < tab->job_count; j++) {
        Job *job = &tab->jobs[j];
        signal_job(job, SIGKILL);
        for (int i = 0; i < job->pid_count; i++) {
            if (job->pids[i] > 0) {
                waitpid(job->pids[i], NULL, 0);
                job->pids[i] = -1;
            }
        }
        if (job->read_fd >= 0) {
            close(job->read_fd);
            job->read_fd = -1;
        }
    }
    tab->job_count = 0;
}
