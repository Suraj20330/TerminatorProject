#include "terminator.h"

const char *script_lookup(TerminalTab *tab, const char *name)
{
    for (int i = 0; i < tab->var_count; i++) {
        if (strcmp(tab->var_names[i], name) == 0)
            return tab->var_values[i];
    }
    return getenv(name);
}

void script_var_set(TerminalTab *tab, const char *name,
                           const char *value)
{
    for (int i = 0; i < tab->var_count; i++) {
        if (strcmp(tab->var_names[i], name) == 0) {
            snprintf(tab->var_values[i], SCRIPT_VAR_VALUE, "%s", value);
            return;
        }
    }
    if (tab->var_count >= MAX_SCRIPT_VARS) return;
    snprintf(tab->var_names[tab->var_count], SCRIPT_VAR_NAME, "%s", name);
    snprintf(tab->var_values[tab->var_count], SCRIPT_VAR_VALUE, "%s",
             value);
    tab->var_count++;
}

void set_script_positional(TerminalTab *tab, const char *zero,
                                  char *args[], int arg_count)
{
    if (zero != NULL)
        snprintf(tab->pos_args[0], MAX_INPUT, "%s", zero);
    for (int i = 0; i < 9; i++) tab->pos_args[i + 1][0] = '\0';
    int stored = arg_count < 9 ? arg_count : 9;
    for (int i = 0; i < stored; i++)
        snprintf(tab->pos_args[i + 1], MAX_INPUT, "%s", args[i]);
    tab->pos_count = arg_count;
}

void script_strip_comment(char *line)
{
    char quote = '\0';
    for (int i = 0; line[i] != '\0'; i++) {
        char c = line[i];
        if (quote == '\'') {
            if (c == '\'') quote = '\0';
            continue;
        }
        if (quote == '"') {
            if (c == '"') quote = '\0';
            else if (c == '\\' && line[i + 1] != '\0') i++;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '\\' && line[i + 1] != '\0') { i++; continue; }
        if (c == '#' &&
            (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t' ||
             line[i - 1] == ';')) {
            line[i] = '\0';
            return;
        }
    }
}

void script_trim(char *line)
{
    int start = 0;
    while (line[start] == ' ' || line[start] == '\t') start++;
    if (start > 0)
        memmove(line, line + start, strlen(line + start) + 1);
    size_t length = strlen(line);
    while (length > 0 &&
           (line[length - 1] == ' ' || line[length - 1] == '\t'))
        line[--length] = '\0';
}

void script_first_word(const char *line, char *word, size_t size,
                              int *quoted)
{
    *quoted = 0;
    word[0] = '\0';
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    char quote = '\0';
    size_t length = 0;
    while (*p != '\0') {
        if (quote != '\0') {
            if (*p == quote) quote = '\0';
            else if (length + 1 < size) word[length++] = *p;
            p++;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
            *quoted = 1;
            p++;
        } else if (*p == '\\' && p[1] != '\0') {
            if (length + 1 < size) word[length++] = p[1];
            p += 2;
        } else if (*p == ' ' || *p == '\t') {
            break;
        } else {
            if (length + 1 < size) word[length++] = *p;
            p++;
        }
    }
    word[length] = '\0';
}

int script_words(const char *text, char words[][128], int max_words)
{
    int count = 0;
    char quote = '\0';
    char current[128];
    int length = 0;
    int have = 0;
    for (const char *p = text; ; p++) {
        char c = *p;
        if (quote == '\'') {
            if (c == '\0') break;
            if (c == '\'') quote = '\0';
            else if (length < 127) current[length++] = c;
            have = 1;
            continue;
        }
        if (quote == '"') {
            if (c == '\0') break;
            if (c == '"') quote = '\0';
            else if (c == '\\' && p[1] != '\0') {
                if (length < 127) current[length++] = p[1];
                p++;
            } else if (length < 127) current[length++] = c;
            have = 1;
            continue;
        }
        if (c == '\0' || c == ' ' || c == '\t') {
            if (have && count < max_words) {
                current[length] = '\0';
                strcpy(words[count], current);
                count++;
            }
            length = 0;
            have = 0;
            if (c == '\0') break;
            continue;
        }
        if (c == ';') {
            if (have && count < max_words) {
                current[length] = '\0';
                strcpy(words[count], current);
                count++;
            }
            length = 0;
            have = 0;
            if (count < max_words) {
                strcpy(words[count], ";");
                count++;
            }
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; have = 1; continue; }
        if (c == '\\' && p[1] != '\0') {
            if (length < 127) current[length++] = p[1];
            p++;
            have = 1;
            continue;
        }
        if (length < 127) current[length++] = c;
        have = 1;
    }
    return count;
}

void script_condition_text(const char *line, char *out, size_t size)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    while (*p != '\0' && *p != ' ' && *p != '\t') p++;
    char quote = '\0';
    const char *end = p + strlen(p);
    for (const char *q = p; *q != '\0'; q++) {
        char c = *q;
        if (quote == '\'') {
            if (c == '\'') quote = '\0';
            continue;
        }
        if (quote == '"') {
            if (c == '"') quote = '\0';
            else if (c == '\\' && q[1] != '\0') q++;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '\\' && q[1] != '\0') { q++; continue; }
        if (c == ';') { end = q; break; }
    }
    size_t length = (size_t)(end - p);
    if (length >= size) length = size - 1;
    memcpy(out, p, length);
    out[length] = '\0';
    script_trim(out);
}

int script_ends_with_then(const char *line)
{
    size_t length = strlen(line);
    if (length < 4 || strcmp(line + length - 4, "then") != 0) return 0;
    size_t i = length - 4;
    while (i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t')) i--;
    return i > 0 && line[i - 1] == ';';
}

int eval_test(TerminalTab *tab, char *tokens[], int count)
{
    if (count < 2 || strcmp(tokens[0], "[") != 0 ||
        strcmp(tokens[count - 1], "]") != 0) {
        add_line(tab, "terminator: [: missing ]");
        return 0;
    }
    char **t = tokens + 1;
    int n = count - 2;
    int negate = 0;
    if (n > 0 && strcmp(t[0], "!") == 0) { negate = 1; t++; n--; }
    int result;
    if (n == 0) {
        result = 0;
    } else if (n == 1) {
        result = (t[0][0] != '\0');
    } else if (n == 2) {
        if (strcmp(t[0], "-z") == 0) {
            result = (t[1][0] == '\0');
        } else if (strcmp(t[0], "-n") == 0) {
            result = (t[1][0] != '\0');
        } else if (strcmp(t[0], "-e") == 0 ||
                   strcmp(t[0], "-f") == 0 ||
                   strcmp(t[0], "-d") == 0) {

            char full_path[MAX_INPUT + 512];
            if (t[1][0] == '/') {
                snprintf(full_path, sizeof(full_path), "%s", t[1]);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s",
                         tab->cwd, t[1]);
            }
            if (strcmp(t[0], "-e") == 0) {
                result = (access(full_path, F_OK) == 0);
            } else {
                struct stat st;
                result = (stat(full_path, &st) == 0 &&
                          (strcmp(t[0], "-f") == 0
                               ? S_ISREG(st.st_mode)
                               : S_ISDIR(st.st_mode)));
            }
        } else {
            char message[MAX_INPUT + 64];
            snprintf(message, sizeof(message),
                     "terminator: [: %s: unary operator expected", t[0]);
            add_line(tab, message);
            result = 0;
        }
    } else if (n == 3) {
        if (strcmp(t[1], "=") == 0 || strcmp(t[1], "==") == 0) {
            result = (strcmp(t[0], t[2]) == 0);
        } else if (strcmp(t[1], "!=") == 0) {
            result = (strcmp(t[0], t[2]) != 0);
        } else if (strcmp(t[1], "-eq") == 0 || strcmp(t[1], "-ne") == 0 ||
                   strcmp(t[1], "-lt") == 0 || strcmp(t[1], "-le") == 0 ||
                   strcmp(t[1], "-gt") == 0 || strcmp(t[1], "-ge") == 0) {
            char *end1 = NULL;
            char *end2 = NULL;
            long a = strtol(t[0], &end1, 10);
            long b = strtol(t[2], &end2, 10);
            if (end1 == NULL || *end1 != '\0' ||
                end2 == NULL || *end2 != '\0') {
                const char *bad =
                    (end1 != NULL && *end1 != '\0') ? t[0] : t[2];
                char message[MAX_INPUT + 64];
                snprintf(message, sizeof(message),
                         "terminator: [: %s: integer expression expected",
                         bad);
                add_line(tab, message);
                result = 0;
            } else if (strcmp(t[1], "-eq") == 0) {
                result = (a == b);
            } else if (strcmp(t[1], "-ne") == 0) {
                result = (a != b);
            } else if (strcmp(t[1], "-lt") == 0) {
                result = (a < b);
            } else if (strcmp(t[1], "-le") == 0) {
                result = (a <= b);
            } else if (strcmp(t[1], "-gt") == 0) {
                result = (a > b);
            } else {
                result = (a >= b);
            }
        } else {
            char message[MAX_INPUT + 64];
            snprintf(message, sizeof(message),
                     "terminator: [: %s: binary operator expected", t[1]);
            add_line(tab, message);
            result = 0;
        }
    } else {
        add_line(tab, "terminator: [: too many arguments");
        result = 0;
    }
    return negate ? !result : result;
}

void deferred_push(TerminalTab *tab, const char *text)
{
    if (tab->def_count >= SCRIPT_DEFERRED_MAX) {
        add_line(tab, "terminator: typed-ahead line dropped (too many)");
        return;
    }
    snprintf(tab->deferred[tab->def_tail], MAX_INPUT, "%s", text);
    tab->def_tail = (tab->def_tail + 1) % SCRIPT_DEFERRED_MAX;
    tab->def_count++;
}

int deferred_pop(TerminalTab *tab, char *out)
{
    if (tab->def_count <= 0) return 0;
    snprintf(out, MAX_INPUT, "%s", tab->deferred[tab->def_head]);
    tab->def_head = (tab->def_head + 1) % SCRIPT_DEFERRED_MAX;
    tab->def_count--;
    return 1;
}

void drain_deferred_lines(TerminalTab *tab)
{
    char line[MAX_INPUT];
    while (!tab->want_close && deferred_pop(tab, line)) {
        handle_enter(tab, line);
        proto_send_state(tab);
    }
}

void wait_foreground(TerminalTab *tab, int watch_messages,
                            int *aborted, int *quit_seen)
{
    *aborted = 0;
    *quit_seen = 0;

    char acc[4096];
    int acc_length = 0;
    if (watch_messages && g_am_worker_process &&
        tab->script_proto_left_len > 0) {
        acc_length = tab->script_proto_left_len;
        if (acc_length > (int)sizeof(acc)) acc_length = (int)sizeof(acc);
        memcpy(acc, tab->script_proto_left, (size_t)acc_length);
        tab->script_proto_left_len = 0;
    }

    while (tab->output_fd >= 0 || tab->child_pid > 0 ||
           tab->pipe_count > 0) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = -1;
        if (tab->output_fd >= 0) {
            FD_SET(tab->output_fd, &read_fds);
            if (tab->output_fd > max_fd) max_fd = tab->output_fd;
        }
        if (watch_messages && tab->wake_r >= 0) {
            FD_SET(tab->wake_r, &read_fds);
            if (tab->wake_r > max_fd) max_fd = tab->wake_r;
        }
        if (watch_messages && g_am_worker_process) {
            FD_SET(PROTO_IN_FD, &read_fds);
            if (PROTO_IN_FD > max_fd) max_fd = PROTO_IN_FD;
        }
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready > 0 && tab->output_fd >= 0 &&
            FD_ISSET(tab->output_fd, &read_fds))
            read_command_output(tab);
        reap_pipeline(tab);
        if (tab->child_pid > 0) {
            int status;
            pid_t result = waitpid(tab->child_pid, &status, WNOHANG);
            if (result == tab->child_pid) {
                save_exit_status(tab, status);
                tab->child_pid = -1;
            }
        }

        if (watch_messages && tab->wake_r >= 0 &&
            FD_ISSET(tab->wake_r, &read_fds)) {
            char drain[512];
            ssize_t ignored = read(tab->wake_r, drain, sizeof(drain));
            (void)ignored;
            TabMsg message;
            while (queue_pop(tab, &message)) {
                if (message.type == MSG_LINE) {
                    deferred_push(tab, message.text);
                } else if (message.type == MSG_INTERRUPT) {
                    engine_interrupt(tab);
                    *aborted = 1;
                } else if (message.type == MSG_SUSPEND) {
                    suspend_foreground(tab);
                    *aborted = 1;
                } else if (message.type == MSG_QUIT) {
                    tab->want_close = 1;
                    *quit_seen = 1;
                    return;
                }
            }
        }

        if (watch_messages && g_am_worker_process &&
            FD_ISSET(PROTO_IN_FD, &read_fds)) {
            ssize_t n = read(PROTO_IN_FD, acc + acc_length,
                             sizeof(acc) - (size_t)acc_length);
            if (n == 0) {

                tab->want_close = 1;
                *quit_seen = 1;
                return;
            } else if (n > 0) {
                acc_length += (int)n;
                int used = 0;
                while (acc_length - used >= 1) {
                    char type = acc[used];
                    if (type == 'L') {
                        if (acc_length - used < 3) break;
                        int length = (unsigned char)acc[used + 1] |
                                     ((unsigned char)acc[used + 2] << 8);
                        if (length > MAX_INPUT - 1)
                            length = MAX_INPUT - 1;
                        if (acc_length - used < 3 + length) break;
                        char line[MAX_INPUT];
                        memcpy(line, acc + used + 3, (size_t)length);
                        line[length] = '\0';
                        used += 3 + length;
                        deferred_push(tab, line);
                    } else if (type == 'C') {
                        used += 1;
                        engine_interrupt(tab);
                        *aborted = 1;
                    } else if (type == 'Z') {
                        used += 1;
                        suspend_foreground(tab);
                        *aborted = 1;
                    } else if (type == 'Q') {
                        used += 1;
                        tab->want_close = 1;
                        *quit_seen = 1;
                    } else {
                        used += 1;
                    }
                    if (*quit_seen) break;
                }
                memmove(acc, acc + used, (size_t)(acc_length - used));
                acc_length -= used;
                if (*quit_seen) return;
            }

        }
    }

    if (watch_messages && g_am_worker_process && acc_length > 0 &&
        acc_length <= (int)sizeof(tab->script_proto_left)) {
        memcpy(tab->script_proto_left, acc, (size_t)acc_length);
        tab->script_proto_left_len = acc_length;
    }
}

int script_read_file(TerminalTab *tab, const char *path,
                            char ***out_lines, int *out_count)
{

    char full_path[MAX_INPUT + 512];
    if (path[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", path);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", tab->cwd, path);
    }
    struct stat st;
    if (stat(full_path, &st) != 0) return -1;
    if (!S_ISREG(st.st_mode)) { errno = EISDIR; return -1; }
    FILE *f = fopen(full_path, "r");
    if (f == NULL) return -1;

    char **lines = NULL;
    int count = 0;
    int capacity = 0;
    char *raw = NULL;
    size_t raw_capacity = 0;
    ssize_t raw_length;
    char logical[MAX_PENDING];
    logical[0] = '\0';
    int result = 0;

    while ((raw_length = getline(&raw, &raw_capacity, f)) >= 0) {
        while (raw_length > 0 &&
               (raw[raw_length - 1] == '\n' ||
                raw[raw_length - 1] == '\r'))
            raw[--raw_length] = '\0';
        if (logical[0] == '\0') {
            snprintf(logical, sizeof(logical), "%s", raw);
        } else if (ends_with_continuation_backslash(logical)) {
            int quoted = ends_with_quoted_continuation_backslash(logical);
            size_t length = strlen(logical);
            logical[length - 1] = '\0';
            if (quoted)
                strncat(logical, "\n",
                        sizeof(logical) - strlen(logical) - 1);
            strncat(logical, raw, sizeof(logical) - strlen(logical) - 1);
        } else {
            strncat(logical, "\n", sizeof(logical) - strlen(logical) - 1);
            strncat(logical, raw, sizeof(logical) - strlen(logical) - 1);
        }
        if (line_is_complete(logical)) {
            script_strip_comment(logical);
            script_trim(logical);
            if (logical[0] != '\0') {
                if (count >= MAX_SCRIPT_LINES) { result = -2; break; }
                if (count >= capacity) {
                    capacity = capacity == 0 ? 64 : capacity * 2;
                    char **grown = realloc(lines,
                                           sizeof(char *) *
                                           (size_t)capacity);
                    if (grown == NULL) {
                        result = -1;
                        errno = ENOMEM;
                        break;
                    }
                    lines = grown;
                }
                lines[count] = strdup(logical);
                if (lines[count] == NULL) {
                    result = -1;
                    errno = ENOMEM;
                    break;
                }
                count++;
            }
            logical[0] = '\0';
        }
    }
    if (result == 0 && ferror(f)) result = -1;
    if (result == 0 && logical[0] != '\0') result = -3;
    free(raw);
    fclose(f);
    if (result != 0) {
        for (int i = 0; i < count; i++) free(lines[i]);
        free(lines);
        return result;
    }
    *out_lines = lines;
    *out_count = count;
    return 0;
}

int script_validate_if_line(char **lines, int count, int index,
                                   char *detail, size_t detail_size)
{
    char words[64][128];
    int n = script_words(lines[index], words, 64);
    if (n < 2 || (strcmp(words[0], "if") != 0 &&
                  strcmp(words[0], "elif") != 0)) {
        snprintf(detail, detail_size, "not an if line");
        return 0;
    }
    if (strcmp(words[1], "[") != 0) {
        snprintf(detail, detail_size, "if without [ ... ] test");
        return 0;
    }
    if (strcmp(words[n - 1], "then") == 0) {
        if (n < 5 || strcmp(words[n - 2], ";") != 0 ||
            strcmp(words[n - 3], "]") != 0) {
            snprintf(detail, detail_size, "if without then");
            return 0;
        }
        return 1;
    }
    if (strcmp(words[n - 1], ";") == 0) {

        if (n < 2 || strcmp(words[n - 2], "]") != 0) {
            snprintf(detail, detail_size, "if without then");
            return 0;
        }
    } else if (strcmp(words[n - 1], "]") != 0) {
        snprintf(detail, detail_size, "if without then");
        return 0;
    }
    if (index + 1 >= count || strcmp(lines[index + 1], "then") != 0) {
        snprintf(detail, detail_size, "if without then");
        return 0;
    }
    return 1;
}

int script_validate(TerminalTab *tab, char **lines, int count,
                           const char *path)
{
    int else_seen[128];
    int depth = 0;
    for (int i = 0; i < count; i++) {
        char word[128];
        int quoted = 0;
        script_first_word(lines[i], word, sizeof(word), &quoted);
        if (quoted) continue;
        const char *reason = NULL;
        char detail[128];
        detail[0] = '\0';
        if (strcmp(word, "if") == 0) {
            if (depth >= 128) {
                reason = "if nesting too deep";
            } else if (!script_validate_if_line(lines, count, i, detail,
                                                sizeof(detail))) {
                reason = detail;
            } else {
                else_seen[depth] = 0;
                depth++;
            }
        } else if (strcmp(word, "elif") == 0) {
            if (depth == 0) {
                reason = "elif without if";
            } else if (else_seen[depth - 1]) {
                reason = "elif after else";
            } else if (!script_validate_if_line(lines, count, i, detail,
                                                sizeof(detail))) {
                reason = detail;
            }
        } else if (strcmp(word, "else") == 0) {
            if (depth == 0) {
                reason = "else without if";
            } else if (else_seen[depth - 1]) {
                reason = "else after else";
            } else if (strcmp(lines[i], "else") != 0) {
                reason = "unexpected text after else";
            } else {
                else_seen[depth - 1] = 1;
            }
        } else if (strcmp(word, "fi") == 0) {
            if (depth == 0) {
                reason = "unexpected fi";
            } else if (strcmp(lines[i], "fi") != 0) {
                reason = "unexpected text after fi";
            } else {
                depth--;
            }
        }
        if (reason != NULL) {
            char message[MAX_INPUT + 256];
            snprintf(message, sizeof(message),
                     "terminator: %s: syntax error: %s", path, reason);
            add_line(tab, message);
            return 2;
        }
    }
    if (depth != 0) {
        char message[MAX_INPUT + 256];
        snprintf(message, sizeof(message),
                 "terminator: %s: syntax error: missing fi", path);
        add_line(tab, message);
        return 2;
    }
    return 0;
}

int script_next_boundary(char **lines, int count, int from,
                                char *kind)
{
    int depth = 1;
    for (int j = from; j < count; j++) {
        char word[128];
        int quoted = 0;
        script_first_word(lines[j], word, sizeof(word), &quoted);
        if (quoted) continue;
        if (strcmp(word, "if") == 0) {
            depth++;
        } else if (strcmp(word, "fi") == 0) {
            depth--;
            if (depth == 0) { *kind = 'f'; return j; }
        } else if (depth == 1 && strcmp(word, "elif") == 0) {
            *kind = 'e';
            return j;
        } else if (depth == 1 && strcmp(word, "else") == 0) {
            *kind = 's';
            return j;
        }
    }
    return -1;
}

int script_eval_condition(TerminalTab *tab, const char *line)
{
    char *cond_text = malloc(MAX_PENDING);
    if (cond_text == NULL) return 0;
    script_condition_text(line, cond_text, MAX_PENDING);
    int result = 0;
    char *expanded = expand_substitutions(tab, cond_text);
    free(cond_text);
    if (expanded != NULL) {
        char *storage = malloc(MAX_TOKEN_STORAGE);
        if (storage != NULL) {
            char *args[MAX_ARGUMENTS];
            int argc = parse_tokens(tab, expanded, args, storage);
            if (argc > 0) result = eval_test(tab, args, argc);
            free(storage);
        }
        free(expanded);
    }
    return result;
}

int script_exec_if(TerminalTab *tab, char **lines, int count,
                          int if_index, ScriptCtx *ctx, int watch_messages)
{
    int cursor = if_index;
    int fi_index = -1;
    while (1) {
        int then_on_line = script_ends_with_then(lines[cursor]);
        int body_start = then_on_line ? cursor + 1 : cursor + 2;
        char kind = 0;
        int boundary = script_next_boundary(lines, count, body_start,
                                            &kind);
        if (boundary < 0) {
            add_line(tab, "terminator: script error: missing fi");
            tab->last_exit_status = 2;
            return count;
        }
        if (kind == 'f') fi_index = boundary;
        if (script_eval_condition(tab, lines[cursor])) {
            script_exec_range(tab, lines, body_start, boundary, ctx,
                              watch_messages);
            break;
        }
        if (kind == 'f') {
            tab->last_exit_status = 0;
            break;
        }
        if (kind == 's') {
            char kind2 = 0;
            int fi2 = script_next_boundary(lines, count, boundary + 1,
                                           &kind2);
            if (fi2 < 0) {
                add_line(tab, "terminator: script error: missing fi");
                tab->last_exit_status = 2;
                return count;
            }
            fi_index = fi2;
            script_exec_range(tab, lines, boundary + 1, fi2, ctx,
                              watch_messages);
            break;
        }
        cursor = boundary;
    }
    return fi_index < 0 ? count : fi_index;
}

void script_exec_range(TerminalTab *tab, char **lines, int start,
                              int end, ScriptCtx *ctx, int watch_messages)
{
    for (int i = start; i < end && !ctx->stop; i++) {
        char word[128];
        int quoted = 0;
        script_first_word(lines[i], word, sizeof(word), &quoted);
        if (!quoted && strcmp(word, "if") == 0) {
            i = script_exec_if(tab, lines, end, i, ctx, watch_messages);
            continue;
        }
        if (!quoted && (strcmp(word, "elif") == 0 ||
                        strcmp(word, "else") == 0 ||
                        strcmp(word, "fi") == 0)) {
            add_line(tab, "terminator: script error: unexpected "
                          "elif/else/fi");
            tab->last_exit_status = 2;
            continue;
        }
        if (!quoted && strcmp(word, "exit") == 0) {
            char *storage = malloc(MAX_TOKEN_STORAGE);
            int bare = 1;
            int code = tab->last_exit_status;
            int too_many = 0;
            int bad_arg = 0;
            char bad_text[MAX_INPUT];
            bad_text[0] = '\0';
            if (storage != NULL) {
                char *args[MAX_ARGUMENTS];
                int argc = parse_tokens(tab, lines[i], args, storage);
                for (int k = 0; k < argc; k++) {
                    if (strcmp(args[k], "|") == 0 ||
                        strcmp(args[k], "<") == 0 ||
                        strcmp(args[k], ">") == 0 ||
                        strcmp(args[k], ">>") == 0 ||
                        strcmp(args[k], "2>") == 0) {
                        bare = 0;
                        break;
                    }
                }
                if (bare && argc > 2) {
                    too_many = 1;
                } else if (bare && argc == 2) {
                    char *endp = NULL;
                    long n = strtol(args[1], &endp, 10);
                    if (endp == NULL || *endp != '\0') {
                        bad_arg = 1;
                        snprintf(bad_text, sizeof(bad_text), "%s",
                                 args[1]);
                    } else {
                        code = (int)(((n % 256) + 256) % 256);
                    }
                }
                free(storage);
            }
            if (!bare) {

            } else if (too_many) {
                add_line(tab, "terminator: exit: too many arguments");
                tab->last_exit_status = 1;
                continue;
            } else if (bad_arg) {
                char message[MAX_INPUT + 64];
                snprintf(message, sizeof(message),
                         "terminator: exit: %s: numeric argument required",
                         bad_text);
                add_line(tab, message);
                tab->last_exit_status = 255;
                ctx->stop = 1;
                return;
            } else {
                tab->last_exit_status = code;
                ctx->stop = 1;
                return;
            }
        }

        char *expanded = expand_substitutions(tab, lines[i]);
        if (expanded != NULL) {
            execute_line(tab, expanded);
            free(expanded);
        }
        if (tab->output_fd >= 0 || tab->child_pid > 0 ||
            tab->pipe_count > 0) {
            int aborted = 0;
            int quit_seen = 0;
            wait_foreground(tab, watch_messages, &aborted, &quit_seen);
            if (aborted || quit_seen) ctx->stop = 1;
        }
        if (tab->want_close) ctx->stop = 1;
    }
}

int run_script_file(TerminalTab *tab, const char *path,
                           int watch_messages, int reset_status)
{
    if (g_script_depth >= MAX_SCRIPT_DEPTH) {
        add_line(tab, "terminator: script nesting too deep");
        return 1;
    }
    char **lines = NULL;
    int count = 0;
    int read_result = script_read_file(tab, path, &lines, &count);
    if (read_result == -1) {
        char message[MAX_INPUT + 256];
        snprintf(message, sizeof(message), "terminator: %s: %s", path,
                 strerror(errno));
        add_line(tab, message);
        return 1;
    }
    if (read_result == -2) {
        char message[MAX_INPUT + 256];
        snprintf(message, sizeof(message),
                 "terminator: %s: too many lines (max %d)", path,
                 MAX_SCRIPT_LINES);
        add_line(tab, message);
        return 1;
    }
    if (read_result == -3) {
        char message[MAX_INPUT + 256];
        snprintf(message, sizeof(message),
                 "terminator: %s: syntax error: unexpected end of file",
                 path);
        add_line(tab, message);
        return 2;
    }
    if (script_validate(tab, lines, count, path) != 0) {
        for (int i = 0; i < count; i++) free(lines[i]);
        free(lines);
        return 2;
    }
    if (reset_status) tab->last_exit_status = 0;
    g_script_depth++;
    ScriptCtx ctx;
    ctx.stop = 0;
    script_exec_range(tab, lines, 0, count, &ctx, watch_messages);
    g_script_depth--;
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
    return tab->last_exit_status;
}

void builtin_run_script(TerminalTab *tab, char *arguments[],
                               int argument_count, int is_bash)
{
    if (argument_count < 2) {
        add_line(tab, is_bash ? "terminator: bash: missing script file"
                              : "terminator: source: missing script file");
        tab->last_exit_status = 1;
        return;
    }
    const char *path = arguments[1];
    char saved_cwd[512];
    char saved_prev[512];
    char (*saved_names)[SCRIPT_VAR_NAME] = NULL;
    char (*saved_values)[SCRIPT_VAR_VALUE] = NULL;
    int saved_var_count = 0;
    char saved_pos[10][MAX_INPUT];
    int saved_pos_count = tab->pos_count;
    memcpy(saved_pos, tab->pos_args, sizeof(saved_pos));
    if (is_bash) {
        saved_names = malloc(sizeof(tab->var_names));
        saved_values = malloc(sizeof(tab->var_values));
        if (saved_names != NULL && saved_values != NULL) {
            memcpy(saved_names, tab->var_names, sizeof(tab->var_names));
            memcpy(saved_values, tab->var_values,
                   sizeof(tab->var_values));
            saved_var_count = tab->var_count;
            tab->var_count = 0;
        } else {
            free(saved_names);
            free(saved_values);
            saved_names = NULL;
            saved_values = NULL;
        }
        snprintf(saved_cwd, sizeof(saved_cwd), "%s", tab->cwd);
        snprintf(saved_prev, sizeof(saved_prev), "%s", tab->previous_cwd);
    }
    set_script_positional(tab, is_bash ? path : NULL, arguments + 2,
                          argument_count - 2);
    int status = run_script_file(tab, path, 1, is_bash ? 1 : 0);
    memcpy(tab->pos_args, saved_pos, sizeof(saved_pos));
    tab->pos_count = saved_pos_count;
    if (is_bash) {
        if (saved_names != NULL && saved_values != NULL) {
            memcpy(tab->var_names, saved_names, sizeof(tab->var_names));
            memcpy(tab->var_values, saved_values,
                   sizeof(tab->var_values));
            tab->var_count = saved_var_count;
            free(saved_names);
            free(saved_values);
        }
        snprintf(tab->cwd, sizeof(tab->cwd), "%s", saved_cwd);
        snprintf(tab->previous_cwd, sizeof(tab->previous_cwd), "%s",
                 saved_prev);
    }
    tab->last_exit_status = status;
}
