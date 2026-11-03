#include "terminator.h"

void hist_lock(void)
{
    if (g_process_mode) {
        while (sem_wait(&hist_store->psem) != 0 && errno == EINTR) {

        }
    } else {
        pthread_mutex_lock(&hist_store->mutex);
    }
}

void hist_unlock(void)
{
    if (g_process_mode) sem_post(&hist_store->psem);
    else pthread_mutex_unlock(&hist_store->mutex);
}

void hist_store_insert_locked(const char *text)
{
    if (hist_store->count >= HIST_MAX) {
        int drop = 1000;
        memmove(hist_store->items, hist_store->items + drop,
                (size_t)(hist_store->count - drop) * HIST_LEN);
        hist_store->count -= drop;
    }
    snprintf(hist_store->items[hist_store->count], HIST_LEN, "%s", text);
    hist_store->count++;
}

void hist_add(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    hist_lock();
    hist_store_insert_locked(text);
    hist_file_append(text);
    hist_unlock();
}

int hist_count(void)
{
    hist_lock();
    int count = hist_store->count;
    hist_unlock();
    return count;
}

int hist_get(int index, char *out)
{
    hist_lock();
    if (index < 0 || index >= hist_store->count) {
        hist_unlock();
        return 0;
    }
    snprintf(out, HIST_LEN, "%s", hist_store->items[index]);
    hist_unlock();
    return 1;
}

void hist_file_path_init(void)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        hist_file_path[0] = '\0';
        return;
    }
    snprintf(hist_file_path, sizeof(hist_file_path),
             "%s/.terminator_history", home);
}

void hist_file_append(const char *text)
{
    if (hist_file_path[0] == '\0') return;
    int fd = open(hist_file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    size_t left = strlen(text);
    const char *p = text;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w <= 0) break;
        p += w;
        left -= (size_t)w;
    }
    if (write(fd, "\n", 1) < 0) {   }
    close(fd);
}

void hist_file_load(void)
{
    if (hist_file_path[0] == '\0') return;
    FILE *f = fopen(hist_file_path, "r");
    if (f == NULL) return;

    char *line = NULL;
    size_t cap = 0;
    ssize_t got;
    long total_lines = 0;
    while ((got = getline(&line, &cap, f)) > 0) total_lines++;
    long skip = total_lines > HIST_MAX ? total_lines - HIST_MAX : 0;

    rewind(f);
    long seen = 0;
    while ((got = getline(&line, &cap, f)) > 0) {
        if (seen++ < skip) continue;
        while (got > 0 && (line[got - 1] == '\n' || line[got - 1] == '\r'))
            line[--got] = '\0';
        if (line[0] == '\0') continue;
        hist_lock();
        hist_store_insert_locked(line);
        hist_unlock();
    }
    free(line);
    fclose(f);

    if (skip > 0) {
        f = fopen(hist_file_path, "w");
        if (f != NULL) {
            int total = hist_count();
            for (int i = 0; i < total; i++) {
                char entry[HIST_LEN];
                if (hist_get(i, entry)) fprintf(f, "%s\n", entry);
            }
            fclose(f);
        }
    }
}

void init_history_store(void)
{
    if (g_process_mode) {
        void *mapping = mmap(NULL, sizeof(SharedHistory),
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            fprintf(stderr, "terminator: shared memory failed; "
                            "history will not be shared\n");
            hist_store = &hist_static_store;
        } else {
            hist_store = (SharedHistory *)mapping;
        }
        memset(hist_store, 0, sizeof(*hist_store));
        pthread_mutex_init(&hist_store->mutex, NULL);
        sem_init(&hist_store->psem, 1, 1);
    } else {
        hist_store = &hist_static_store;
        memset(hist_store, 0, sizeof(*hist_store));
        pthread_mutex_init(&hist_store->mutex, NULL);
        sem_init(&hist_store->psem, 0, 1);
    }
}

int longest_common_substring(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la == 0 || lb == 0) return 0;
    int *prev = (int *)calloc(lb + 1, sizeof(int));
    int *cur = (int *)calloc(lb + 1, sizeof(int));
    if (prev == NULL || cur == NULL) {
        free(prev);
        free(cur);
        return 0;
    }
    int best = 0;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = 0;
        for (size_t j = 1; j <= lb; j++) {
            if (a[i - 1] == b[j - 1]) {
                cur[j] = prev[j - 1] + 1;
                if (cur[j] > best) best = cur[j];
            } else {
                cur[j] = 0;
            }
        }
        int *tmp = prev;
        prev = cur;
        cur = tmp;
    }
    free(prev);
    free(cur);
    return best;
}

int shares_three_gram(const char *term, const char *entry)
{
    size_t lt = strlen(term);
    if (lt < 3) return 0;
    char gram[4];
    for (size_t i = 0; i + 3 <= lt; i++) {
        memcpy(gram, term + i, 3);
        gram[3] = '\0';
        if (strstr(entry, gram) != NULL) return 1;
    }
    return 0;
}

void search_restore_input(TerminalTab *tab)
{
    snprintf(tab->input, sizeof(tab->input), "%s", tab->search_stash);
    tab->input_length = (int)strlen(tab->input);
    tab->cursor = tab->search_stash_cursor;
    if (tab->cursor > tab->input_length) tab->cursor = tab->input_length;
    tab->searching = 0;
}

void run_history_search(TerminalTab *tab, const char *term)
{
    char shown[MAX_INPUT + 32];
    snprintf(shown, sizeof(shown), "Enter search term: %s", term);
    add_line(tab, shown);

    int total = hist_count();
    char entry[HIST_LEN];

    for (int i = total - 1; i >= 0; i--) {
        if (hist_get(i, entry) && strcmp(entry, term) == 0) {
            add_output(tab, entry);
            return;
        }
    }

    if (term[0] != '\0') {
        int found_substring = 0;
        for (int i = total - 1; i >= 0; i--) {
            if (hist_get(i, entry) && strstr(entry, term) != NULL) {
                add_output(tab, entry);
                found_substring = 1;
            }
        }
        if (found_substring) return;
    }

    int best = 0;
    for (int i = 0; i < total; i++) {
        if (hist_get(i, entry) && shares_three_gram(term, entry)) {
            int score = longest_common_substring(term, entry);
            if (score > best) best = score;
        }
    }
    if (best > 2) {
        for (int i = total - 1; i >= 0; i--) {
            if (hist_get(i, entry) && shares_three_gram(term, entry) &&
                longest_common_substring(term, entry) == best)
                add_output(tab, entry);
        }
        return;
    }

    add_line(tab, "No match for search term in history");
}
