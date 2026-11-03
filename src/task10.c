#include "terminator.h"

int compl_name_cmp(const void *a, const void *b)
{
    return strcmp(((const ComplMatch *)a)->name, ((const ComplMatch *)b)->name);
}

void input_replace_range(TerminalTab *tab, int start, int end,
                                const char *replacement)
{
    if (start < 0 || end < start || end > tab->input_length) return;
    size_t repl_len = strlen(replacement);
    size_t tail_len = (size_t)(tab->input_length - end);
    if ((size_t)start + repl_len + tail_len >= MAX_INPUT) {
        if ((size_t)start + tail_len >= MAX_INPUT) return;
        repl_len = MAX_INPUT - 1 - (size_t)start - tail_len;
    }
    memmove(tab->input + start + repl_len, tab->input + end, tail_len + 1);
    memcpy(tab->input + start, replacement, repl_len);
    tab->input_length = (int)(start + repl_len + tail_len);
    tab->input[start + repl_len + tail_len] = '\0';
    tab->cursor = (int)(start + repl_len);
}

void handle_tab_completion(TerminalTab *tab)
{
    tab->completing = 0;
    if (tab->cursor <= 0) return;
    tab->input[tab->input_length] = '\0';

    int word_start = tab->cursor;
    while (word_start > 0 && tab->input[word_start - 1] != ' ' &&
           tab->input[word_start - 1] != '\t')
        word_start--;
    if (word_start >= tab->cursor) return;
    int word_len = tab->cursor - word_start;
    if (word_len >= MAX_INPUT) return;
    char word[MAX_INPUT];
    memcpy(word, tab->input + word_start, (size_t)word_len);
    word[word_len] = '\0';

    char dir_path[1024];
    const char *base = word;
    char *slash = strrchr(word, '/');
    if (slash != NULL) {
        base = slash + 1;
        size_t dir_len = (size_t)(slash - word);
        if (dir_len == 0) {
            snprintf(dir_path, sizeof(dir_path), "/");
        } else if (word[0] == '/') {
            snprintf(dir_path, sizeof(dir_path), "%.*s", (int)dir_len, word);
        } else if (word[0] == '~' && word[1] == '/') {
            const char *home = getenv("HOME");
            if (home == NULL) return;
            snprintf(dir_path, sizeof(dir_path), "%s/%.*s", home,
                     (int)(dir_len - 2), word + 2);
        } else {
            snprintf(dir_path, sizeof(dir_path), "%s/%.*s", tab->cwd,
                     (int)dir_len, word);
        }
    } else {
        snprintf(dir_path, sizeof(dir_path), "%s", tab->cwd);
    }
    size_t base_len = strlen(base);

    DIR *dir = opendir(dir_path);
    if (dir == NULL) return;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < COMPL_MAX) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (name[0] == '.' && base[0] != '.') continue;
        if (strncmp(name, base, base_len) != 0) continue;
        snprintf(tab->compl_matches[count].name,
                 sizeof(tab->compl_matches[count].name), "%s", name);

        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", dir_path, name);
        struct stat st;
        tab->compl_matches[count].is_dir =
            (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
        count++;
    }
    closedir(dir);
    if (count == 0) return;

    qsort(tab->compl_matches, (size_t)count, sizeof(ComplMatch), compl_name_cmp);

    int base_start = word_start + (int)(base - word);

    if (count == 1) {
        char replacement[300];
        snprintf(replacement, sizeof(replacement), "%s%s",
                 tab->compl_matches[0].name,
                 tab->compl_matches[0].is_dir ? "/" : "");
        input_replace_range(tab, base_start, tab->cursor, replacement);
        return;
    }

    size_t lcp = strlen(tab->compl_matches[0].name);
    for (int i = 1; i < count; i++) {
        size_t j = 0;
        while (j < lcp &&
               tab->compl_matches[i].name[j] == tab->compl_matches[0].name[j])
            j++;
        lcp = j;
    }
    if (lcp > base_len) {

        char extension[256];
        size_t ext_len = lcp - base_len;
        if (ext_len >= sizeof(extension)) ext_len = sizeof(extension) - 1;
        memcpy(extension, tab->compl_matches[0].name + base_len, ext_len);
        extension[ext_len] = '\0';
        input_replace_range(tab, tab->cursor, tab->cursor, extension);
        return;
    }

    {
        char list[4096];
        size_t used = 0;
        list[0] = '\0';
        for (int i = 0; i < count; i++) {
            int w = snprintf(list + used, sizeof(list) - used, "%s%d. %s",
                             i == 0 ? "" : " ", i + 1,
                             tab->compl_matches[i].name);
            if (w < 0 || (size_t)w >= sizeof(list) - used) break;
            used += (size_t)w;
        }
        add_line(tab, list);
    }
    tab->compl_start = base_start;
    tab->compl_len = (int)base_len;
    snprintf(tab->compl_word, sizeof(tab->compl_word), "%s", base);
    tab->compl_count = count;
    tab->completing = 1;
}

int completion_pick(TerminalTab *tab, int pick)
{
    if (!tab->completing || pick < 1 || pick > tab->compl_count) return 0;
    if (tab->compl_start < 0 ||
        tab->compl_start + tab->compl_len > tab->input_length ||
        memcmp(tab->input + tab->compl_start, tab->compl_word,
               (size_t)tab->compl_len) != 0)
        return 0;
    char replacement[300];
    snprintf(replacement, sizeof(replacement), "%s%s",
             tab->compl_matches[pick - 1].name,
             tab->compl_matches[pick - 1].is_dir ? "/" : "");
    input_replace_range(tab, tab->compl_start,
                        tab->compl_start + tab->compl_len, replacement);
    tab->completing = 0;
    return 1;
}
