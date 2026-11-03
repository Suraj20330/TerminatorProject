#include "terminator.h"

int action_from_name(const char *name)
{
    int count = (int)(sizeof(ACTION_NAMES) / sizeof(ACTION_NAMES[0]));
    for (int i = 0; i < count; i++)
        if (strcmp(ACTION_NAMES[i].name, name) == 0) return ACTION_NAMES[i].action;
    return ACT_NONE;
}

const char *action_name(int action)
{
    int count = (int)(sizeof(ACTION_NAMES) / sizeof(ACTION_NAMES[0]));
    for (int i = 0; i < count; i++)
        if (ACTION_NAMES[i].action == action) return ACTION_NAMES[i].name;
    return "?";
}

int find_action(KeySym keysym, unsigned int state)
{
    unsigned int mods = state & (ControlMask | ShiftMask | Mod1Mask);

    KeySym key = keysym;
    if (mods != 0 && key >= XK_A && key <= XK_Z)
        key = (KeySym)(key - XK_A + XK_a);

    for (int i = 0; i < binding_count; i++) {
        if (key_bindings[i].keysym == key && key_bindings[i].mods == mods)
            return key_bindings[i].action;
    }
    return ACT_NONE;
}

char *trim(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
        text++;
    size_t length = strlen(text);
    while (length > 0) {
        char last = text[length - 1];
        if (last != ' ' && last != '\t' && last != '\n' && last != '\r') break;
        text[--length] = '\0';
    }
    return text;
}

int ci_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int parse_key_token(const char *token, KeySym *out)
{
    static const struct { const char *name; KeySym keysym; } KEY_NAMES[] = {
        { "Left", XK_Left },       { "Right", XK_Right },
        { "Up", XK_Up },           { "Down", XK_Down },
        { "Home", XK_Home },       { "End", XK_End },
        { "PageUp", XK_Page_Up },  { "PageDown", XK_Page_Down },
        { "Space", XK_space },     { "Tab", XK_Tab },
    };

    if (strlen(token) == 1) {
        char one[2] = { token[0], '\0' };
        if (isalpha((unsigned char)one[0]))
            one[0] = (char)tolower((unsigned char)one[0]);
        KeySym keysym = XStringToKeysym(one);
        if (keysym == NoSymbol) return 0;
        *out = keysym;
        return 1;
    }

    int name_count = (int)(sizeof(KEY_NAMES) / sizeof(KEY_NAMES[0]));
    for (int i = 0; i < name_count; i++) {
        if (ci_equal(token, KEY_NAMES[i].name)) {
            *out = KEY_NAMES[i].keysym;
            return 1;
        }
    }

    if ((token[0] == 'F' || token[0] == 'f') && token[1] != '\0') {
        int value = 0;
        int all_digits = 1;
        for (int i = 1; token[i] != '\0'; i++) {
            if (token[i] < '0' || token[i] > '9') { all_digits = 0; break; }
            value = value * 10 + (token[i] - '0');
        }
        if (all_digits && value >= 1 && value <= 12) {
            *out = (KeySym)(XK_F1 + (value - 1));
            return 1;
        }
    }
    return 0;
}

int parse_combo(char *text, KeySym *keysym_out, unsigned int *mods_out)
{
    char tokens[5][64];
    int count = 0;
    const char *piece = text;

    while (1) {
        const char *plus = strchr(piece, '+');
        size_t length = plus != NULL ? (size_t)(plus - piece) : strlen(piece);
        if (count >= 5 || length >= sizeof(tokens[0])) return 0;
        memcpy(tokens[count], piece, length);
        tokens[count][length] = '\0';

        char *clean = trim(tokens[count]);
        memmove(tokens[count], clean, strlen(clean) + 1);
        count++;
        if (plus == NULL) break;
        piece = plus + 1;
    }

    unsigned int mods = 0;
    for (int i = 0; i + 1 < count; i++) {
        if (ci_equal(tokens[i], "Ctrl")) mods |= ControlMask;
        else if (ci_equal(tokens[i], "Alt")) mods |= Mod1Mask;
        else if (ci_equal(tokens[i], "Shift")) mods |= ShiftMask;
        else return 0;
    }

    if (!parse_key_token(tokens[count - 1], keysym_out)) return 0;
    *mods_out = mods;
    return 1;
}

void remove_action_rows(Binding out[], int *count, int action)
{
    int kept = 0;
    for (int i = 0; i < *count; i++)
        if (out[i].action != action) out[kept++] = out[i];
    *count = kept;
}

void restore_default_rows(Binding out[], int *count, int action)
{
    remove_action_rows(out, count, action);
    for (int i = 0; i < DEFAULT_BINDING_COUNT; i++) {
        if (DEFAULT_BINDINGS[i].action == action && *count < MAX_BINDINGS)
            out[(*count)++] = DEFAULT_BINDINGS[i];
    }
}

void load_bindings(const char *path, Binding out[], int *count, int *warnings)
{
    int n = 0;
    int problems = 0;

    for (int i = 0; i < DEFAULT_BINDING_COUNT && n < MAX_BINDINGS; i++)
        out[n++] = DEFAULT_BINDINGS[i];

    if (path != NULL) {
        FILE *file = fopen(path, "r");
        if (file != NULL) {

            int config_line[ACT_COUNT];
            for (int a = 0; a < ACT_COUNT; a++) config_line[a] = -1;

            char line[512];
            int line_number = 0;
            while (fgets(line, sizeof(line), file) != NULL) {
                line_number++;
                char *text = trim(line);
                if (text[0] == '\0' || text[0] == '#') continue;

                char *equals = strchr(text, '=');
                if (equals == NULL) {
                    fprintf(stderr, "terminator: %s line %d: not an "
                            "'action = keys' line - ignored\n",
                            path, line_number);
                    problems++;
                    continue;
                }
                *equals = '\0';
                char *name = trim(text);
                char *combo = trim(equals + 1);

                int action = action_from_name(name);
                if (action == ACT_NONE) {
                    fprintf(stderr, "terminator: %s line %d: unknown "
                            "action '%s' - line ignored\n",
                            path, line_number, name);
                    problems++;
                    continue;
                }

                KeySym keysym;
                unsigned int mods;
                if (!parse_combo(combo, &keysym, &mods)) {
                    fprintf(stderr, "terminator: %s line %d: bad key "
                            "combination '%s' - line ignored\n",
                            path, line_number, combo);
                    problems++;
                    continue;
                }

                remove_action_rows(out, &n, action);
                if (n < MAX_BINDINGS) {
                    out[n].keysym = keysym;
                    out[n].mods = mods;
                    out[n].action = action;
                    n++;
                }
                config_line[action] = line_number;
            }
            fclose(file);

            for (int pass = 0; pass < ACT_COUNT; pass++) {
                int restore[ACT_COUNT];
                int any = 0;
                for (int a = 0; a < ACT_COUNT; a++) restore[a] = 0;

                for (int i = 0; i < n; i++) {
                    for (int j = i + 1; j < n; j++) {
                        if (out[i].keysym == out[j].keysym &&
                            out[i].mods == out[j].mods &&
                            out[i].action != out[j].action) {
                            if (config_line[out[i].action] >= 0)
                                restore[out[i].action] = 1;
                            if (config_line[out[j].action] >= 0)
                                restore[out[j].action] = 1;
                        }
                    }
                }
                for (int a = 0; a < ACT_COUNT; a++) if (restore[a]) any = 1;
                if (!any) break;

                for (int a = 0; a < ACT_COUNT; a++) {
                    if (!restore[a]) continue;
                    fprintf(stderr, "terminator: %s line %d: binding "
                            "for '%s' conflicts with another binding "
                            "- keeping the default\n",
                            path, config_line[a], action_name(a));
                    problems++;
                    restore_default_rows(out, &n, a);
                    config_line[a] = -1;
                }
            }
        }
    }

    *count = n;
    *warnings = problems;
}

void input_set(TerminalTab *tab, const char *text)
{
    snprintf(tab->input, MAX_INPUT, "%s", text);
    tab->input_length = (int)strlen(tab->input);
    tab->cursor = tab->input_length;
}

void input_insert(TerminalTab *tab, const char *text, int count)
{
    if (count <= 0) return;
    int room = (MAX_INPUT - 1) - tab->input_length;
    if (count > room) count = room;
    if (count <= 0) return;

    memmove(tab->input + tab->cursor + count, tab->input + tab->cursor,
            (size_t)(tab->input_length - tab->cursor) + 1);
    memcpy(tab->input + tab->cursor, text, (size_t)count);
    tab->input_length += count;
    tab->cursor += count;
}

void input_delete_backward(TerminalTab *tab)
{
    if (tab->cursor <= 0) return;
    memmove(tab->input + tab->cursor - 1, tab->input + tab->cursor,
            (size_t)(tab->input_length - tab->cursor) + 1);
    tab->input_length--;
    tab->cursor--;
}

void input_delete_forward(TerminalTab *tab)
{
    if (tab->cursor >= tab->input_length) return;
    memmove(tab->input + tab->cursor, tab->input + tab->cursor + 1,
            (size_t)(tab->input_length - tab->cursor));
    tab->input_length--;
}

void history_move(TerminalTab *tab, int direction)
{
    int total = hist_count();
    if (direction < 0) {
        if (total == 0) return;
        if (tab->history_position == -1) {
            snprintf(tab->saved_input, MAX_INPUT, "%s", tab->input);
            tab->saved_valid = 1;
            tab->history_position = total - 1;
        } else if (tab->history_position > 0) {
            tab->history_position--;
        } else {
            return;
        }
        char entry[HIST_LEN];
        if (hist_get(tab->history_position, entry))
            input_set(tab, entry);
    } else {
        if (tab->history_position == -1) return;
        tab->history_position++;
        if (tab->history_position >= total) {
            tab->history_position = -1;
            input_set(tab, tab->saved_valid ? tab->saved_input : "");
            tab->saved_valid = 0;
        } else {
            char entry[HIST_LEN];
            if (hist_get(tab->history_position, entry))
                input_set(tab, entry);
        }
    }
}

int apply_action(TerminalTab *tab, int action)
{
    switch (action) {

    case ACT_NEW_TAB:   return ACT_WANT_NEW_TAB;
    case ACT_CLOSE_TAB: return ACT_WANT_CLOSE_TAB;
    case ACT_COPY:      return ACT_WANT_COPY;
    case ACT_PASTE:     return ACT_WANT_PASTE;

    case ACT_CLEAR_SCREEN:
        buffer_clear(tab);
        break;

    case ACT_INTERRUPT:

        return ACT_WANT_INTERRUPT;

    case ACT_SUSPEND:

        return ACT_WANT_SUSPEND;

    case ACT_EOF_OR_DELETE:
        if (tab->input_length == 0) return ACT_WANT_CLOSE_TAB;
        input_delete_forward(tab);
        break;

    case ACT_CURSOR_LEFT:
        if (tab->cursor > 0) tab->cursor--;
        break;

    case ACT_CURSOR_RIGHT:
        if (tab->cursor < tab->input_length) tab->cursor++;
        break;

    case ACT_CURSOR_START:
        tab->cursor = 0;
        break;

    case ACT_CURSOR_END:
        tab->cursor = tab->input_length;
        break;

    case ACT_HISTORY_PREV:
        history_move(tab, -1);
        break;

    case ACT_HISTORY_NEXT:
        history_move(tab, +1);
        break;

    case ACT_DELETE_TO_START: {
        if (tab->cursor > 0) {
            int tail = tab->input_length - tab->cursor;
            memmove(tab->input, tab->input + tab->cursor, (size_t)tail + 1);
            tab->input_length = tail;
            tab->cursor = 0;
        }
        break;
    }

    case ACT_DELETE_TO_END:
        tab->input[tab->cursor] = '\0';
        tab->input_length = tab->cursor;
        break;

    case ACT_DELETE_WORD: {
        int start = tab->cursor;
        while (start > 0 && tab->input[start - 1] == ' ') start--;
        while (start > 0 && tab->input[start - 1] != ' ') start--;
        if (start < tab->cursor) {
            int tail = tab->input_length - tab->cursor;
            memmove(tab->input + start, tab->input + tab->cursor,
                    (size_t)tail + 1);
            tab->input_length -= tab->cursor - start;
            tab->cursor = start;
        }
        break;
    }

    case ACT_SEARCH_HISTORY:

        if (!tab->searching) {
            tab->input[tab->input_length] = '\0';
            snprintf(tab->search_stash, sizeof(tab->search_stash),
                     "%s", tab->input);
            tab->search_stash_cursor = tab->cursor;
            tab->input_length = 0;
            tab->input[0] = '\0';
            tab->cursor = 0;
            tab->searching = 1;
            tab->history_position = -1;
            tab->saved_valid = 0;
        }
        break;

    case ACT_SCROLL_UP:
        if (tab->scroll_position < tab->line_count - 1) tab->scroll_position++;
        break;

    case ACT_SCROLL_DOWN:
        if (tab->scroll_position > 0) tab->scroll_position--;
        break;

    default:
        break;
    }
    return ACT_OK;
}

void clipboard_copy_input_line(void)
{
    TerminalTab *tab = &tabs[active_tab];
    if (display == NULL || tab->input_length == 0) return;

    snprintf(clipboard_text, sizeof(clipboard_text), "%s", tab->input);
    XSetSelectionOwner(display, xa_clipboard, window, CurrentTime);
    clipboard_owned = (XGetSelectionOwner(display, xa_clipboard) == window);
}

void paste_text_into_active_tab(const char *text, long length)
{
    char clean[MAX_CLIPBOARD];
    int out = 0;
    for (long i = 0; i < length && text[i] != '\0' && out < MAX_CLIPBOARD - 1; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n' || c == '\r' || c == '\t') {
            if (out == 0 || clean[out - 1] != ' ') clean[out++] = ' ';
        } else if (c >= 32 && c != 127) {
            clean[out++] = (char)c;
        }
    }
    if (out > 0) {
        input_insert(&tabs[active_tab], clean, out);
        tabs[active_tab].scroll_position = 0;
    }
}

void clipboard_request_paste(void)
{
    if (display == NULL) return;
    if (clipboard_owned) {
        paste_text_into_active_tab(clipboard_text,
                                   (long)strlen(clipboard_text));
        return;
    }
    XConvertSelection(display, xa_clipboard, xa_utf8_string,
                      xa_paste_atom, window, CurrentTime);
    XFlush(display);
}

void serve_selection_request(XSelectionRequestEvent *request)
{
    XEvent answer;
    memset(&answer, 0, sizeof(answer));
    answer.xselection.type = SelectionNotify;
    answer.xselection.display = request->display;
    answer.xselection.requestor = request->requestor;
    answer.xselection.selection = request->selection;
    answer.xselection.target = request->target;
    answer.xselection.time = request->time;

    Atom property = request->property;
    if (property == None) property = request->target;
    answer.xselection.property = property;

    if (request->target == xa_targets) {
        Atom supported[3] = { xa_targets, xa_utf8_string, XA_STRING };
        XChangeProperty(display, request->requestor, property, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)supported, 3);
    } else if (request->target == xa_utf8_string ||
               request->target == XA_STRING) {
        XChangeProperty(display, request->requestor, property,
                        request->target, 8, PropModeReplace,
                        (unsigned char *)clipboard_text,
                        (int)strlen(clipboard_text));
    } else {
        answer.xselection.property = None;
    }

    XSendEvent(display, request->requestor, False, 0, &answer);
    XFlush(display);
}

void handle_selection_notify(XSelectionEvent *event)
{
    if (event->property == None) return;

    Atom actual_type;
    int actual_format;
    unsigned long item_count, bytes_left;
    unsigned char *data = NULL;
    if (XGetWindowProperty(display, window, xa_paste_atom, 0,
                           MAX_CLIPBOARD / 4, False, AnyPropertyType,
                           &actual_type, &actual_format, &item_count,
                           &bytes_left, &data) == Success) {
        if (data != NULL) {
            long byte_count = (long)item_count * (actual_format / 8);
            paste_text_into_active_tab((const char *)data, byte_count);
            XFree(data);
        }
    }
    XDeleteProperty(display, window, xa_paste_atom);
}

void handle_key_press(XKeyEvent *key_event)
{
    TerminalTab *tab = &tabs[active_tab];

    KeySym key_symbol;
    char typed_text[32];
    int character_count = XLookupString(key_event, typed_text,
                                        sizeof(typed_text), &key_symbol, NULL);

    if ((key_event->state & Mod1Mask) && key_symbol >= XK_1 && key_symbol <= XK_9) {
        switch_to_visible_tab((int)(key_symbol - XK_1));
        return;
    }

if (key_symbol == XK_Return || key_symbol == XK_KP_Enter) {
    tab->completing = 0;
    tab->scroll_position = 0;


        if (tab->searching) {
            char term[MAX_INPUT];
            tab->input[tab->input_length] = '\0';
            snprintf(term, sizeof(term), "%s", tab->input);
            run_history_search(tab, term);
            search_restore_input(tab);
            return;
        }

        tab->input[tab->input_length] = '\0';
        gui_send_line(tab, tab->input);
        tab->input_length = 0;
        tab->input[0] = '\0';
        tab->cursor = 0;
        tab->history_position = -1;
        tab->saved_valid = 0;
        return;
    }

    if (key_symbol == XK_BackSpace) {
        input_delete_backward(tab);
        return;
    }
    if (key_symbol == XK_Delete) {
        input_delete_forward(tab);
        return;
    }

    if (tab->searching && key_symbol == XK_Escape) {
        search_restore_input(tab);
        return;
    }

    if (key_symbol == XK_Tab && !tab->searching) {
        handle_tab_completion(tab);
        return;
    }

    int action = find_action(key_symbol, key_event->state);
    if (action != ACT_NONE) {

        if (tab->searching) {
            if (action == ACT_INTERRUPT) search_restore_input(tab);
            else if (action == ACT_SEARCH_HISTORY) apply_action(tab, action);
            return;
        }
        int result = apply_action(tab, action);
        if (result == ACT_WANT_NEW_TAB) open_new_tab();
        else if (result == ACT_WANT_CLOSE_TAB) close_active_tab();
        else if (result == ACT_WANT_COPY) clipboard_copy_input_line();
        else if (result == ACT_WANT_PASTE) clipboard_request_paste();
        else if (result == ACT_WANT_INTERRUPT) {

    tab->input_length = 0;
    tab->input[0] = '\0';
    tab->cursor = 0;
    add_line(tab, "^C");
    gui_send_interrupt(tab);
}

else if (result == ACT_WANT_SUSPEND) { add_line(tab, "^Z"); gui_send_suspend(tab); }
        return;
    }

    if (tab->completing) {
        if (character_count > 0 && typed_text[0] >= '1' &&
            typed_text[0] <= '9') {
            if (completion_pick(tab, typed_text[0] - '0')) return;
        }
        tab->completing = 0;
    }

    if (character_count > 0 && typed_text[0] >= 32 && typed_text[0] != 127) {
        input_insert(tab, typed_text, character_count);
        tab->scroll_position = 0;
    }
}
