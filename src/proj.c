#include "terminator.h"

Display *display;

Window window;

GC graphics;

TerminalTab tabs[MAX_TABS];

int active_tab = 0;

int tab_count = 1;

int running = 1;

Atom close_message;

int g_process_mode = 0;

int g_am_worker_process = 0;

SharedHistory *hist_store = NULL;

SharedHistory hist_static_store;

int gui_wake_r = -1;

int gui_wake_w = -1;

DyingWorker dying_workers[MAX_DYING];

int dying_count = 0;

XFontStruct *font_info = NULL;

char clipboard_text[MAX_CLIPBOARD];

int clipboard_owned = 0;

Atom xa_clipboard, xa_utf8_string, xa_targets, xa_paste_atom;

int tab_label_left[MAX_TABS];

int tab_label_right[MAX_TABS];

int plus_label_left = -1;

int plus_label_right = -1;

char hist_file_path[1024];

int g_script_depth = 0;

const Binding DEFAULT_BINDINGS[] = {
    { XK_t,       ControlMask,               ACT_NEW_TAB },
    { XK_w,       ControlMask | ShiftMask,   ACT_CLOSE_TAB },
    { XK_l,       ControlMask,               ACT_CLEAR_SCREEN },
    { XK_c,       ControlMask,               ACT_INTERRUPT },
    { XK_d,       ControlMask,               ACT_EOF_OR_DELETE },
    { XK_c,       ControlMask | ShiftMask,   ACT_COPY },
    { XK_v,       ControlMask | ShiftMask,   ACT_PASTE },
    { XK_Left,    0,                         ACT_CURSOR_LEFT },
    { XK_Right,   0,                         ACT_CURSOR_RIGHT },
    { XK_a,       ControlMask,               ACT_CURSOR_START },
    { XK_Home,    0,                         ACT_CURSOR_START },
    { XK_e,       ControlMask,               ACT_CURSOR_END },
    { XK_End,     0,                         ACT_CURSOR_END },
    { XK_Up,      0,                         ACT_HISTORY_PREV },
    { XK_Down,    0,                         ACT_HISTORY_NEXT },
    { XK_u,       ControlMask,               ACT_DELETE_TO_START },
    { XK_k,       ControlMask,               ACT_DELETE_TO_END },
    { XK_w,       ControlMask,               ACT_DELETE_WORD },
    { XK_z,       ControlMask,               ACT_SUSPEND },
    { XK_r,       ControlMask,               ACT_SEARCH_HISTORY },
    { XK_Page_Up, 0,                         ACT_SCROLL_UP },
    { XK_Page_Down, 0,                       ACT_SCROLL_DOWN },
};

Binding key_bindings[MAX_BINDINGS];

int binding_count = 0;

int rc_warning_count = 0;

const ActionName ACTION_NAMES[] = {
    { "new-tab",         ACT_NEW_TAB },
    { "close-tab",       ACT_CLOSE_TAB },
    { "clear-screen",    ACT_CLEAR_SCREEN },
    { "interrupt",       ACT_INTERRUPT },
    { "eof",             ACT_EOF_OR_DELETE },
    { "copy",            ACT_COPY },
    { "paste",           ACT_PASTE },
    { "cursor-left",     ACT_CURSOR_LEFT },
    { "cursor-right",    ACT_CURSOR_RIGHT },
    { "cursor-start",    ACT_CURSOR_START },
    { "cursor-end",      ACT_CURSOR_END },
    { "history-prev",    ACT_HISTORY_PREV },
    { "history-next",    ACT_HISTORY_NEXT },
    { "delete-to-start", ACT_DELETE_TO_START },
    { "delete-to-end",   ACT_DELETE_TO_END },
    { "delete-word",     ACT_DELETE_WORD },
    { "suspend",         ACT_SUSPEND },
    { "search-history",  ACT_SEARCH_HISTORY },
    { "scroll-up",       ACT_SCROLL_UP },
    { "scroll-down",     ACT_SCROLL_DOWN },
};

int main(int argument_count, char **arguments)
{

    for (int i = 1; i < argument_count; i++) {
        if (strcmp(arguments[i], "useProcess=True") == 0) {
            g_process_mode = 1;
        } else if (strcmp(arguments[i], "useProcess=False") == 0) {
            g_process_mode = 0;
        } else {
            fprintf(stderr, "terminator: ignoring unknown argument '%s'\n",
                    arguments[i]);
        }
    }

    init_history_store();

    hist_file_path_init();
    hist_file_load();

    if (!g_process_mode) {
        int wake_fds[2];
        if (pipe(wake_fds) == 0) {
            gui_wake_r = wake_fds[0];
            gui_wake_w = wake_fds[1];
            fcntl(gui_wake_w, F_SETFL, O_NONBLOCK);
        }
    }

    char rc_path[1024];
    rc_path[0] = '\0';
    const char *home_folder = getenv("HOME");
    if (home_folder != NULL)
        snprintf(rc_path, sizeof(rc_path), "%s/.terminatorrc", home_folder);
    load_bindings(rc_path[0] != '\0' ? rc_path : NULL,
                  key_bindings, &binding_count, &rc_warning_count);

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Error: Cannot open X11 display.\n");
        return 1;
    }

    int screen = DefaultScreen(display);

    window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, 1,
                                 BlackPixel(display, screen),
                                 BlackPixel(display, screen));

    XStoreName(display, window, g_process_mode ?
               "Terminator - Process Mode" : "Terminator - Thread Mode");

    XSelectInput(display, window, ExposureMask | KeyPressMask | ButtonPressMask);

    graphics = XCreateGC(display, window, 0, NULL);
    XSetForeground(display, graphics, WhitePixel(display, screen));
    XSetBackground(display, graphics, BlackPixel(display, screen));

    {
        XFontStruct *bigger_font = XLoadQueryFont(display, "12x24");
        if (bigger_font != NULL)
            XSetFont(display, graphics, bigger_font->fid);
    }

    font_info = XQueryFont(display, XGContextFromGC(graphics));
    xa_clipboard = XInternAtom(display, "CLIPBOARD", False);
    xa_utf8_string = XInternAtom(display, "UTF8_STRING", False);
    xa_targets = XInternAtom(display, "TARGETS", False);
    xa_paste_atom = XInternAtom(display, "TERMINATOR_PASTE", False);

    close_message = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &close_message, 1);

    for (int i = 0; i < MAX_TABS; i++) {
        tabs[i].active = 0;
        tabs[i].output_fd = -1;
        tabs[i].child_pid = -1;
        tabs[i].pipe_count = 0;
        tabs[i].last_pid = -1;
    }
    initialize_tab(&tabs[0]);

    if (!spawn_tab_worker(0)) {
        fprintf(stderr, "Error: could not start the first tab's worker.\n");
        return 1;
    }

    if (rc_warning_count > 0) {
        char note[512];
        snprintf(note, sizeof(note),
                 "terminator: %d problem(s) in ~/.terminatorrc - defaults "
                 "kept (details in the terminal you started from)",
                 rc_warning_count);
        add_line(&tabs[0], note);
    }

    XClassHint *class_hint = XAllocClassHint();
    class_hint->res_name = "terminator";
    class_hint->res_class = "Terminator";
    XSetClassHint(display, window, class_hint);

    XSizeHints *size_hints = XAllocSizeHints();
    size_hints->flags = PSize | PMinSize;
    size_hints->width = WINDOW_WIDTH;
    size_hints->height = WINDOW_HEIGHT;
    size_hints->min_width = WINDOW_WIDTH;
    size_hints->min_height = WINDOW_HEIGHT;
    XSetNormalHints(display, window, size_hints);

    XMapRaised(display, window);
    XFlush(display);
    draw_window();

    int x11_fd = ConnectionNumber(display);

    while (running) {

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(x11_fd, &read_fds);

        int max_fd = x11_fd;
        if (gui_wake_r >= 0) {
            FD_SET(gui_wake_r, &read_fds);
            if (gui_wake_r > max_fd) max_fd = gui_wake_r;
        }
        for (int i = 0; i < MAX_TABS; i++) {
            if (tabs[i].active && tabs[i].proto_out_fd >= 0) {
                FD_SET(tabs[i].proto_out_fd, &read_fds);
                if (tabs[i].proto_out_fd > max_fd) max_fd = tabs[i].proto_out_fd;
            }
        }

        struct timeval timeout;
        struct timeval *timeout_pointer = NULL;
        if (dying_count > 0) {
            timeout.tv_sec = 0;
            timeout.tv_usec = 200000;
            timeout_pointer = &timeout;
        }
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, timeout_pointer);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (FD_ISSET(x11_fd, &read_fds)) {
            while (XPending(display) > 0) {
                XEvent event;
                XNextEvent(display, &event);

                if (event.type == Expose) {
                    draw_window();
                } else if (event.type == KeyPress) {
                    handle_key_press(&event.xkey);
                    draw_window();
} else if (event.type == ButtonPress) {

    if (event.xbutton.button == 4 || event.xbutton.button == 5) {
        if (event.xbutton.y >= 40) {
            TerminalTab *tab = &tabs[active_tab];
            if (event.xbutton.button == 4) {
                tab->scroll_position += 3;
                if (tab->scroll_position > tab->line_count - 1)
                    tab->scroll_position = tab->line_count - 1;
                if (tab->scroll_position < 0)
                    tab->scroll_position = 0;
            } else {
                tab->scroll_position -= 3;
                if (tab->scroll_position < 0)
                    tab->scroll_position = 0;
            }
            draw_window();
        }
    } else if (event.xbutton.button == 1 &&
               event.xbutton.y < 40) {
        int click_x = event.xbutton.x;
        if (click_x >= plus_label_left && click_x <= plus_label_right) {
            open_new_tab();
        } else {
            for (int i = 0; i < MAX_TABS; i++) {
                if (tabs[i].active &&
                    click_x >= tab_label_left[i] &&
                    click_x <= tab_label_right[i]) {
                    active_tab = i;
                    break;
                }
            }
        }
        draw_window();
    }

                } else if (event.type == ClientMessage) {
                    if ((Atom)event.xclient.data.l[0] == close_message)
                        running = 0;
                } else if (event.type == SelectionRequest) {

                    serve_selection_request(&event.xselectionrequest);
                } else if (event.type == SelectionNotify) {

                    handle_selection_notify(&event.xselection);
                    draw_window();
                } else if (event.type == SelectionClear) {

                    clipboard_owned = 0;
                }
            }
        }

        if (gui_wake_r >= 0 && FD_ISSET(gui_wake_r, &read_fds)) {
            char drain[256];
            ssize_t ignored = read(gui_wake_r, drain, sizeof(drain));
            (void)ignored;
            for (int i = 0; i < MAX_TABS; i++) tabs[i].gui_dirty = 0;
            draw_window();
        }
        if (g_process_mode) {
            for (int i = 0; i < MAX_TABS; i++) {
                if (tabs[i].active && tabs[i].proto_out_fd >= 0 &&
                    FD_ISSET(tabs[i].proto_out_fd, &read_fds)) {
                    drain_proto(&tabs[i]);
                    if (i == active_tab) draw_window();
                }
            }
        }

        for (int i = 0; i < MAX_TABS; i++) {
            if (tabs[i].active && tabs[i].want_close) {
                tabs[i].want_close = 0;
                close_tab_at(i);
                draw_window();
                if (!running) break;
            }
        }
        sweep_dying();
    }

    for (int i = 0; i < MAX_TABS; i++) {
        if (tabs[i].active) close_tab_at(i);
    }
    {
        time_t deadline = time(NULL) + 3;
        while (dying_count > 0 && time(NULL) < deadline) {
            sweep_dying();
            struct timespec pause_slice = { 0, 50 * 1000 * 1000 };
            nanosleep(&pause_slice, NULL);
        }
        for (int i = 0; i < dying_count; i++) {
            kill(dying_workers[i].pid, SIGKILL);
            waitpid(dying_workers[i].pid, NULL, 0);
        }
        dying_count = 0;
    }

    XFreeGC(display, graphics);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}

int write_all_fd(int fd, const char *data, int length)
{
    int sent = 0;
    while (sent < length) {
        ssize_t n = write(fd, data + sent, (size_t)(length - sent));
        if (n > 0) { sent += (int)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return 0;
    }
    return 1;
}

void frame_write(int fd, char type, const char *payload, int payload_length)
{
    if (fd < 0) return;
    if (type == 'L' || type == 'O' || type == 'S') {
        char header[3];
        header[0] = type;
        header[1] = (char)(payload_length & 0xFF);
        header[2] = (char)((payload_length >> 8) & 0xFF);
        if (!write_all_fd(fd, header, 3)) return;
        if (payload_length > 0)
            write_all_fd(fd, payload, payload_length);
    } else {
        write_all_fd(fd, &type, 1);
    }
}

void proto_send_frame(char type, const char *payload, int payload_length)
{
    if (!g_am_worker_process) return;
    frame_write(PROTO_OUT_FD, type, payload, payload_length);
}

void proto_send_state(TerminalTab *tab)
{
    if (!g_am_worker_process) return;
    char payload[600];
    int length = snprintf(payload, sizeof(payload), "%s\t%d",
                          tab->cwd, tab->continuing ? 1 : 0);
    if (length > (int)sizeof(payload) - 1) length = (int)sizeof(payload) - 1;
    proto_send_frame('S', payload, length);
}

void poke_gui(void)
{
    if (g_process_mode || g_am_worker_process || gui_wake_w < 0) return;
    char byte = 'r';
    ssize_t ignored = write(gui_wake_w, &byte, 1);
    (void)ignored;
}

void queue_push(TerminalTab *tab, int type, const char *text)
{
    TabMsg message;
    message.type = type;
    snprintf(message.text, sizeof(message.text), "%s", text);
    pthread_mutex_lock(&tab->q_lock);
    if (tab->q_count < TAB_QUEUE_SIZE) {
        tab->queue[tab->q_tail] = message;
        tab->q_tail = (tab->q_tail + 1) % TAB_QUEUE_SIZE;
        tab->q_count++;
    }
    pthread_mutex_unlock(&tab->q_lock);
}

int queue_pop(TerminalTab *tab, TabMsg *out)
{
    int found = 0;
    pthread_mutex_lock(&tab->q_lock);
    if (tab->q_count > 0) {
        *out = tab->queue[tab->q_head];
        tab->q_head = (tab->q_head + 1) % TAB_QUEUE_SIZE;
        tab->q_count--;
        found = 1;
    }
    pthread_mutex_unlock(&tab->q_lock);
    return found;
}

void gui_send_line(TerminalTab *tab, const char *text)
{
    if (g_process_mode) {
        if (tab->worker_dead) return;
        frame_write(tab->proto_in_w, 'L', text, (int)strlen(text));
    } else {
        queue_push(tab, MSG_LINE, text);
        char byte = 'w';
        ssize_t ignored = write(tab->wake_w, &byte, 1);
        (void)ignored;
    }
}

void gui_send_interrupt(TerminalTab *tab)
{
    if (g_process_mode) {
        if (tab->worker_dead) return;
        frame_write(tab->proto_in_w, 'C', "", 0);
    } else {
        queue_push(tab, MSG_INTERRUPT, "");
        char byte = 'w';
        ssize_t ignored = write(tab->wake_w, &byte, 1);
        (void)ignored;
    }
}

void gui_send_suspend(TerminalTab *tab)
{
    if (g_process_mode) {
        if (tab->worker_dead) return;
        frame_write(tab->proto_in_w, 'Z', "", 0);
    } else {
        queue_push(tab, MSG_SUSPEND, "");
        char byte = 'w';
        ssize_t ignored = write(tab->wake_w, &byte, 1);
        (void)ignored;
    }
}

void request_tab_close(TerminalTab *tab)
{
    if (g_am_worker_process) {
        proto_send_frame('E', "", 0);
        tab->want_close = 1;
        return;
    }
    tab->want_close = 1;
    if (!g_process_mode) {
        tab->gui_dirty = 1;
        poke_gui();
    }
}

void engine_interrupt(TerminalTab *tab)
{
    if (foreground_running(tab)) {
        signal_foreground(tab, SIGINT);
    } else if (tab->watch_active) {
        stop_multiwatch(tab);
    } else if (tab->continuing) {
        tab->pending_line[0] = '\0';
        tab->continuing = 0;
    }
}

void engine_teardown(TerminalTab *tab)
{
    if (tab->watch_active) stop_multiwatch(tab);
    kill_all_jobs(tab);
    if (tab->output_fd >= 0) {
        if (tab->child_pid > 0) kill(tab->child_pid, SIGTERM);
        for (int i = 0; i < tab->pipe_count; i++)
            if (tab->pipe_pids[i] > 0) kill(tab->pipe_pids[i], SIGTERM);
        reap_pipeline(tab);
        if (tab->child_pid > 0) {
            for (int tries = 0; tries < 100 && tab->child_pid > 0; tries++) {
                int status;
                pid_t result = waitpid(tab->child_pid, &status, WNOHANG);
                if (result == tab->child_pid) { tab->child_pid = -1; break; }
                struct timespec pause_slice = { 0, 10 * 1000 * 1000 };
                nanosleep(&pause_slice, NULL);
            }
            if (tab->child_pid > 0) {
                kill(tab->child_pid, SIGKILL);
                waitpid(tab->child_pid, NULL, 0);
                tab->child_pid = -1;
            }
        }
        close(tab->output_fd);
        tab->output_fd = -1;
    }
}

void shell_worker_main(TerminalTab *tab)
{

    proto_send_state(tab);

    char in_bytes[8192];
    int in_have = 0;

    int done = 0;
    while (!done) {

        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = -1;
        if (tab->wake_r >= 0) {
            FD_SET(tab->wake_r, &read_fds);
            if (tab->wake_r > max_fd) max_fd = tab->wake_r;
        }
        if (g_am_worker_process) {
            FD_SET(PROTO_IN_FD, &read_fds);
            if (PROTO_IN_FD > max_fd) max_fd = PROTO_IN_FD;
        }
        if (tab->output_fd >= 0) {
            FD_SET(tab->output_fd, &read_fds);
            if (tab->output_fd > max_fd) max_fd = tab->output_fd;
        }
        for (int j = 0; j < tab->job_count; j++) {
            if (tab->jobs[j].read_fd >= 0) {
                FD_SET(tab->jobs[j].read_fd, &read_fds);
                if (tab->jobs[j].read_fd > max_fd) max_fd = tab->jobs[j].read_fd;
            }
        }
        if (tab->watch_active) {
            for (int w = 0; w < tab->watch_count; w++) {
                Watch *watch = &tab->watches[w];
                if (watch->state == 1 && watch->read_fd >= 0) {
                    FD_SET(watch->read_fd, &read_fds);
                    if (watch->read_fd > max_fd) max_fd = watch->read_fd;
                }
            }
        }
        if (max_fd < 0) break;

        struct timeval timeout;
        struct timeval *timeout_pointer = NULL;
        if (tab->watch_active || tab->job_count > 0) {
            timeout.tv_sec = 0;
            timeout.tv_usec = 200000;
            timeout_pointer = &timeout;
        }
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, timeout_pointer);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (g_am_worker_process && FD_ISSET(PROTO_IN_FD, &read_fds)) {
            ssize_t n = read(PROTO_IN_FD, in_bytes + in_have,
                             sizeof(in_bytes) - (size_t)in_have);
            if (n == 0) {

                done = 1;
            } else if (n > 0) {
                in_have += (int)n;

                if (tab->script_proto_left_len > 0 &&
                    in_have + tab->script_proto_left_len <=
                        (int)sizeof(in_bytes)) {
                    memmove(in_bytes + tab->script_proto_left_len,
                            in_bytes, (size_t)in_have);
                    memcpy(in_bytes, tab->script_proto_left,
                           (size_t)tab->script_proto_left_len);
                    in_have += tab->script_proto_left_len;
                    tab->script_proto_left_len = 0;
                }
                int used = 0;
                while (!done && in_have - used >= 1) {
                    char type = in_bytes[used];
                    if (type == 'L') {
                        if (in_have - used < 3) break;
                        int length = (unsigned char)in_bytes[used + 1] |
                                     ((unsigned char)in_bytes[used + 2] << 8);
                        if (length > MAX_INPUT - 1) length = MAX_INPUT - 1;
                        if (in_have - used < 3 + length) break;
                        char line[MAX_INPUT];
                        memcpy(line, in_bytes + used + 3, (size_t)length);
                        line[length] = '\0';
                        used += 3 + length;
                        handle_enter(tab, line);
                        proto_send_state(tab);
                        if (tab->want_close) done = 1;

                        if (!done && !tab->want_close)
                            drain_deferred_lines(tab);
                        if (tab->want_close) done = 1;
                    } else if (type == 'C') {
                        used += 1;
                        engine_interrupt(tab);
                    } else if (type == 'Z') {
                        used += 1;
                        suspend_foreground(tab);
                    } else if (type == 'Q') {
                        done = 1;
                    } else {
                        used += 1;
                    }
                }
                memmove(in_bytes, in_bytes + used, (size_t)(in_have - used));
                in_have -= used;
            }

        }

        if (!done && tab->wake_r >= 0 && FD_ISSET(tab->wake_r, &read_fds)) {
            char drain[512];
            ssize_t ignored = read(tab->wake_r, drain, sizeof(drain));
            (void)ignored;
            TabMsg message;
            while (queue_pop(tab, &message)) {
                if (message.type == MSG_LINE) {
                    handle_enter(tab, message.text);
                } else if (message.type == MSG_INTERRUPT) {
                    engine_interrupt(tab);
                } else if (message.type == MSG_SUSPEND) {
                    suspend_foreground(tab);
                } else if (message.type == MSG_QUIT) {
                    done = 1;
                    break;
                }
                if (tab->want_close) { done = 1; break; }
            }

            if (!done && !tab->want_close) drain_deferred_lines(tab);
            if (tab->want_close) done = 1;
            poke_gui();
        }

        if (done) break;

        if (tab->output_fd >= 0 && FD_ISSET(tab->output_fd, &read_fds))
            read_command_output(tab);
        for (int j = 0; j < tab->job_count; j++) {
            Job *job = &tab->jobs[j];
            if (job->read_fd >= 0 && FD_ISSET(job->read_fd, &read_fds))
                drain_job(tab, job);
        }
        if (tab->watch_active) {
            for (int w = 0; w < tab->watch_count; w++) {
                Watch *watch = &tab->watches[w];
                if (watch->state == 1 && watch->read_fd >= 0 &&
                    FD_ISSET(watch->read_fd, &read_fds))
                    drain_watch(tab, watch);
            }
        }
        reap_pipeline(tab);
        if (service_jobs_tab(tab) > 0) tab->gui_dirty = 1;
        if (service_multiwatch_tab(tab) > 0) tab->gui_dirty = 1;
        if (tab->gui_dirty) {
            tab->gui_dirty = 0;
            poke_gui();
        }
    }

    engine_teardown(tab);
}

void *worker_thread_main(void *argument)
{
    shell_worker_main((TerminalTab *)argument);
    return NULL;
}

int spawn_tab_worker(int slot)
{
    TerminalTab *tab = &tabs[slot];

    if (!g_process_mode) {

        int pipe_fds[2];
        if (pipe(pipe_fds) != 0) return 0;
        tab->wake_r = pipe_fds[0];
        tab->wake_w = pipe_fds[1];
        fcntl(tab->wake_w, F_SETFL, O_NONBLOCK);
        if (pthread_create(&tab->worker_thread, NULL,
                           worker_thread_main, tab) != 0) {
            close(tab->wake_r);
            close(tab->wake_w);
            tab->wake_r = -1;
            tab->wake_w = -1;
            return 0;
        }
        tab->worker_started = 1;
        return 1;
    }

    int to_worker[2];
    int to_gui[2];
    if (pipe(to_worker) != 0) return 0;
    if (pipe(to_gui) != 0) {
        close(to_worker[0]);
        close(to_worker[1]);
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(to_worker[0]); close(to_worker[1]);
        close(to_gui[0]);    close(to_gui[1]);
        return 0;
    }

    if (pid == 0) {

        for (int i = 0; i < MAX_TABS; i++) {
            if (tabs[i].proto_in_w >= 0) close(tabs[i].proto_in_w);
            if (tabs[i].proto_out_fd >= 0) close(tabs[i].proto_out_fd);
        }
        if (gui_wake_r >= 0) close(gui_wake_r);
        if (gui_wake_w >= 0) close(gui_wake_w);

        int inherited_x_fd = (display != NULL) ? ConnectionNumber(display) : -1;
        int in_fd = fcntl(to_worker[0], F_DUPFD, 10);
        int out_fd = fcntl(to_gui[1], F_DUPFD, 11);
        close(to_worker[0]); close(to_worker[1]);
        close(to_gui[0]);    close(to_gui[1]);
        if (in_fd < 0 || out_fd < 0) _exit(1);
        dup2(in_fd, PROTO_IN_FD);
        dup2(out_fd, PROTO_OUT_FD);
        close(in_fd);
        close(out_fd);

        fcntl(PROTO_IN_FD, F_SETFD, FD_CLOEXEC);
        fcntl(PROTO_OUT_FD, F_SETFD, FD_CLOEXEC);

        if (inherited_x_fd >= 0 && inherited_x_fd != PROTO_IN_FD &&
            inherited_x_fd != PROTO_OUT_FD)
            close(inherited_x_fd);

        g_am_worker_process = 1;

        TerminalTab mine;
        memset(&mine, 0, sizeof(mine));
        initialize_tab(&mine);
        shell_worker_main(&mine);
        _exit(0);
    }

    close(to_worker[0]);
    close(to_gui[1]);
    tab->proto_in_w = to_worker[1];
    tab->proto_out_fd = to_gui[0];

    fcntl(tab->proto_out_fd, F_SETFL, O_NONBLOCK);
    tab->worker_pid = pid;
    tab->worker_dead = 0;
    tab->pbuf_len = 0;
    return 1;
}

void drain_proto(TerminalTab *tab)
{
    for (;;) {
        int space = (int)sizeof(tab->pbuf) - tab->pbuf_len;
        if (space <= 0) tab->pbuf_len = 0;
        space = (int)sizeof(tab->pbuf) - tab->pbuf_len;
        ssize_t n = read(tab->proto_out_fd, tab->pbuf + tab->pbuf_len,
                         (size_t)space);
        if (n > 0) {
            tab->pbuf_len += (int)n;
            int used = 0;
            while (tab->pbuf_len - used >= 1) {
                char type = tab->pbuf[used];
                if (type == 'O' || type == 'S') {
                    if (tab->pbuf_len - used < 3) break;
                    int length = (unsigned char)tab->pbuf[used + 1] |
                                 ((unsigned char)tab->pbuf[used + 2] << 8);
                    if (tab->pbuf_len - used < 3 + length) break;
                    if (type == 'O') {
                        char line[MAX_LINE_LENGTH];
                        int copy = length;
                        if (copy > MAX_LINE_LENGTH - 1) copy = MAX_LINE_LENGTH - 1;
                        memcpy(line, tab->pbuf + used + 3, (size_t)copy);
                        line[copy] = '\0';
                        add_line(tab, line);
                    } else {

                        char payload[512];
                        int copy = length;
                        if (copy > (int)sizeof(payload) - 1) copy = (int)sizeof(payload) - 1;
                        memcpy(payload, tab->pbuf + used + 3, (size_t)copy);
                        payload[copy] = '\0';
                        char *tab_mark = strchr(payload, '\t');
                        if (tab_mark != NULL) {
                            *tab_mark = '\0';
                            snprintf(tab->cwd, sizeof(tab->cwd), "%s", payload);
                            tab->continuing = (tab_mark[1] == '1');
                        }
                    }
                    used += 3 + length;
                } else if (type == 'K') {
                    used += 1;
                    buffer_clear(tab);
                } else if (type == 'E') {
                    used += 1;
                    tab->want_close = 1;
                } else {
                    used += 1;
                }
            }
            memmove(tab->pbuf, tab->pbuf + used,
                    (size_t)(tab->pbuf_len - used));
            tab->pbuf_len -= used;
            continue;
        }
        if (n == 0) {

            close(tab->proto_out_fd);
            tab->proto_out_fd = -1;
            if (!tab->worker_dead) {
                tab->worker_dead = 1;
                add_line(tab, "--- this tab's process has exited ---");
                if (tab->worker_pid > 0) {
                    waitpid(tab->worker_pid, NULL, WNOHANG);
                    tab->worker_pid = -1;
                }
            }
            return;
        }

        return;
    }
}

void add_dying(pid_t pid)
{
    if (pid <= 0) return;
    if (dying_count >= MAX_DYING) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return;
    }
    dying_workers[dying_count].pid = pid;
    dying_workers[dying_count].deadline = time(NULL) + 1;
    dying_workers[dying_count].stage = 0;
    dying_count++;
}

void sweep_dying(void)
{
    time_t now = time(NULL);
    for (int i = 0; i < dying_count; i++) {
        int status;
        pid_t result = waitpid(dying_workers[i].pid, &status, WNOHANG);
        if (result == dying_workers[i].pid) {
            dying_workers[i] = dying_workers[dying_count - 1];
            dying_count--;
            i--;
            continue;
        }
        if (now >= dying_workers[i].deadline) {
            if (dying_workers[i].stage == 0) {
                kill(dying_workers[i].pid, SIGTERM);
                dying_workers[i].stage = 1;
                dying_workers[i].deadline = now + 1;
            } else if (dying_workers[i].stage == 1) {
                kill(dying_workers[i].pid, SIGKILL);
                dying_workers[i].stage = 2;
                dying_workers[i].deadline = now + 1;
            }
        }
    }
}
