#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include "yolog.h"

//#if defined(unix) || defined(__unix) || defined(__unix__)
#define HAVE_CURSES 1
#include <curses.h>
#include <term.h>
#include <unistd.h>
//#endif
/*just some macros*/
#define _FG "3"
#define _BG "4"
#define _BRIGHT_FG "1"
#define _INTENSE_FG "9"
#define _DIM_FG "2"
#define _YELLOW "3"
#define _WHITE "7"
#define _MAGENTA "5"
#define _CYAN "6"
#define _BLUE "4"
#define _GREEN "2"
#define _RED "1"
#define _BLACK "0"
/*Logging subsystem*/

#define __loggerlogwrap(level, fmt, ...) yobot_logger(loggerparams_internal, level, \
		__LINE__, __func__, fmt, ## __VA_ARGS__)
static yobot_log_s loggerparams_internal = {
		"logger",
		1
};

static char *title_fmt = "", *reset_fmt = "";

#ifdef HAVE_CURSES
static int use_escapes = -1;
static void _init_color_logging(void) {
	if (use_escapes < 0) {
		if (!isatty(STDOUT_FILENO)) {
			use_escapes = 0;
			__loggerlogwrap(YOBOT_LOG_WARN, "no tty detected");
			return;
		}
		int erret;
		if (setupterm(NULL, STDOUT_FILENO, &erret) != OK) {
			use_escapes = 0;
			__loggerlogwrap(YOBOT_LOG_ERROR, "setupterm returned ERR");
			return;
		}
		erret = tigetnum("colors");
		if (erret <= 0) {
			use_escapes = 0;
			__loggerlogwrap(YOBOT_LOG_WARN, "couldn't find color caps for terminal");
			return;
		} else {
			if (erret >= 16) {
				title_fmt = "\033[" _INTENSE_FG _MAGENTA "m";
				use_escapes = 1;
			} else if (erret >= 8) {
				title_fmt = "\033[" _BRIGHT_FG ";" _FG _MAGENTA "m";
				use_escapes = 1;
			} else /*less than eight colors*/ {
				use_escapes = 0;
				__loggerlogwrap(YOBOT_LOG_WARN, "too few colors supported by terminal. not doing logging");
			}
		}
		if(use_escapes) {
			reset_fmt = "\033[0m";
			__loggerlogwrap(YOBOT_LOG_INFO, "using color escapes for logging");
		}
	}
}
#else
#define _init_color_logging() ;
static int use_escapes = 0;
#endif

void yobot_logger(yobot_log_s logparams, yobot_log_level level, int line,
		const char *fn, const char *fmt, ...) {
	if (logparams.level > level) {
//		printf("level failed");
		return;
	}
	if(use_escapes < 0) {
		_init_color_logging();
	}
	va_list ap;
	va_start(ap, fmt);
	char *line_fmt = "";
	if (use_escapes) {
		switch (level) {
		case YOBOT_LOG_CRIT:
		case YOBOT_LOG_ERROR: {
			line_fmt = "\033[" _BRIGHT_FG ";" _FG _RED "m";
			break;
		}
		case YOBOT_LOG_WARN: {
			line_fmt = "\033[" _FG _YELLOW "m";
			break;
		}
		case YOBOT_LOG_DEBUG: {
			line_fmt = "\033[" _DIM_FG ";" _FG _WHITE "m";
			break;
		}
		default:
			line_fmt = "";
			break;
		}
	}
	flockfile(stdout);
	printf("[%s%s%s] ", title_fmt, logparams.prefix, reset_fmt);
#ifdef YOLOG_TIME
	printf(" %f ", (float)clock()/CLOCKS_PER_SEC);
#endif
	printf("%s", line_fmt);
	printf("%s:%d ", fn, line);
	vprintf(fmt, ap);
	printf("%s", reset_fmt);
	printf("\n");
	fflush(NULL);
	funlockfile(stdout);
	va_end(ap);
}
