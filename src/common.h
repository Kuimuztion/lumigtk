/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LUMIGTK_COMMON_H
#define LUMIGTK_COMMON_H

#include <stdbool.h>
#include <stddef.h>

#define APP_NAME	"lumigtk"
#define TMP_VIDEO_DIR	"/tmp/lumigtk/video"
#define TMP_WORK_DIR	"/tmp/lumigtk/work"

/* 以下返回的字符串由调用者 g_free() */
char *config_dir(void);		/* ~/.config/lumigtk */
char *audio_dir(void);		/* ~/.config/lumigtk/audio */
char *cookies_path(void);	/* ~/.config/lumigtk/cookies.txt，不存在则 NULL */
char *tail_text(const char *s, size_t max);
char *error_message(const char *prefix, const char *err);
char *last_line(const char *s);

/* argv 以 NULL 结尾；out/err 返回后由调用者 g_free() */
bool run_command(const char **argv, char **out, char **err);

/* 逐行读取子进程输出（stdout 与 stderr 合并），每行回调 on_line。
 * on_line 在调用线程中执行。tail 返回末尾约 400 字节，由调用者 g_free()。 */
typedef void (*line_fn)(const char *line, void *data);
bool run_streamed(const char **argv, line_fn on_line, void *data, char **tail);

#endif
