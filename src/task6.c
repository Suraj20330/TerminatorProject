#include "terminator.h"

void mw_report_usage(TerminalTab *tab)
{
    add_line(tab, "terminator: multiWatch: usage: multiWatch "
                  "[\"cmd1\", \"cmd2\", ...]");
    tab->last_exit_status = 2;
}

int mw_parse_commands(const char *text,
                             char commands[][MAX_INPUT],
                             int max_commands)
{
    const char *p = text;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return -1;
    p++;

    int count = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (*p == ']') { p++; break; }
        if (*p == '\0') return -1;
        if (*p != '"' && *p != '\'') return -1;

        char quote = *p++;
        const char *start = p;
        while (*p != '\0' && *p != quote) p++;
        if (*p == '\0') return -1;
        if (count >= max_commands) return -2;

        size_t length = (size_t)(p - start);
        if (length > MAX_INPUT - 1) length = MAX_INPUT - 1;
        memcpy(commands[count], start, length);
        commands[count][length] = '\0';
        count++;
        p++;
    }
    (void)p;
    return count;
}

void mw_cycle_append(Watch *watch, const char *line)
{
    size_t length = strlen(line);
    size_t needed = watch->cycle_len + length + 2;
    if (needed > watch->cycle_cap) {
        size_t cap = watch->cycle_cap == 0 ? 256 : watch->cycle_cap;
        while (cap < needed) cap *= 2;
        char *grown = realloc(watch->cycle_out, cap);
        if (grown == NULL) return;
        watch->cycle_out = grown;
        watch->cycle_cap = cap;
    }
    memcpy(watch->cycle_out + watch->cycle_len, line, length);
    watch->cycle_len += length;
    watch->cycle_out[watch->cycle_len] = '\n';
    watch->cycle_len++;
    watch->cycle_out[watch->cycle_len] = '\0';
}

void mw_print_frame(TerminalTab *tab, Watch *watch)
{
    char header[MAX_INPUT + 64];
    snprintf(header, sizeof(header), "\"%s\", %ld:",
             watch->cmd, (long)watch->launched_at);
    add_line(tab, header);
    add_line(tab, MW_SEPARATOR_LINE);
    if (watch->cycle_out != NULL) {
        char *p = watch->cycle_out;
        while (*p != '\0') {
            char *nl = strchr(p, '\n');
            if (nl != NULL) *nl = '\0';
            add_line(tab, p);
            if (nl == NULL) break;
            p = nl + 1;
        }
        free(watch->cycle_out);
        watch->cycle_out = NULL;
    }
    watch->cycle_len = 0;
    watch->cycle_cap = 0;
    add_line(tab, MW_SEPARATOR_LINE);
}

void mw_try_flush(TerminalTab *tab)
{
    if (!tab->watch_active || tab->watch_count <= 0) return;
    if (tab->watch_cursor >= tab->watch_count) tab->watch_cursor = 0;
    for (;;) {
        Watch *watch = &tab->watches[tab->watch_cursor];
        if (!watch->frame_ready) return;
        mw_print_frame(tab, watch);
        watch->frame_ready = 0;
        tab->watch_cursor++;
        if (tab->watch_cursor >= tab->watch_count) tab->watch_cursor = 0;
    }
}

void launch_watch_cycle(TerminalTab *tab, Watch *watch)
{
    watch->launched_at = time(NULL);
    free(watch->cycle_out);
    watch->cycle_out = NULL;
    watch->cycle_len = 0;
    watch->cycle_cap = 0;
    watch->frame_ready = 0;


    char line_copy[MAX_INPUT];
    snprintf(line_copy, sizeof(line_copy), "%s", watch->cmd);
    char storage[MAX_TOKEN_STORAGE];
    char *arguments[MAX_ARGUMENTS];
    int argument_count = parse_tokens(tab, line_copy, arguments, storage);

    Segment segments[MAX_SEGMENTS];
    char bad_operator[8];
    int segment_count = -1;
    if (argument_count > 0) {
        segment_count = split_pipeline(arguments, argument_count,
                                       segments, MAX_SEGMENTS,
                                       bad_operator, sizeof(bad_operator));
        if (segment_count > 0) {

            for (int i = 0; i < segment_count; i++)
                if (segments[i].argc == 0) segment_count = -1;
        }
    }

    pid_t pids[MAX_SEGMENTS];
    int forked = 0;
    int read_fd = -1;
    if (segment_count > 0)
        read_fd = launch_pipeline_into(tab, segments, segment_count,
                                       pids, &forked);

    if (read_fd < 0) {
        watch->frame_ready = 1;
        mw_try_flush(tab);
        watch->state = 0;
        watch->next_run = time(NULL) + MW_RERUN_SECONDS;
        return;
    }

    for (int i = 0; i < forked; i++) watch->pids[i] = pids[i];
    watch->pid_count = forked;
    watch->read_fd = read_fd;
    watch->partial_len = 0;
    watch->partial[0] = '\0';
    watch->state = 1;
}

void watch_sweep(Watch *watch)
{
    if (watch->state != 1 || watch->read_fd >= 0) return;

    int still_alive = 0;
    for (int i = 0; i < watch->pid_count; i++) {
        if (watch->pids[i] <= 0) continue;
        if (waitpid(watch->pids[i], NULL, WNOHANG) == watch->pids[i])
            watch->pids[i] = -1;
        else
            still_alive = 1;
    }
    if (!still_alive) {
        watch->pid_count = 0;
        watch->state = 0;
        watch->next_run = time(NULL) + MW_RERUN_SECONDS;
    }
}

void drain_watch(TerminalTab *tab, Watch *watch)
{
    if (watch->read_fd < 0) return;

    char buffer[4096];
    ssize_t bytes_read = read(watch->read_fd, buffer, sizeof(buffer));

    if (bytes_read > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buffer[i];
if (c == '\n') {
    watch->partial[watch->partial_len] = '\0';
    mw_cycle_append(watch, watch->partial);
    watch->partial_len = 0;

            } else if (watch->partial_len < (int)sizeof(watch->partial) - 1) {
                watch->partial[watch->partial_len] = c;
                watch->partial_len++;
            }

        }
        return;
    }

    if (bytes_read < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return;

if (watch->partial_len > 0) {
    watch->partial[watch->partial_len] = '\0';
    mw_cycle_append(watch, watch->partial);
    watch->partial_len = 0;
}
watch->frame_ready = 1;
mw_try_flush(tab);
close(watch->read_fd);

    watch->read_fd = -1;
    watch_sweep(watch);
}

int service_multiwatch_tab(TerminalTab *tab)
{

    int launched = 0;
    time_t now = time(NULL);
    if (!tab->watch_active) return 0;
    for (int w = 0; w < tab->watch_count; w++) {
        Watch *watch = &tab->watches[w];
        watch_sweep(watch);
        if (watch->state == 0 && !watch->frame_ready &&
            now >= watch->next_run) {
            launch_watch_cycle(tab, watch);
            launched++;
        }
    }
    return launched;
}

void stop_multiwatch(TerminalTab *tab)
{
    if (!tab->watch_active) return;

    for (int w = 0; w < tab->watch_count; w++) {
        for (int i = 0; i < tab->watches[w].pid_count; i++) {
            if (tab->watches[w].pids[i] > 0)
                kill(tab->watches[w].pids[i], SIGINT);
        }
    }

    for (int w = 0; w < tab->watch_count; w++) {
        Watch *watch = &tab->watches[w];
        for (int i = 0; i < watch->pid_count; i++) {
            if (watch->pids[i] <= 0) continue;
            if (waitpid(watch->pids[i], NULL, WNOHANG) != watch->pids[i]) {
                kill(watch->pids[i], SIGKILL);
                waitpid(watch->pids[i], NULL, 0);
            }
            watch->pids[i] = -1;
        }
        watch->pid_count = 0;
        if (watch->read_fd >= 0) {
            close(watch->read_fd);
            watch->read_fd = -1;
        }
    watch->state = 0;
    watch->partial_len = 0;
    free(watch->cycle_out);
    watch->cycle_out = NULL;
    watch->cycle_len = 0;
    watch->cycle_cap = 0;
    watch->launched_at = 0;
    watch->frame_ready = 0;
}
tab->watch_count = 0;

    tab->watch_active = 0;
    tab->watch_cursor = 0;
}

void start_multiwatch(TerminalTab *tab, const char *line)
{
    if (tab->watch_active) {
        add_line(tab, "terminator: multiWatch: already running in this "
                      "tab (Ctrl+C to stop it)");
        tab->last_exit_status = 1;
        return;
    }

    char commands[MAX_WATCHES][MAX_INPUT];
    int count = mw_parse_commands(line + 10,
                                  commands, MAX_WATCHES);
    if (count == -2) {
        add_line(tab, "terminator: multiWatch: too many commands "
                      "(maximum 8)");
        tab->last_exit_status = 2;
        return;
    }
    if (count <= 0) {
        mw_report_usage(tab);
        return;
    }

    tab->watch_count = 0;
    for (int i = 0; i < count; i++) {
        Watch *watch = &tab->watches[i];
        char *expanded = expand_substitutions(tab, commands[i]);
        snprintf(watch->cmd, sizeof(watch->cmd), "%s",
                 expanded != NULL ? expanded : commands[i]);
        free(expanded);
        watch->pid_count = 0;
        watch->read_fd = -1;
        watch->partial_len = 0;
    watch->partial[0] = '\0';
    watch->state = 0;
    watch->next_run = 0;
    watch->cycle_out = NULL;
    watch->cycle_len = 0;
    watch->cycle_cap = 0;
    watch->launched_at = 0;
    watch->frame_ready = 0;
}

    tab->watch_count = count;
    tab->watch_active = 1;
    tab->watch_cursor = 0;
    tab->last_exit_status = 0;

    for (int i = 0; i < count; i++)
        launch_watch_cycle(tab, &tab->watches[i]);
}
