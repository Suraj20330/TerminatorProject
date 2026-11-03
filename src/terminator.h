#ifndef TERMINATOR_H
#define TERMINATOR_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#undef MAX_INPUT

#define MAX_TABS 10
#define MAX_INPUT 1024
#define MAX_LINES 500
#define MAX_LINE_LENGTH 1024
#define MAX_ARGUMENTS 64
#define MAX_SEGMENTS 16
#define MAX_PENDING 8192
#define MAX_TOKEN_STORAGE 32768
#define MAX_WATCHES 8
#define MAX_JOBS 16
#define MAX_SCRIPT_VARS 64
#define SCRIPT_VAR_NAME 64
#define SCRIPT_VAR_VALUE 1024
#define MAX_SCRIPT_LINES 1024
#define SCRIPT_DEFERRED_MAX 16
#define MAX_SCRIPT_DEPTH 16
#define MW_RERUN_SECONDS 2
#define MW_SEPARATOR_LINE "----------------------------------------------------"
#define MAX_BINDINGS 64
#define MAX_CLIPBOARD 8192
#define HIST_MAX 10000
#define HIST_LEN 1024
#define PROTO_IN_FD 3
#define PROTO_OUT_FD 4
#define VISIBLE_LINES 25
#define WINDOW_WIDTH 900
#define WINDOW_HEIGHT 600
#define LINE_HEIGHT 28
#define JOB_RUNNING_BG 1
#define JOB_STOPPED 2
#define TAB_QUEUE_SIZE 8
#define COMPL_MAX 64
#define MAX_DYING 32
#define SCAN_NORMAL   0
#define SCAN_SINGLE   1
#define SCAN_DOUBLE   2
#define SCAN_BACKTICK 3
#define DEFAULT_BINDING_COUNT \
    ((int)(sizeof(DEFAULT_BINDINGS) / sizeof(DEFAULT_BINDINGS[0])))

typedef struct {
    char cmd[MAX_INPUT];
    pid_t pids[MAX_SEGMENTS];
    int pid_count;
    int read_fd;
    char partial[2048];
    int partial_len;
    int state;
    time_t next_run;
    time_t launched_at;
    char *cycle_out;
    size_t cycle_len;
    size_t cycle_cap;
    int frame_ready;
} Watch;



typedef struct {
    int id;
    pid_t pids[MAX_SEGMENTS];
    int pid_count;
    pid_t last_pid;
    pid_t pgid;
    int read_fd;
    char partial[2048];
    int partial_len;
    char cmd[256];
    int state;
    int last_status_known;
    int last_status;
    int pids_reaped;
} Job;

typedef struct {
    pthread_mutex_t mutex;
    sem_t psem;
    int count;
    char items[HIST_MAX][HIST_LEN];
} SharedHistory;

enum {
    MSG_LINE = 1,
    MSG_INTERRUPT,
    MSG_SUSPEND,
    MSG_QUIT
};

typedef struct {
    int type;
    char text[MAX_INPUT];
} TabMsg;

typedef struct {
    char name[256];
    int is_dir;
} ComplMatch;

typedef struct {
    char input[MAX_INPUT];
    int input_length;

    char lines[MAX_LINES][MAX_LINE_LENGTH];
    int line_count;
    int scroll_position;

    int history_position;

    int output_fd;
    pid_t child_pid;

    pid_t pipe_pids[MAX_SEGMENTS];
    int pipe_count;
    pid_t last_pid;

    int active;

    char cwd[512];
    char previous_cwd[512];
    int last_exit_status;

    char pending_line[MAX_PENDING];
    int continuing;

    int cursor;
    char saved_input[MAX_INPUT];
    int saved_valid;

    int searching;
    char search_stash[MAX_INPUT];
    int search_stash_cursor;

    int completing;
    int compl_start;
    int compl_len;
    char compl_word[MAX_INPUT];
    int compl_count;
    ComplMatch compl_matches[COMPL_MAX];

    char var_names[MAX_SCRIPT_VARS][SCRIPT_VAR_NAME];
    char var_values[MAX_SCRIPT_VARS][SCRIPT_VAR_VALUE];
    int var_count;
    char pos_args[10][MAX_INPUT];
    int pos_count;
    int script_sink;
    char deferred[SCRIPT_DEFERRED_MAX][MAX_INPUT];
    int def_head;
    int def_tail;
    int def_count;
    char script_proto_left[2048];
    int script_proto_left_len;

   Watch watches[MAX_WATCHES];
   int watch_count;
   int watch_active;
   int watch_cursor;


    Job jobs[MAX_JOBS];
    int job_count;
    char fg_cmd[256];
    pid_t fg_pgid;
    int fg_preset_valid;
    int fg_preset_code;

    pthread_mutex_t buffer_lock;
    pthread_t worker_thread;
    int worker_started;
    pid_t worker_pid;
    int proto_in_w;
    int proto_out_fd;
    int worker_dead;
    char pbuf[16384];
    int pbuf_len;
    int wake_r;
    int wake_w;
    TabMsg queue[TAB_QUEUE_SIZE];
    pthread_mutex_t q_lock;
    int q_head;
    int q_tail;
    int q_count;
    int want_close;
    int gui_dirty;
} TerminalTab;

typedef struct { pid_t pid; time_t deadline; int stage; } DyingWorker;

typedef struct {
    const char *input_file;
    const char *output_file;
    const char *error_file;
    int append;
} Redirection;

typedef struct {
    char **argv;
    int argc;
    Redirection redir;
} Segment;

typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} StringBuilder;

typedef struct {
    int stop;
} ScriptCtx;

enum {
    ACT_NONE = 0,
    ACT_NEW_TAB,
    ACT_CLOSE_TAB,
    ACT_CLEAR_SCREEN,
    ACT_INTERRUPT,
    ACT_EOF_OR_DELETE,
    ACT_COPY,
    ACT_PASTE,
    ACT_CURSOR_LEFT,
    ACT_CURSOR_RIGHT,
    ACT_CURSOR_START,
    ACT_CURSOR_END,
    ACT_HISTORY_PREV,
    ACT_HISTORY_NEXT,
    ACT_DELETE_TO_START,
    ACT_DELETE_TO_END,
    ACT_DELETE_WORD,
    ACT_SUSPEND,
    ACT_SCROLL_UP,
    ACT_SCROLL_DOWN,
    ACT_SEARCH_HISTORY,
    ACT_COUNT
};

enum {
    ACT_OK = 0,
    ACT_WANT_NEW_TAB,
    ACT_WANT_CLOSE_TAB,
    ACT_WANT_COPY,
    ACT_WANT_PASTE,

    ACT_WANT_INTERRUPT,
    ACT_WANT_SUSPEND
};

typedef struct {
    KeySym keysym;
    unsigned int mods;
    int action;
} Binding;

typedef struct {
    const char *name;
    int action;
} ActionName;


extern Display *display;
extern Window window;
extern GC graphics;
extern TerminalTab tabs[MAX_TABS];
extern int active_tab;
extern int tab_count;
extern int running;
extern Atom close_message;
extern int g_process_mode;
extern int g_am_worker_process;
extern SharedHistory *hist_store;
extern SharedHistory hist_static_store;
extern int gui_wake_r;
extern int gui_wake_w;
extern DyingWorker dying_workers[MAX_DYING];
extern int dying_count;
extern XFontStruct *font_info;
extern char clipboard_text[MAX_CLIPBOARD];
extern int clipboard_owned;
extern Atom xa_clipboard, xa_utf8_string, xa_targets, xa_paste_atom;
extern int tab_label_left[MAX_TABS];
extern int tab_label_right[MAX_TABS];
extern int plus_label_left;
extern int plus_label_right;
extern char hist_file_path[1024];
extern int g_script_depth;
extern const Binding DEFAULT_BINDINGS[22];
extern Binding key_bindings[MAX_BINDINGS];
extern int binding_count;
extern int rc_warning_count;
extern const ActionName ACTION_NAMES[20];

void add_line(TerminalTab *tab, const char *text);
void add_output(TerminalTab *tab, const char *text);
void buffer_clear(TerminalTab *tab);
void hist_lock(void);
void hist_unlock(void);
void hist_store_insert_locked(const char *text);
void hist_add(const char *text);
int hist_count(void);
int hist_get(int index, char *out);
void hist_file_path_init(void);
void hist_file_append(const char *text);
void hist_file_load(void);
void init_history_store(void);
void initialize_tab(TerminalTab *tab);
void draw_text(int x, int y, const char *text);
void draw_window(void);
void tok_add_char(char **out, const char *limit, char c);
const char *script_lookup(TerminalTab *tab, const char *name);
void script_var_set(TerminalTab *tab, const char *name,
                           const char *value);
void set_script_positional(TerminalTab *tab, const char *zero,
                                  char *args[], int arg_count);
void tok_add_text(char **out, const char *limit, const char *text);
int is_name_start(char c);
int is_name_char(char c);
void expand_dollar(TerminalTab *tab, const char *line, int *pos,
                          char **out, const char *limit);
int parse_tokens(TerminalTab *tab, char *line, char *arguments[], char *storage);
void save_exit_status(TerminalTab *tab, int wait_status);
void scan_line(const char *text, int *is_complete, int *trailing_backslash,
                      int *backslash_in_quotes);
int line_is_complete(const char *text);
int ends_with_continuation_backslash(const char *text);
int ends_with_quoted_continuation_backslash(const char *text);
const char *cd_target(TerminalTab *tab, char *arguments[],
                             int argument_count,
                             char *expanded, size_t expanded_size);
void builtin_cd(TerminalTab *tab, char *arguments[], int argument_count);
int classify_redirection(char *token, const char **attached);
int extract_redirections(char *arguments[], int *argument_count_io,
                                Redirection *redirection,
                                char *bad_operator, size_t bad_operator_size);
int split_pipeline(char *arguments[], int argument_count,
                          Segment segments[], int max_segments,
                          char *bad_operator, size_t bad_operator_size);
int apply_redirections(Redirection *redirection);
int builtin_to_fd(TerminalTab *tab, char *arguments[],
                         int argument_count);
void exec_or_report(TerminalTab *tab, char *arguments[]);
int launch_command_into(TerminalTab *tab, char *arguments[],
                               int argument_count, Redirection *redirection,
                               int run_builtin, pid_t *pid_out);
void launch_command(TerminalTab *tab, char *arguments[],
                           int argument_count, Redirection *redirection,
                           int run_builtin);
int launch_pipeline_into(TerminalTab *tab, Segment segments[],
                                int segment_count, pid_t pids_out[],
                                int *forked_out);
void launch_pipeline(TerminalTab *tab, Segment segments[],
                            int segment_count);
void reap_pipeline(TerminalTab *tab);
void mw_report_usage(TerminalTab *tab);
int mw_parse_commands(const char *text,
                             char commands[][MAX_INPUT],
                             int max_commands);
void launch_watch_cycle(TerminalTab *tab, Watch *watch);
void watch_sweep(Watch *watch);
void drain_watch(TerminalTab *tab, Watch *watch);
int service_multiwatch_tab(TerminalTab *tab);
void stop_multiwatch(TerminalTab *tab);
void start_multiwatch(TerminalTab *tab, const char *line);
int run_redirection_setup(TerminalTab *tab, Redirection *redirection);
int status_to_code(int wait_status);
int foreground_running(TerminalTab *tab);
void signal_group_or_each(pid_t pgid, pid_t pids[], int pid_count,
                                 int signal_number);
void signal_foreground(TerminalTab *tab, int signal_number);
void signal_job(Job *job, int signal_number);
void make_cmd_text(char *destination, size_t size, const char *line);
Job *add_job(TerminalTab *tab, pid_t pids[], int pid_count,
                    int read_fd, const char *cmd_text, int state);
void remove_job(TerminalTab *tab, int index);
void print_job_start_line(TerminalTab *tab, Job *job);
void format_job_line(TerminalTab *tab, int index,
                            char *out, size_t size);
void jobs_print_fd(TerminalTab *tab);
void builtin_jobs(TerminalTab *tab);
int job_target_index(TerminalTab *tab, char *arguments[],
                            int argument_count, const char *builtin);
void builtin_bg(TerminalTab *tab, char *arguments[],
                       int argument_count);
void builtin_fg(TerminalTab *tab, char *arguments[],
                       int argument_count);
void suspend_foreground(TerminalTab *tab);
void drain_job(TerminalTab *tab, Job *job);
int service_jobs_tab(TerminalTab *tab);
void kill_all_jobs(TerminalTab *tab);
void execute_line(TerminalTab *tab, char *line);
void sb_init(StringBuilder *sb);
void sb_add_char(StringBuilder *sb, char c);
void sb_add_text(StringBuilder *sb, const char *text);
void sb_add_bytes(StringBuilder *sb, const char *data, size_t count);
char *copy_range(const char *text, int start, int end);
int find_matching_paren(const char *text, int open_index);
void run_pipeline_sync(TerminalTab *tab, Segment segments[],
                              int segment_count);
char *run_and_capture(TerminalTab *tab, const char *line);
char *expand_one_pass(TerminalTab *tab, const char *line, int *changed);
char *expand_substitutions(TerminalTab *tab, const char *line);
char *expand_globs(TerminalTab *tab, const char *line);

void pending_append(TerminalTab *tab, const char *text);
void handle_enter(TerminalTab *tab, const char *segment_text);
void script_strip_comment(char *line);
void script_trim(char *line);
void script_first_word(const char *line, char *word, size_t size,
                              int *quoted);
int script_words(const char *text, char words[][128], int max_words);
void script_condition_text(const char *line, char *out, size_t size);
int script_ends_with_then(const char *line);
int eval_test(TerminalTab *tab, char *tokens[], int count);
void deferred_push(TerminalTab *tab, const char *text);
int deferred_pop(TerminalTab *tab, char *out);
void drain_deferred_lines(TerminalTab *tab);
void wait_foreground(TerminalTab *tab, int watch_messages,
                            int *aborted, int *quit_seen);
int script_read_file(TerminalTab *tab, const char *path,
                            char ***out_lines, int *out_count);
int script_validate_if_line(char **lines, int count, int index,
                                   char *detail, size_t detail_size);
int script_validate(TerminalTab *tab, char **lines, int count,
                           const char *path);
int script_next_boundary(char **lines, int count, int from,
                                char *kind);
int script_eval_condition(TerminalTab *tab, const char *line);
int script_exec_if(TerminalTab *tab, char **lines, int count,
                          int if_index, ScriptCtx *ctx, int watch_messages);
void script_exec_range(TerminalTab *tab, char **lines, int start,
                              int end, ScriptCtx *ctx, int watch_messages);
int run_script_file(TerminalTab *tab, const char *path,
                           int watch_messages, int reset_status);
void builtin_run_script(TerminalTab *tab, char *arguments[],
                               int argument_count, int is_bash);
void read_command_output(TerminalTab *tab);
void open_new_tab(void);
void close_tab_at(int slot);
void close_active_tab(void);
void switch_to_visible_tab(int visible_index);
int action_from_name(const char *name);
const char *action_name(int action);
int find_action(KeySym keysym, unsigned int state);
char *trim(char *text);
int ci_equal(const char *a, const char *b);
int parse_key_token(const char *token, KeySym *out);
int parse_combo(char *text, KeySym *keysym_out, unsigned int *mods_out);
void remove_action_rows(Binding out[], int *count, int action);
void restore_default_rows(Binding out[], int *count, int action);
void load_bindings(const char *path, Binding out[], int *count, int *warnings);
void input_set(TerminalTab *tab, const char *text);
void input_insert(TerminalTab *tab, const char *text, int count);
void input_delete_backward(TerminalTab *tab);
void input_delete_forward(TerminalTab *tab);
void history_move(TerminalTab *tab, int direction);
int apply_action(TerminalTab *tab, int action);
int longest_common_substring(const char *a, const char *b);
int shares_three_gram(const char *term, const char *entry);
void search_restore_input(TerminalTab *tab);
void run_history_search(TerminalTab *tab, const char *term);
int compl_name_cmp(const void *a, const void *b);
void input_replace_range(TerminalTab *tab, int start, int end,
                                const char *replacement);
void handle_tab_completion(TerminalTab *tab);
int completion_pick(TerminalTab *tab, int pick);
void clipboard_copy_input_line(void);
void paste_text_into_active_tab(const char *text, long length);
void clipboard_request_paste(void);
void serve_selection_request(XSelectionRequestEvent *request);
void handle_selection_notify(XSelectionEvent *event);
void handle_key_press(XKeyEvent *key_event);
int write_all_fd(int fd, const char *data, int length);
void frame_write(int fd, char type, const char *payload, int payload_length);
void proto_send_frame(char type, const char *payload, int payload_length);
void proto_send_state(TerminalTab *tab);
void poke_gui(void);
void queue_push(TerminalTab *tab, int type, const char *text);
int queue_pop(TerminalTab *tab, TabMsg *out);
void gui_send_line(TerminalTab *tab, const char *text);
void gui_send_interrupt(TerminalTab *tab);
void gui_send_suspend(TerminalTab *tab);
void request_tab_close(TerminalTab *tab);
void engine_interrupt(TerminalTab *tab);
void engine_teardown(TerminalTab *tab);
void shell_worker_main(TerminalTab *tab);
void *worker_thread_main(void *argument);
int spawn_tab_worker(int slot);
void drain_proto(TerminalTab *tab);
void add_dying(pid_t pid);
void sweep_dying(void);

#endif