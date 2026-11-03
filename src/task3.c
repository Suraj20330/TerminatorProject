#include "terminator.h"

void tok_add_char(char **out, const char *limit, char c)
{
    if (*out < limit) {
        **out = c;
        (*out)++;
    }
}

void tok_add_text(char **out, const char *limit, const char *text)
{
    if (text == NULL) return;
    while (*text != '\0') {
        tok_add_char(out, limit, *text);
        text++;
    }
}

int is_name_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

int is_name_char(char c)
{
    return is_name_start(c) || (c >= '0' && c <= '9');
}

void expand_dollar(TerminalTab *tab, const char *line, int *pos,
                          char **out, const char *limit)
{
    int i = *pos;
    char next = line[i + 1];

    if (next == '?') {
        char number[16];
        snprintf(number, sizeof(number), "%d", tab->last_exit_status);
        tok_add_text(out, limit, number);
        *pos = i + 2;
        return;
    }

    if (next == '#') {
        char number[16];
        snprintf(number, sizeof(number), "%d", tab->pos_count);
        tok_add_text(out, limit, number);
        *pos = i + 2;
        return;
    }
    if (next >= '0' && next <= '9') {
        tok_add_text(out, limit, tab->pos_args[next - '0']);
        *pos = i + 2;
        return;
    }

    if (next == '{') {
        char name[128];
        int length = 0;
        int j = i + 2;
        while (line[j] != '\0' && line[j] != '}' &&
               length < (int)sizeof(name) - 1) {
            name[length++] = line[j++];
        }
        name[length] = '\0';
        if (line[j] == '}') {
            if (strcmp(name, "?") == 0) {
                char number[16];
                snprintf(number, sizeof(number), "%d", tab->last_exit_status);
                tok_add_text(out, limit, number);
            } else if (strcmp(name, "#") == 0) {

                char number[16];
                snprintf(number, sizeof(number), "%d", tab->pos_count);
                tok_add_text(out, limit, number);
            } else if (name[0] >= '0' && name[0] <= '9') {

                int all_digits = 1;
                for (int k = 0; name[k] != '\0'; k++)
                    if (name[k] < '0' || name[k] > '9') all_digits = 0;
                if (all_digits && atoi(name) < 10)
                    tok_add_text(out, limit, tab->pos_args[atoi(name)]);
            } else {

                tok_add_text(out, limit, script_lookup(tab, name));
            }
            *pos = j + 1;
            return;
        }

        tok_add_char(out, limit, '$');
        *pos = i + 1;
        return;
    }

    if (is_name_start(next)) {
        char name[128];
        int length = 0;
        int j = i + 1;
        while (is_name_char(line[j]) && length < (int)sizeof(name) - 1) {
            name[length++] = line[j++];
        }
        name[length] = '\0';

        tok_add_text(out, limit, script_lookup(tab, name));
        *pos = j;
        return;
    }

    tok_add_char(out, limit, '$');
    *pos = i + 1;
}

int parse_tokens(TerminalTab *tab, char *line, char *arguments[], char *storage)
{
    enum { NORMAL, SINGLE, DOUBLE } state = NORMAL;
    char *out = storage;
    const char *limit = storage + MAX_TOKEN_STORAGE - 1;
    char *token_start = storage;
    int token_started = 0;
    int argument_count = 0;

    for (int i = 0; line[i] != '\0'; ) {
        char c = line[i];

        if (state == SINGLE) {

            if (c == '\'') { state = NORMAL; i++; }
            else { tok_add_char(&out, limit, c); i++; }
        }
        else if (state == DOUBLE) {

            if (c == '"') { state = NORMAL; i++; }
            else if (c == '\\' && (line[i + 1] == '$' || line[i + 1] == '`' ||
                                   line[i + 1] == '"' || line[i + 1] == '\\')) {

                tok_add_char(&out, limit, line[i + 1]);
                i += 2;
            }
            else if (c == '\\' && line[i + 1] == '\n') {
                i += 2;
            }
            else if (c == '$') { expand_dollar(tab, line, &i, &out, limit); }
            else { tok_add_char(&out, limit, c); i++; }
        }
        else {
            if (c == ' ' || c == '\t' || c == '\n') {

                if (token_started) {
                    tok_add_char(&out, limit, '\0');
                    if (argument_count < MAX_ARGUMENTS - 1)
                        arguments[argument_count++] = token_start;
                    token_started = 0;
                }
                i++;
            }
            else if (c == '\'') {
                if (!token_started) { token_started = 1; token_start = out; }
                state = SINGLE;
                i++;
            }
            else if (c == '"') {
                if (!token_started) { token_started = 1; token_start = out; }
                state = DOUBLE;
                i++;
            }
            else if (c == '\\') {
                if (!token_started) { token_started = 1; token_start = out; }
                if (line[i + 1] == '\n') { i += 2; }

                else if (line[i + 1] != '\0') {
                    tok_add_char(&out, limit, line[i + 1]);
                    i += 2;
                }
                else { i++; }
            }
            else if (c == '$') {
                if (!token_started) { token_started = 1; token_start = out; }
                expand_dollar(tab, line, &i, &out, limit);
            }
            else if (c == '&') {

                if (token_started) {
                    tok_add_char(&out, limit, '\0');
                    if (argument_count < MAX_ARGUMENTS - 1)
                        arguments[argument_count++] = token_start;
                    token_started = 0;
                }
                token_start = out;
                tok_add_char(&out, limit, '&');
                tok_add_char(&out, limit, '\0');
                if (argument_count < MAX_ARGUMENTS - 1)
                    arguments[argument_count++] = token_start;
                i++;
            }
            else if (c == '|') {

                if (token_started) {
                    tok_add_char(&out, limit, '\0');
                    if (argument_count < MAX_ARGUMENTS - 1)
                        arguments[argument_count++] = token_start;
                    token_started = 0;
                }
                token_start = out;
                tok_add_char(&out, limit, '|');
                tok_add_char(&out, limit, '\0');
                if (argument_count < MAX_ARGUMENTS - 1)
                    arguments[argument_count++] = token_start;
                i++;
            }
            else {
                if (!token_started) { token_started = 1; token_start = out; }
                tok_add_char(&out, limit, c);
                i++;
            }
        }
    }

    if (token_started) {
        tok_add_char(&out, limit, '\0');
        if (argument_count < MAX_ARGUMENTS - 1)
            arguments[argument_count++] = token_start;
    }

    storage[MAX_TOKEN_STORAGE - 1] = '\0';

    arguments[argument_count] = NULL;
    return argument_count;
}

void scan_line(const char *text, int *is_complete, int *trailing_backslash,
                      int *backslash_in_quotes)
{
    int state = SCAN_NORMAL;
    int before_backtick = SCAN_NORMAL;
    int paren_depth = 0;
    int escape_at_end = 0;

    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        escape_at_end = 0;

        if (state == SCAN_SINGLE) {

            if (c == '\'') state = SCAN_NORMAL;
        }
        else if (state == SCAN_BACKTICK) {
            if (c == '\\' && (text[i + 1] == '`' || text[i + 1] == '$' ||
                              text[i + 1] == '\\')) {
                i++;
            }
            else if (c == '`') state = before_backtick;
        }
        else if (state == SCAN_DOUBLE) {
            if (c == '\\' && (text[i + 1] == '$' || text[i + 1] == '`' ||
                              text[i + 1] == '"' || text[i + 1] == '\\')) {
                i++;
            }
            else if (c == '\\' && text[i + 1] == '\0') {
                escape_at_end = 1;
            }
            else if (c == '"') state = SCAN_NORMAL;
            else if (c == '`') { before_backtick = SCAN_DOUBLE; state = SCAN_BACKTICK; }
            else if (c == '$' && text[i + 1] == '(') { paren_depth++; i++; }
            else if (c == ')' && paren_depth > 0) paren_depth--;
        }
        else {
            if (c == '\\') {
                if (text[i + 1] == '\0') escape_at_end = 1;
                else i++;
            }
            else if (c == '\'') state = SCAN_SINGLE;
            else if (c == '"') state = SCAN_DOUBLE;
            else if (c == '`') { before_backtick = SCAN_NORMAL; state = SCAN_BACKTICK; }
            else if (c == '$' && text[i + 1] == '(') { paren_depth++; i++; }
            else if (c == ')' && paren_depth > 0) paren_depth--;
        }
    }

    *trailing_backslash = escape_at_end;

    if (backslash_in_quotes != NULL)
        *backslash_in_quotes = (escape_at_end && state == SCAN_DOUBLE);
    *is_complete = (state == SCAN_NORMAL && paren_depth == 0 && !escape_at_end);
}

int line_is_complete(const char *text)
{
    int complete, backslash;
    scan_line(text, &complete, &backslash, NULL);
    return complete;
}

int ends_with_continuation_backslash(const char *text)
{
    int complete, backslash;
    scan_line(text, &complete, &backslash, NULL);
    (void)complete;
    return backslash;
}

int ends_with_quoted_continuation_backslash(const char *text)
{
    int complete, backslash, in_quotes;
    scan_line(text, &complete, &backslash, &in_quotes);
    (void)complete; (void)backslash;
    return in_quotes;
}

void sb_init(StringBuilder *sb)
{
    sb->capacity = 256;
    sb->length = 0;
    sb->text = malloc(sb->capacity + 1);
    if (sb->text != NULL) sb->text[0] = '\0';
}

void sb_add_char(StringBuilder *sb, char c)
{
    if (sb->text == NULL) return;
    if (sb->length + 1 > sb->capacity) {
        size_t bigger = sb->capacity * 2;
        char *grown = realloc(sb->text, bigger + 1);
        if (grown == NULL) return;
        sb->text = grown;
        sb->capacity = bigger;
    }
    sb->text[sb->length++] = c;
    sb->text[sb->length] = '\0';
}

void sb_add_text(StringBuilder *sb, const char *text)
{
    if (text == NULL) return;
    while (*text != '\0') {
        sb_add_char(sb, *text);
        text++;
    }
}

void sb_add_bytes(StringBuilder *sb, const char *data, size_t count)
{
    for (size_t k = 0; k < count; k++) sb_add_char(sb, data[k]);
}

char *copy_range(const char *text, int start, int end)
{
    int length = end - start;
    char *copy = malloc((size_t)length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, text + start, (size_t)length);
    copy[length] = '\0';
    return copy;
}

int find_matching_paren(const char *text, int open_index)
{
    int depth = 0;

    for (int i = open_index; text[i] != '\0'; i++) {
        char c = text[i];

        if (c == '\\') {
            if (text[i + 1] != '\0') i++;
            continue;
        }
        if (c == '\'') {
            i++;
            while (text[i] != '\0' && text[i] != '\'') i++;
            if (text[i] == '\0') return -1;
            continue;
        }
        if (c == '"') {
            i++;
            while (text[i] != '\0' && text[i] != '"') {
                if (text[i] == '\\' && text[i + 1] != '\0') i++;
                i++;
            }
            if (text[i] == '\0') return -1;
            continue;
        }
        if (c == '`') {
            i++;
            while (text[i] != '\0' && text[i] != '`') {
                if (text[i] == '\\' && text[i + 1] != '\0') i++;
                i++;
            }
            if (text[i] == '\0') return -1;
            continue;
        }
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

char *run_and_capture(TerminalTab *tab, const char *line)
{
    StringBuilder out_sb, err_sb;
    sb_init(&out_sb);
    sb_init(&err_sb);

    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) == -1 || pipe(err_pipe) == -1) {
        return out_sb.text;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return out_sb.text;
    }

    if (pid == 0) {

        close(out_pipe[0]);
        close(err_pipe[0]);

        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);

        if (chdir(tab->cwd) != 0) {
            dprintf(STDERR_FILENO, "terminator: cannot enter folder %s\n",
                    tab->cwd);
            _exit(1);
        }

char line_copy[MAX_PENDING];
snprintf(line_copy, sizeof(line_copy), "%s", line);
char *globbed = expand_globs(tab, line_copy);
char *parse_line = (globbed != NULL) ? globbed : line_copy;
char storage[MAX_TOKEN_STORAGE];
char *arguments[MAX_ARGUMENTS];
int argument_count = parse_tokens(tab, parse_line, arguments, storage);
free(globbed);


        if (argument_count == 0) _exit(0);
        Segment segments[MAX_SEGMENTS];
        char bad_operator[8];
        int segment_count = split_pipeline(arguments, argument_count,
                                           segments, MAX_SEGMENTS,
                                           bad_operator,
                                           sizeof(bad_operator));
        if (segment_count == -1) {
            dprintf(STDERR_FILENO,
                    "terminator: syntax error near unexpected token '|'\n");
            _exit(2);
        }
        if (segment_count == -2) {
            dprintf(STDERR_FILENO,
                    "terminator: syntax error: missing file after '%s'\n",
                    bad_operator);
            _exit(2);
        }
        if (segment_count == -3) {
            dprintf(STDERR_FILENO,
                    "terminator: too many commands in one pipeline\n");
            _exit(2);
        }

        if (segment_count > 1)
            run_pipeline_sync(tab, segments, segment_count);

        Redirection *redirection = &segments[0].redir;
        char **seg_arguments = segments[0].argv;
        int seg_argument_count = segments[0].argc;
        if (apply_redirections(redirection) != 0) _exit(1);
        if (seg_argument_count == 0) _exit(0);

        int builtin_status = builtin_to_fd(tab, seg_arguments,
                                           seg_argument_count);
        if (builtin_status != -1) _exit(builtin_status);

        exec_or_report(tab, seg_arguments);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);

    int out_open = 1, err_open = 1;
    while (out_open || err_open) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = -1;
        if (out_open) {
            FD_SET(out_pipe[0], &read_fds);
            if (out_pipe[0] > max_fd) max_fd = out_pipe[0];
        }
        if (err_open) {
            FD_SET(err_pipe[0], &read_fds);
            if (err_pipe[0] > max_fd) max_fd = err_pipe[0];
        }

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (out_open && FD_ISSET(out_pipe[0], &read_fds)) {
            char chunk[4096];
            ssize_t n = read(out_pipe[0], chunk, sizeof(chunk));
            if (n > 0) sb_add_bytes(&out_sb, chunk, (size_t)n);
            else if (n == 0) { close(out_pipe[0]); out_open = 0; }
            else if (errno != EINTR) { close(out_pipe[0]); out_open = 0; }
        }
        if (err_open && FD_ISSET(err_pipe[0], &read_fds)) {
            char chunk[4096];
            ssize_t n = read(err_pipe[0], chunk, sizeof(chunk));
            if (n > 0) sb_add_bytes(&err_sb, chunk, (size_t)n);
            else if (n == 0) { close(err_pipe[0]); err_open = 0; }
            else if (errno != EINTR) { close(err_pipe[0]); err_open = 0; }
        }
    }

    int status;
    waitpid(pid, &status, 0);

    if (err_sb.length > 0) add_output(tab, err_sb.text);
    free(err_sb.text);

    if (out_sb.text == NULL) {
        out_sb.text = malloc(1);
        if (out_sb.text != NULL) out_sb.text[0] = '\0';
    }
    return out_sb.text;
}

char *expand_one_pass(TerminalTab *tab, const char *line, int *changed)
{
    StringBuilder result;
    sb_init(&result);
    *changed = 0;
    int in_single = 0;
    int in_double = 0;

    for (int i = 0; line[i] != '\0'; ) {
        char c = line[i];

        if (in_single) {

            sb_add_char(&result, c);
            if (c == '\'') in_single = 0;
            i++;
        }
        else if (c == '\'' && !in_double) {
            in_single = 1;
            sb_add_char(&result, c);
            i++;
        }
        else if (c == '"') {
            in_double = !in_double;
            sb_add_char(&result, c);
            i++;
        }
        else if (c == '\\' && line[i + 1] != '\0') {

            sb_add_char(&result, c);
            sb_add_char(&result, line[i + 1]);
            i += 2;
        }
        else if (c == '$' && line[i + 1] == '(') {
            int close = find_matching_paren(line, i + 1);
            if (close == -1) {
                sb_add_text(&result, line + i);
                break;
            }
            char *inner = copy_range(line, i + 2, close);
            char *inner_expanded = (inner != NULL)
                                   ? expand_substitutions(tab, inner) : NULL;
            free(inner);
            char *output = (inner_expanded != NULL)
                           ? run_and_capture(tab, inner_expanded) : NULL;
            free(inner_expanded);
            if (output != NULL) {

                size_t length = strlen(output);
                while (length > 0 && output[length - 1] == '\n')
                    output[--length] = '\0';
                sb_add_text(&result, output);
                free(output);
            }
            *changed = 1;
            i = close + 1;
        }
        else if (c == '`') {

            int j = i + 1;
            while (line[j] != '\0') {
                if (line[j] == '\\' && line[j + 1] != '\0') { j += 2; continue; }
                if (line[j] == '`') break;
                j++;
            }
            if (line[j] == '\0') {
                sb_add_text(&result, line + i);
                break;
            }
            char *inner = copy_range(line, i + 1, j);
            char *inner_expanded = (inner != NULL)
                                   ? expand_substitutions(tab, inner) : NULL;
            free(inner);
            char *output = (inner_expanded != NULL)
                           ? run_and_capture(tab, inner_expanded) : NULL;
            free(inner_expanded);
            if (output != NULL) {
                size_t length = strlen(output);
                while (length > 0 && output[length - 1] == '\n')
                    output[--length] = '\0';
                sb_add_text(&result, output);
                free(output);
            }
            *changed = 1;
            i = j + 1;
        }
        else {
            sb_add_char(&result, c);
            i++;
        }
    }

    if (result.text == NULL) {
        result.text = malloc(1);
        if (result.text != NULL) result.text[0] = '\0';
    }
    return result.text;
}

char *expand_substitutions(TerminalTab *tab, const char *line)
{
    char *current = copy_range(line, 0, (int)strlen(line));
    if (current == NULL) return NULL;

    for (int pass = 0; pass < 10; pass++) {
        int changed = 0;
        char *next = expand_one_pass(tab, current, &changed);
        free(current);
        current = next;
        if (!changed) break;
    }
    return current;
}

void pending_append(TerminalTab *tab, const char *text)
{
    size_t used = strlen(tab->pending_line);
    if (used < MAX_PENDING - 1)
        snprintf(tab->pending_line + used, MAX_PENDING - used, "%s", text);
}

void handle_enter(TerminalTab *tab, const char *segment_text)
{

    char segment[MAX_INPUT];
    snprintf(segment, sizeof(segment), "%s", segment_text);

    if (segment[0] == '\0' && !tab->continuing) return;

    if (tab->continuing) {
        char shown[MAX_INPUT + 4];
        snprintf(shown, sizeof(shown), "> %s", segment);
        add_line(tab, shown);
    } else {
        add_line(tab, segment);
    }

    if (tab->pending_line[0] == '\0') {
        snprintf(tab->pending_line, MAX_PENDING, "%s", segment);
    } else if (ends_with_continuation_backslash(tab->pending_line)) {

        int quoted = ends_with_quoted_continuation_backslash(tab->pending_line);
        size_t length = strlen(tab->pending_line);
        tab->pending_line[length - 1] = '\0';
        if (quoted)
            pending_append(tab, "\n");
        pending_append(tab, segment);
    } else {
        pending_append(tab, "\n");
        pending_append(tab, segment);
    }

    if (!line_is_complete(tab->pending_line)) {
        tab->continuing = 1;
        return;
    }
    tab->continuing = 0;

    if (tab->pending_line[0] != '\0') hist_add(tab->pending_line);

    char full_line[MAX_PENDING];
    snprintf(full_line, sizeof(full_line), "%s", tab->pending_line);
    tab->pending_line[0] = '\0';

    if (full_line[0] == '\0') return;

    char *expanded = expand_substitutions(tab, full_line);
    if (expanded != NULL) {
        execute_line(tab, expanded);
        free(expanded);
    }
}
static int cmp_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int word_is_redirect(const char *word, size_t length)
{
    if (length == 1 && (word[0] == '<' || word[0] == '>')) return 1;
    if (length == 2 && word[0] == '>' && word[1] == '>') return 1;
    if (length >= 2 && word[length - 1] == '>') {
        for (size_t i = 0; i + 1 < length; i++)
            if (!isdigit((unsigned char)word[i])) return 0;
        return 1;
    }
    return 0;
}

static void append_expanded(TerminalTab *tab, const char *word,
                            StringBuilder *result)
{
    const char *slash = strrchr(word, '/');
    const char *base = (slash != NULL) ? slash + 1 : word;
    char dirpart[4096];
    if (slash != NULL) {
        size_t dlen = (size_t)(slash - word);
        if (dlen >= sizeof(dirpart)) { sb_add_text(result, word); return; }
        memcpy(dirpart, word, dlen);
        dirpart[dlen] = '\0';
    }
    if (strpbrk(base, "*?[") == NULL) { sb_add_text(result, word); return; }
    if (tab->cwd[0] == '\0') { sb_add_text(result, word); return; }

    char search[4608];
    if (slash == NULL) {
        snprintf(search, sizeof(search), "%s", tab->cwd);
    } else if (dirpart[0] == '/') {
        snprintf(search, sizeof(search), "%s", dirpart);
    } else {
        snprintf(search, sizeof(search), "%s/%s", tab->cwd, dirpart);
    }

    DIR *d = opendir(search);
    if (d == NULL) { sb_add_text(result, word); return; }
    char **names = NULL;
    size_t count = 0, capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        if (entry->d_name[0] == '.' && base[0] != '.') continue;
        if (fnmatch(base, entry->d_name, 0) != 0) continue;
        if (count == capacity) {
            size_t bigger = capacity == 0 ? 16 : capacity * 2;
            char **grown = realloc(names, bigger * sizeof(*grown));
            if (grown == NULL) break;
            names = grown;
            capacity = bigger;
        }
        names[count] = strdup(entry->d_name);
        if (names[count] == NULL) break;
        count++;
    }
    closedir(d);
    if (count == 0) {
        free(names);
        sb_add_text(result, word);
        return;
    }
    qsort(names, count, sizeof(*names), cmp_names);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) sb_add_char(result, ' ');
        if (slash != NULL) {
            sb_add_text(result, dirpart);
            sb_add_char(result, '/');
        }
        sb_add_text(result, names[i]);
        free(names[i]);
    }
    free(names);
}

static void flush_word(TerminalTab *tab, StringBuilder *word, int has_pattern,
                       int *redirect_next, StringBuilder *result)
{
    if (word->length == 0) return;
    if (word_is_redirect(word->text, word->length)) {
        *redirect_next = 1;
        sb_add_text(result, word->text);
    } else if (has_pattern && !*redirect_next) {
        append_expanded(tab, word->text, result);
        *redirect_next = 0;
    } else {
        sb_add_text(result, word->text);
        *redirect_next = 0;
    }
    word->length = 0;
    word->text[0] = '\0';
}

char *expand_globs(TerminalTab *tab, const char *line)
{
    StringBuilder result, word;
    sb_init(&result);
    sb_init(&word);
    int in_single = 0, in_double = 0, has_pattern = 0, redirect_next = 0;
    for (int i = 0; line[i] != '\0'; ) {
        char c = line[i];
        if (in_single) {
            sb_add_char(&word, c);
            if (c == '\'') in_single = 0;
            i++;
        } else if (in_double) {
            if (c == '"') {
                sb_add_char(&word, c);
                in_double = 0;
                i++;
            } else if (c == '\\' && line[i + 1] != '\0') {
                sb_add_char(&word, c);
                sb_add_char(&word, line[i + 1]);
                i += 2;
            } else {
                sb_add_char(&word, c);
                i++;
            }
        } else if (c == '\'') {
            in_single = 1;
            sb_add_char(&word, c);
            i++;
        } else if (c == '"') {
            in_double = 1;
            sb_add_char(&word, c);
            i++;
        } else if (c == '\\' && line[i + 1] != '\0') {
            sb_add_char(&word, c);
            sb_add_char(&word, line[i + 1]);
            i += 2;
        } else if (c == ' ' || c == '\t' || c == '\n') {
            flush_word(tab, &word, has_pattern, &redirect_next, &result);
            has_pattern = 0;
            sb_add_char(&result, c);
            i++;
        } else if (c == '|' || c == '&') {
            flush_word(tab, &word, has_pattern, &redirect_next, &result);
            has_pattern = 0;
            redirect_next = 0;
            sb_add_char(&result, c);
            i++;
        } else {
            if (c == '*' || c == '?' || c == '[') has_pattern = 1;
            sb_add_char(&word, c);
            i++;
        }
    }
    flush_word(tab, &word, has_pattern, &redirect_next, &result);
    free(word.text);
    if (result.text == NULL) {
        result.text = malloc(1);
        if (result.text != NULL) result.text[0] = '\0';
    }
    return result.text;
}

