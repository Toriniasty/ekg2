#include "ekg2-config.h"

#ifndef __FreeBSD__
#define _XOPEN_SOURCE 600
#define __EXTENSIONS__
#endif

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <ekg/plugins.h>
#include <ekg/stuff.h>
#include <ekg/vars.h>
#include <ekg/windows.h>
#include <ekg/themes.h>
#include <ekg/xmalloc.h>

#include <readline.h>
#include "ui-readline.h"

static int readline_theme_init();
PLUGIN_DEFINE(readline, PLUGIN_UI, readline_theme_init);

int config_ctrld_quits = 1;

/*
 * sigint_handler() //XXX może wywalać 
 *
 * obsługuje wciśnięcie Ctrl-C.
 */
static void sigint_handler()
{
	rl_delete_text(0, rl_end);
	rl_point = rl_end = 0;
	putchar('\n');
	rl_forced_update_display();
}

/*
 * sigcont_handler() //XXX może wywalać
 *
 * osługuje powrót z tła poleceniem ,,fg'', żeby odświeżyć ekran.
 */
static void sigcont_handler()
{
	rl_forced_update_display();
}

/*
 * sigwinch_handler()
 *
 * obsługuje zmianę rozmiaru okna.
 */
#ifdef SIGWINCH
static void sigwinch_handler()
{
	ui_need_refresh = 1;
}
#endif

static int readline_theme_init() {
#ifndef NO_DEFAULT_THEME
	/* prompty dla ui-readline */
	format_add("readline_prompt", "% ", 1);
	format_add("readline_prompt_away", "/ ", 1);
	format_add("readline_prompt_invisible", ". ", 1);
	format_add("readline_prompt_query", "%1> ", 1);
	format_add("readline_prompt_win", "%1%% ", 1);
	format_add("readline_prompt_away_win", "%1/ ", 1);
	format_add("readline_prompt_invisible_win", "%1. ", 1);
	format_add("readline_prompt_query_win", "%2:%1> ", 1);
	format_add("readline_prompt_win_act", "%1 (act/%2)%% ", 1);
	format_add("readline_prompt_away_win_act", "%1 (act/%2)/ ", 1);
	format_add("readline_prompt_invisible_win_act", "%1 (act/%2). ", 1);
	format_add("readline_prompt_query_win_act", "%2:%1 (act/%3)> ", 1);
	format_add("readline_more", _("-- Press Enter to continue or Ctrl-D to break --"), 1);
#endif
	return 0;
}

QUERY(readline_ui_window_new) { /* window_add() */
	window_t *w = *(va_arg(ap, window_t **));
	w->private = xmalloc(sizeof(readline_window_t));
	return 0;
}

QUERY(readline_ui_window_kill) { /* window_free */
	window_t *w = *(va_arg(ap, window_t **));
	readline_window_t *r = w->private;
	int i;

	for (i = 0; i < MAX_LINES_PER_SCREEN; i++) {
		xfree(r->line[i]);
		r->line[i] = NULL;
	}
	xfree(r);
	w->private = NULL;
	return 0;
}

QUERY(readline_ui_window_switch) { /* window_switch */
	window_t *w = *(va_arg(ap, window_t **));
	window_current = w;
	w->act = 0;
	window_refresh();
#ifdef HAVE_RL_SET_PROMPT
	rl_set_prompt((char *) current_prompt());
#else
	rl_expand_prompt((char *) current_prompt());
#endif
	rl_initialize();
	return 0;
}

char *readline_change_string_t_back_to_char(char *str, short *attr) {
        string_t st = string_init(NULL);
        int prev = -1, prevbold = 0, prevblink = 0;
        int i;

/* ripped from perl plugin */
/* rewrite */
        for (i=0; i < strlen(str); i++) {
                short chattr = attr[i];
                int bold = 0, blink = 0;

                if (chattr & 64)  bold = 1;
		if (chattr & 256) blink = 1;

/*		if (chattr & 512) underline = 1;
		if (chattr & 1024) reverse = 1;  */
		if (!blink && prevblink != blink && prev != -1) {
			prev = -1;
			string_append(st, "%n"); /* turn off blinking */
		}
                if (blink && (prevblink != blink || prev == -1))
			string_append(st, "%i"); /* turn on blinking */

                if (!(chattr & 128) && (prev != (chattr & 7) || prevbold != bold)) { /* change color/bold */
			string_append_c(st, '%');
                        switch (chattr & 7) {
				case (0): string_append_c(st, (bold) ? 'K' : 'k'); break;
				case (1): string_append_c(st, (bold) ? 'R' : 'r'); break;
				case (2): string_append_c(st, (bold) ? 'G' : 'g'); break;
				case (3): string_append_c(st, (bold) ? 'Y' : 'y'); break;
				case (4): string_append_c(st, (bold) ? 'B' : 'b'); break;
				case (5): string_append_c(st, (bold) ? 'M' : 'm'); break; /* | fioletowy     | %m/%p  | %M/%P | %q  | */
				case (6): string_append_c(st, (bold) ? 'C' : 'c'); break;
				case (7): string_append_c(st, (bold) ? 'W' : 'w'); break;
                        }
                        prev = (chattr & 7);
                } else if ((chattr & 128) && (prev != -1 || (!bold && prevbold != bold))) { /* reset all attributes */
                       	string_append(st, "%n");
			prev = -1;
                }

		if (prev == -1 && bold && bold != prevbold) {	/* mh ? */
			string_append(st, "%T");
		}

                string_append_c(st, str[i]);
		prevblink = blink;
		prevbold  = bold;
        }
	string_append(st, "%n");	/* reset at end ? */
	return string_free(st, 0);
}

char *readline_ui_window_print_helper(char *str, short *attr) {
	char *ascii = readline_change_string_t_back_to_char(str, attr);
	char *colorful = format_string(ascii);

	xfree(ascii);
	return colorful;
}

QUERY(readline_ui_window_print) {
	window_t *w = *(va_arg(ap, window_t **));
	fstring_t *l = *(va_arg(ap, fstring_t **));
	char *str = readline_ui_window_print_helper(l->str, l->attr);

	ui_readline_print(w, 1, str);
	xfree(str);
	return 0;
}
QUERY(readline_variable_changed) {
	char *name = *(va_arg(ap, char**));
	if (!xstrcasecmp(name, "sort_windows") && config_sort_windows) {
		list_t l;
		int id = 1;
		for (l = windows; l; l = l->next) {
			window_t *w = l->data;
			w->id = id++;
		}
	}
	return 0;
}

QUERY(readline_ui_window_clear) {
	int i;
	window_t *w = *(va_arg(ap, window_t **));
	readline_window_t *r = w->private;

	for (i = 0; i < MAX_LINES_PER_SCREEN; i++) {
		xfree(r->line[i]);
		r->line[i] = NULL;
	}
	window_refresh();
	return 0;
}

QUERY(ekg2_readline_loop) {
	while(ui_readline_loop());
	return -1;
}

QUERY(readline_ui_is_initialized) {
	int *tmp = va_arg(ap, int *);
	*tmp = 1;
	return 0;
}

QUERY(readline_beep) { /* ui_readline_beep() */
	printf("\a");
	fflush(stdout);
	return 0;
}

WATCHER(readline_watch_stdin) {
	return 0;
}
	
int readline_plugin_init(int prio) {
	char c;
	struct sigaction sa;
	list_t l;
	int is_UI = 0;

        query_emit(NULL, "ui-is-initialized", &is_UI);

        if (is_UI)
                return -1;

	plugin_register(&readline_plugin, prio);

	query_connect(&readline_plugin, "ui-beep", readline_beep, NULL);
	query_connect(&readline_plugin, "ui-is-initialized", readline_ui_is_initialized, NULL);
	query_connect(&readline_plugin, "ui-window-new", readline_ui_window_new, NULL);
	query_connect(&readline_plugin, "ui-window-switch", readline_ui_window_switch, NULL);
	query_connect(&readline_plugin, "ui-window-kill", readline_ui_window_kill, NULL);
	query_connect(&readline_plugin, "ui-window-print", readline_ui_window_print, NULL);
	query_connect(&readline_plugin, "ui-window-clear", readline_ui_window_clear, NULL);
	query_connect(&readline_plugin, "variable-changed", readline_variable_changed, NULL);
	query_connect(&readline_plugin, "ui-loop", ekg2_readline_loop, NULL);

	variable_add(&readline_plugin, TEXT("ctrld_quits"),  VAR_BOOL, 1, &config_ctrld_quits, NULL, NULL, NULL);

	watch_add(&readline_plugin, 0, WATCH_READ, readline_watch_stdin, NULL);

	for (l = windows; l; l = l->next) {
		window_t *w = l->data;
		w->private = xmalloc(sizeof(readline_window_t));
	}
	
	window_refresh();
	rl_initialize();
	
	rl_getc_function = my_getc;
	rl_event_hook	 = my_loop;
	rl_readline_name = "ekg2";
	
	rl_attempted_completion_function = (CPPFunction *) my_completion;
	rl_completion_entry_function = (void*) empty_generator;

	rl_set_key("\033[[A", binding_help, emacs_standard_keymap);
	rl_set_key("\033OP", binding_help, emacs_standard_keymap);
	rl_set_key("\033[11~", binding_help, emacs_standard_keymap);
	rl_set_key("\033[M", binding_help, emacs_standard_keymap);
	rl_set_key("\033[[B", binding_quick_list, emacs_standard_keymap);
	rl_set_key("\033OQ", binding_quick_list, emacs_standard_keymap);
	rl_set_key("\033[12~", binding_quick_list, emacs_standard_keymap);
	rl_set_key("\033[N", binding_quick_list, emacs_standard_keymap);
	
	//rl_set_key("\033[24~", binding_toggle_debug, emacs_standard_keymap);

	for (c = '0'; c <= '9'; c++)
		rl_bind_key_in_map(c, bind_handler_window, emacs_meta_keymap);

	memset(&sa, 0, sizeof(sa));

	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);

	sa.sa_handler = sigcont_handler;
	sigaction(SIGCONT, &sa, NULL);

#ifdef SIGWINCH
	sa.sa_handler = sigwinch_handler;
	sigaction(SIGWINCH, &sa, NULL);
#endif

	rl_get_screen_size(&screen_lines, &screen_columns);

	if (screen_lines < 1)
		screen_lines = 24;
	if (screen_columns < 1)
		screen_columns = 80;

	ui_screen_width = screen_columns;
	ui_screen_height = screen_lines;
	ui_need_refresh = 0;

	return 0;
}


static int readline_plugin_destroy() {
	plugin_unregister(&readline_plugin);
	return 0;

}
