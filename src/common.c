/* SPDX-License-Identifier: GPL-3.0-only */
#include <glib.h>
#include <gio/gio.h>
#include <string.h>

#include "common.h"

char *config_dir(void)
{
	char *dir = g_build_filename(g_get_user_config_dir(), APP_NAME, NULL);

	g_mkdir_with_parents(dir, 0755);
	return dir;
}

char *audio_dir(void)
{
	char *base = config_dir();
	char *dir = g_build_filename(base, "audio", NULL);

	g_free(base);
	g_mkdir_with_parents(dir, 0755);
	return dir;
}

char *cookies_path(void)
{
	char *dir = config_dir();
	char *path = g_build_filename(dir, "cookies.txt", NULL);

	g_free(dir);
	if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
		return path;
	g_free(path);
	return NULL;
}

char *last_line(const char *s)
{
	gchar **lines = g_strsplit(s, "\n", -1);
	char *ret = g_strdup("");
	guint i;

	for (i = 0; lines[i]; ++i) {
		char *t = g_strstrip(lines[i]);

		if (*t) {
			g_free(ret);
			ret = g_strdup(t);
		}
	}
	g_strfreev(lines);
	return ret;
}

char *tail_text(const char *s, size_t max)
{
	size_t len = strlen(s);
	const char *p = s;

	if (len > max) {
		p = s + len - max;
		/* 跳过 UTF-8 续字节，避免切断多字节字符 */
		while ((*p & 0xC0) == 0x80)
			++p;
	}
	return g_strstrip(g_strdup(p));
}

char *error_message(const char *prefix, const char *err)
{
	char *tail = tail_text(err ? err : "", 300);
	char *msg = g_strdup_printf("%s: %s", prefix,
				    *tail ? tail : "未知错误");

	g_free(tail);
	return msg;
}

bool run_command(const char **argv, char **out, char **err)
{
	GError *error = NULL;
	gint status = 0;

	if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH,
			  NULL, NULL, out, err, &status, &error)) {
		*out = g_strdup("");
		*err = g_strdup(error->message);
		g_error_free(error);
		return false;
	}
	if (!g_spawn_check_wait_status(status, &error)) {
		if (error)
			g_error_free(error);
		return false;
	}
	return true;
}

bool run_streamed(const char **argv, line_fn on_line, void *data, char **tail)
{
	GError *error = NULL;
	GSubprocess *proc;
	GDataInputStream *din;
	GString *all = g_string_new(NULL);
	char *line;
	bool ok;

	proc = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE |
				 G_SUBPROCESS_FLAGS_STDERR_MERGE, &error);
	if (!proc) {
		*tail = g_strdup(error->message);
		g_error_free(error);
		g_string_free(all, TRUE);
		return false;
	}

	din = g_data_input_stream_new(g_subprocess_get_stdout_pipe(proc));
	g_data_input_stream_set_newline_type(din,
					     G_DATA_STREAM_NEWLINE_TYPE_ANY);
	while ((line = g_data_input_stream_read_line(din, NULL, NULL,
						     NULL))) {
		g_string_append(all, line);
		g_string_append_c(all, '\n');
		if (on_line)
			on_line(line, data);
		g_free(line);
	}

	ok = g_subprocess_wait_check(proc, NULL, NULL);
	*tail = tail_text(all->str, 400);

	g_string_free(all, TRUE);
	g_object_unref(din);
	g_object_unref(proc);
	return ok;
}
