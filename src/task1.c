#include "terminator.h"

void add_line(TerminalTab *tab, const char *text)
{

    if (tab->script_sink) {
        write_all_fd(STDERR_FILENO, text, (int)strlen(text));
        write_all_fd(STDERR_FILENO, "\n", 1);
        return;
    }

    if (g_am_worker_process) {
        proto_send_frame('O', text, (int)strlen(text));
        return;
    }

    pthread_mutex_lock(&tab->buffer_lock);

    if (tab->line_count == MAX_LINES) {

        memmove(tab->lines,
                tab->lines + 1,
                sizeof(tab->lines[0]) * (MAX_LINES - 1));
        tab->line_count--;
    }

    snprintf(tab->lines[tab->line_count], MAX_LINE_LENGTH, "%s", text);

    tab->line_count++;
    tab->scroll_position = 0;

    pthread_mutex_unlock(&tab->buffer_lock);
}

void add_output(TerminalTab *tab, const char *text)
{

    if (tab->script_sink) {
        write_all_fd(STDOUT_FILENO, text, (int)strlen(text));
        return;
    }

    char line[MAX_LINE_LENGTH];
    int position = 0;

    while (*text != '\0') {
        if (*text == '\n') {
            line[position] = '\0';
            add_line(tab, line);
            position = 0;
        } else if (position < MAX_LINE_LENGTH - 1) {
            line[position] = *text;
            position++;
        }
        text++;
    }

    if (position > 0) {
        line[position] = '\0';
        add_line(tab, line);
    }
}

void buffer_clear(TerminalTab *tab)
{
    if (g_am_worker_process) {
        proto_send_frame('K', "", 0);
        return;
    }
    pthread_mutex_lock(&tab->buffer_lock);
    tab->line_count = 0;
    tab->scroll_position = 0;
    pthread_mutex_unlock(&tab->buffer_lock);
}

void initialize_tab(TerminalTab *tab)
{
    memset(tab, 0, sizeof(*tab));

    tab->output_fd = -1;
    tab->child_pid = -1;
    tab->pipe_count = 0;
    tab->last_pid = -1;
    tab->active = 1;
    tab->history_position = -1;

    getcwd(tab->cwd, sizeof(tab->cwd));
    tab->previous_cwd[0] = '\0';
    tab->last_exit_status = 0;

    tab->pending_line[0] = '\0';
    tab->continuing = 0;

    tab->cursor = 0;
    tab->saved_input[0] = '\0';
    tab->saved_valid = 0;

    tab->watch_count = 0;
    tab->watch_active = 0;
    for (int i = 0; i < MAX_WATCHES; i++) tab->watches[i].read_fd = -1;

    tab->job_count = 0;
    tab->fg_cmd[0] = '\0';
    tab->fg_pgid = -1;
    tab->fg_preset_valid = 0;
    tab->fg_preset_code = 0;
    for (int i = 0; i < MAX_JOBS; i++) tab->jobs[i].read_fd = -1;

    pthread_mutex_init(&tab->buffer_lock, NULL);
    pthread_mutex_init(&tab->q_lock, NULL);
    tab->worker_started = 0;
    tab->worker_pid = -1;
    tab->proto_in_w = -1;
    tab->proto_out_fd = -1;
    tab->worker_dead = 0;
    tab->pbuf_len = 0;
    tab->wake_r = -1;
    tab->wake_w = -1;
    tab->q_head = 0;
    tab->q_tail = 0;
    tab->q_count = 0;
    tab->want_close = 0;
    tab->gui_dirty = 0;

    tab->var_count = 0;
    tab->pos_count = 0;
    snprintf(tab->pos_args[0], MAX_INPUT, "%s", "terminator");
    tab->script_sink = 0;
    tab->def_head = 0;
    tab->def_tail = 0;
    tab->def_count = 0;
    tab->script_proto_left_len = 0;

    if (!g_process_mode || g_am_worker_process) {
        add_line(tab, "Welcome to Terminator.");
        add_line(tab, "Type a command and press Enter.");
    }
}

void draw_text(int x, int y, const char *text)
{
    XDrawString(display, window, graphics, x, y, text, (int)strlen(text));
}

void draw_window(void)
{
    TerminalTab *tab = &tabs[active_tab];
    char tab_label[600];

    XClearWindow(display, window);

    int x_position = 15;
    int visible_number = 0;
    for (int i = 0; i < MAX_TABS; i++) {
        if (!tabs[i].active) continue;
        visible_number++;

        snprintf(tab_label, sizeof(tab_label), "[%d]%s",
                 visible_number, i == active_tab ? "*" : "");
        tab_label_left[i] = x_position;
        draw_text(x_position, 25, tab_label);
        x_position += (int)strlen(tab_label) * 12 + 14;
        tab_label_right[i] = x_position - 6;
    }
    plus_label_left = x_position;
    draw_text(x_position, 25, "[+]");
    plus_label_right = x_position + 42;

    XDrawLine(display, window, graphics, 10, 40, WINDOW_WIDTH - 10, 40);

int available_lines = VISIBLE_LINES;
int lines_that_fit = (WINDOW_HEIGHT - 90) / LINE_HEIGHT;
if (available_lines > lines_that_fit) available_lines = lines_that_fit;

int char_width = font_info != NULL ? font_info->max_bounds.width : 12;
if (char_width <= 0) char_width = 12;
int chars_per_row = (WINDOW_WIDTH - 15 - 10) / char_width;
if (chars_per_row < 1) chars_per_row = 1;

pthread_mutex_lock(&tab->buffer_lock);

char prompt[MAX_INPUT + 600];
char prompt_prefix[600];
if (tab->searching)
    snprintf(prompt_prefix, sizeof(prompt_prefix), "Enter search term: ");
else if (tab->continuing)
    snprintf(prompt_prefix, sizeof(prompt_prefix), "> ");
else
    snprintf(prompt_prefix, sizeof(prompt_prefix), "%s> ", tab->cwd);
snprintf(prompt, sizeof(prompt), "%s%s", prompt_prefix, tab->input);
int prompt_length = (int)strlen(prompt);

int cursor_position = (int)strlen(prompt_prefix) + tab->cursor;
int cursor_row = cursor_position / chars_per_row;
int cursor_column = cursor_position % chars_per_row;
int input_rows = (prompt_length + chars_per_row - 1) / chars_per_row;
if (input_rows < 1) input_rows = 1;
if (input_rows < cursor_row + 1) input_rows = cursor_row + 1;

if (available_lines > input_rows - 1)
    available_lines -= (input_rows - 1);
else
    available_lines = 0;

int first_line = tab->line_count - available_lines - tab->scroll_position;
if (first_line < 0) first_line = 0;

int y_position = 65;
for (int i = first_line;
     i < tab->line_count && y_position < WINDOW_HEIGHT - 40;
     i++) {
    draw_text(15, y_position, tab->lines[i]);
    y_position += LINE_HEIGHT;
}

for (int row = 0; row < input_rows; row++) {
    int start = row * chars_per_row;
    int length = prompt_length - start;
    if (length > chars_per_row) length = chars_per_row;
    if (length < 0) length = 0;
    XDrawString(display, window, graphics, 15,
                y_position + row * LINE_HEIGHT,
                prompt + start, length);
}

if (font_info != NULL) {
    int cursor_start = cursor_row * chars_per_row;
    int cursor_x = 15 + XTextWidth(font_info, prompt + cursor_start,
                                   cursor_column);
    int cursor_top = y_position + cursor_row * LINE_HEIGHT
                     - font_info->ascent;
    XFillRectangle(display, window, graphics, cursor_x, cursor_top, 2,
                   (unsigned int)(font_info->ascent + font_info->descent));
}

pthread_mutex_unlock(&tab->buffer_lock);

    XFlush(display);
}

void open_new_tab(void)
{
    if (tab_count >= MAX_TABS) {
        add_line(&tabs[active_tab], "Maximum number of tabs reached.");
        return;
    }
    for (int i = 0; i < MAX_TABS; i++) {
        if (!tabs[i].active) {
            initialize_tab(&tabs[i]);
            active_tab = i;
            tab_count++;

            if (!spawn_tab_worker(i)) {
                tabs[i].active = 0;
                tab_count--;
                for (int k = 0; k < MAX_TABS; k++)
                    if (tabs[k].active) { active_tab = k; break; }
            }
            return;
        }
    }
}

void close_tab_at(int slot)
{

    TerminalTab *tab = &tabs[slot];

    if (g_process_mode) {
        if (tab->worker_pid > 0) {
            frame_write(tab->proto_in_w, 'Q', "", 0);
            add_dying(tab->worker_pid);
            tab->worker_pid = -1;
        }
        if (tab->proto_in_w >= 0) close(tab->proto_in_w);
        if (tab->proto_out_fd >= 0) close(tab->proto_out_fd);
        tab->proto_in_w = -1;
        tab->proto_out_fd = -1;
    } else if (tab->worker_started) {
        TabMsg quit_msg;
        quit_msg.type = MSG_QUIT;
        quit_msg.text[0] = '\0';
        pthread_mutex_lock(&tab->q_lock);
        if (tab->q_count < TAB_QUEUE_SIZE) {
            tab->queue[tab->q_tail] = quit_msg;
            tab->q_tail = (tab->q_tail + 1) % TAB_QUEUE_SIZE;
            tab->q_count++;
        }
        pthread_mutex_unlock(&tab->q_lock);
        char byte = 'w';
        ssize_t ignored = write(tab->wake_w, &byte, 1);
        (void)ignored;
        pthread_join(tab->worker_thread, NULL);
        tab->worker_started = 0;
        if (tab->wake_r >= 0) close(tab->wake_r);
        if (tab->wake_w >= 0) close(tab->wake_w);
        tab->wake_r = -1;
        tab->wake_w = -1;
    } else {

        if (tab->watch_active) stop_multiwatch(tab);
        kill_all_jobs(tab);
        if (tab->output_fd >= 0) {
            if (tab->child_pid > 0) kill(tab->child_pid, SIGTERM);
            for (int i = 0; i < tab->pipe_count; i++)
                if (tab->pipe_pids[i] > 0) kill(tab->pipe_pids[i], SIGTERM);
            reap_pipeline(tab);
            close(tab->output_fd);
            tab->output_fd = -1;
        }
    }

    tab->active = 0;
    tab_count--;

    if (tab_count == 0) {
        running = 0;
        return;
    }

    if (slot == active_tab) {
        for (int i = 0; i < MAX_TABS; i++) {
            if (tabs[i].active) { active_tab = i; break; }
        }
    }
}

void close_active_tab(void)
{

    close_tab_at(active_tab);
}

void switch_to_visible_tab(int visible_index)
{
    int seen = 0;
    for (int i = 0; i < MAX_TABS; i++) {
        if (!tabs[i].active) continue;
        if (seen == visible_index) {
            active_tab = i;
            return;
        }
        seen++;
    }
}
