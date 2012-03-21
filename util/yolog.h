/*
 * yobot_log.h
 *
 *  Created on: Aug 19, 2010
 *      Author: mordy
 */

#ifndef YOBOT_LOG_H_
#define YOBOT_LOG_H_

/*YOBOT_ is here because the actual source file still uses the old name*/
typedef enum {
	YOLOG_DEBUG 	= 1,	YOBOT_LOG_DEBUG = 1,
	YOLOG_INFO		= 2,	YOBOT_LOG_INFO	= 2,
	YOLOG_WARN		= 3,	YOBOT_LOG_WARN	= 3,
	YOLOG_ERROR		= 4,	YOBOT_LOG_ERROR	= 4,
	YOLOG_CRIT		= 5,	YOBOT_LOG_CRIT	= 5
} yobot_log_level;

typedef struct {
	char *prefix;
	int level;
} yobot_log_s;

#define YOLOG_PRIV_NAME yobot_log_params

#ifdef YOLOG_USE_EXTERN
extern yobot_log_s yobotproto_log_params;
#else
/*Call this for every file, to initialize the private logging module*/
/*Thus e.g.:
 * #include <yolog.h>
 * YOLOG_STATIC_INIT("My Module", YOBOT_LOG_CRIT);
 */
#define YOLOG_STATIC_INIT(pfix, lvl) \
	static yobot_log_s YOLOG_PRIV_NAME = { \
		.prefix = pfix, \
		.level = lvl \
	};
#endif

void yobot_logger(yobot_log_s logparams, yobot_log_level level, int line, const char *fn, const char *fmt, ...);

#define __logwrap(level, fmt, ...) yobot_logger(YOLOG_PRIV_NAME, level, __LINE__, __func__, fmt, ## __VA_ARGS__)

#define yolog_info(fmt, ...) __logwrap(YOBOT_LOG_INFO, fmt, ## __VA_ARGS__)
#define yolog_debug(fmt, ...) __logwrap(YOBOT_LOG_DEBUG, fmt, ## __VA_ARGS__)
#define yolog_warn(fmt, ...) __logwrap(YOBOT_LOG_WARN, fmt, ## __VA_ARGS__)
#define yolog_err(fmt, ...) __logwrap(YOBOT_LOG_ERROR, fmt, ## __VA_ARGS__)
#define yolog_crit(fmt, ...) __logwrap(YOBOT_LOG_CRIT, fmt, ## __VA_ARGS__)
#endif /* YOBOT_LOG_H_ */
