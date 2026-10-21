/* SPDX-License-Identifier: GPL-3.0-only */
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "common.h"
#include "importer.h"

static const char *const supported_hosts[] = {
	"bilibili.com", "b23.tv", "douyin.com", "iesdouyin.com",
	"kuaishou.com", "chenzhongtv.com", NULL,
};

struct import_job {
	char *url;
	char *audio_path;
	char *message;
	int streams;		/* yt-dlp 已开始的下载流数量（视频、音频各一） */
	bool ok;
	import_progress_fn progress;
	import_done_fn done;
	void *data;
};

struct progress_event {
	double fraction;
	char *text;
	import_progress_fn fn;
	void *data;
};

static bool url_char(unsigned char c)
{
	if (g_ascii_isalnum(c))
		return true;
	return c != '\0' && strchr("-._~:/?#&=%+@!*;,'", c) != NULL;
}

static bool host_matches(const char *host, size_t len, const char *domain)
{
	size_t dlen = strlen(domain);

	if (len < dlen || strncasecmp(host + len - dlen, domain, dlen) != 0)
		return false;
	return len == dlen || host[len - dlen - 1] == '.';
}

static bool host_supported(const char *url)
{
	const char *host = strstr(url, "://");
	size_t len = 0;
	size_t i;

	if (!host)
		return false;
	host += 3;
	while (host[len] && !strchr("/?#:", host[len]))
		++len;
	for (i = 0; supported_hosts[i]; ++i) {
		if (host_matches(host, len, supported_hosts[i]))
			return true;
	}
	return false;
}

char *importer_extract_url(const char *text)
{
	const char *p = strstr(text, "https://");
	GString *s;
	char *url;

	if (!p)
		p = strstr(text, "http://");
	if (!p)
		return NULL;
	s = g_string_new(NULL);
	while (url_char((unsigned char)*p))
		g_string_append_c(s, *p++);
	url = g_string_free(s, FALSE);
	if (!host_supported(url)) {
		g_free(url);
		return NULL;
	}
	return url;
}

static bool is_short_link(const char *url)
{
	return strstr(url, "://b23.tv/") != NULL ||
	       strstr(url, "://v.douyin.com/") != NULL ||
	       strstr(url, "://v.kuaishou.com/") != NULL;
}

/* ---------- 进度回调（工作线程投递到主线程） ---------- */

static gboolean deliver_progress(gpointer p)
{
	struct progress_event *ev = p;

	ev->fn(ev->fraction, ev->text, ev->data);
	g_free(ev->text);
	g_free(ev);
	return G_SOURCE_REMOVE;
}

static void post_progress(struct import_job *job, double frac,
			  const char *text)
{
	struct progress_event *ev = g_new0(struct progress_event, 1);

	ev->fraction = frac;
	ev->text = g_strdup(text);
	ev->fn = job->progress;
	ev->data = job->data;
	g_idle_add(deliver_progress, ev);
}

/* ---------- 解析短链接 ---------- */

static void resolve_short_link(struct import_job *job)
{
	const char *argv[] = {
		"curl", "-sL", "-o", "/dev/null", "-w", "%{url_effective}",
		"--max-time", "20", job->url, NULL,
	};
	char *out = NULL;
	char *err = NULL;

	if (!is_short_link(job->url))
		return;
	if (run_command(argv, &out, &err) && out && *out) {
		g_strstrip(out);
		g_free(job->url);
		job->url = g_strdup(out);
	}
	g_free(out);
	g_free(err);
}

/* ---------- 下载视频 ---------- */

static void on_ytdlp_line(const char *line, void *data)
{
	struct import_job *job = data;
	const char *p = strstr(line, "[download]");
	double pct = 0.0;
	double frac;
	int idx;
	char *text;

	if (strstr(line, "[download] Destination:")) {
		++job->streams;
		return;
	}
	if (strstr(line, "[Merger]")) {
		post_progress(job, 0.86, "正在合并音视频");
		return;
	}
	if (p && sscanf(p + 10, " %lf%%", &pct) == 1) {
		idx = job->streams > 0 ? job->streams : 1;
		/* 两条流各占一半进度，下载阶段总体落在 5% ~ 85% 之间 */
		frac = 0.05 + 0.75 * ((idx - 1 + pct / 100.0) / 2.0);
		if (frac > 0.85)
			frac = 0.85;
		text = g_strdup_printf("下载视频 %.1f%%", pct);
		post_progress(job, frac, text);
		g_free(text);
	}
}

static bool download_video(struct import_job *job, char **video)
{
	const char *tmpl = TMP_VIDEO_DIR "/%(title)s [%(id)s].%(ext)s";
	char *pathfile = g_strdup_printf(TMP_WORK_DIR "/%08x.path",
					 g_random_int());
	char *cookies = cookies_path();
	const char *argv[32];
	char *tail = NULL;
	char *content = NULL;
	bool ok = false;
	int n = 0;

	g_mkdir_with_parents(TMP_VIDEO_DIR, 0755);
	g_mkdir_with_parents(TMP_WORK_DIR, 0755);

	argv[n++] = "yt-dlp";
	argv[n++] = "--no-playlist";
	argv[n++] = "--newline";
	argv[n++] = "--no-colors";
	argv[n++] = "--trim-filenames";
	argv[n++] = "60";
	argv[n++] = "-f";
	argv[n++] = "bv*+ba/b";
	argv[n++] = "--merge-output-format";
	argv[n++] = "mkv";
	argv[n++] = "--print-to-file";
	argv[n++] = "after_move:filepath";
	argv[n++] = pathfile;
	argv[n++] = "-o";
	argv[n++] = tmpl;
	if (cookies) {
		argv[n++] = "--cookies";
		argv[n++] = cookies;
	}
	argv[n++] = job->url;
	argv[n] = NULL;

	if (!run_streamed(argv, on_ytdlp_line, job, &tail)) {
		job->message = error_message("下载失败", tail);
		goto out;	/* 跳到 out：yt-dlp 退出码非零 */
	}
	if (!g_file_get_contents(pathfile, &content, NULL, NULL)) {
		job->message = g_strdup("下载完成，但未能获取视频路径");
		goto out;	/* 跳到 out：路径文件不存在 */
	}
	*video = last_line(content);
	if (!g_file_test(*video, G_FILE_TEST_IS_REGULAR)) {
		job->message = g_strdup("下载完成，但未找到视频文件");
		g_free(*video);
		*video = NULL;
		goto out;	/* 跳到 out：视频文件缺失 */
	}
	ok = true;
out:	/* 统一释放临时资源 */
	g_free(pathfile);
	g_free(cookies);
	g_free(tail);
	g_free(content);
	return ok;
}

/* ---------- 转换为音频 ---------- */

static char *track_name(const char *video)
{
	char *base = g_path_get_basename(video);
	char *dot = strrchr(base, '.');
	char *tag;
	char *name;

	if (dot)
		*dot = '\0';
	/* 去掉 yt-dlp 追加的 " [BVxxxx]" 后缀 */
	tag = strrchr(base, '[');
	if (tag && tag > base && tag[-1] == ' ')
		tag[-1] = '\0';
	name = g_strdup(g_strstrip(base));
	g_free(base);
	return name;
}

/* 同名时自动追加 " (2)"、" (3)"……，绝不覆盖已有歌曲 */
static char *unique_audio_path(const char *dir, const char *name)
{
	char *path = g_strdup_printf("%s/%s.flac", dir, name);
	int n = 2;

	while (g_file_test(path, G_FILE_TEST_EXISTS)) {
		g_free(path);
		path = g_strdup_printf("%s/%s (%d).flac", dir, name, n++);
	}
	return path;
}

static bool convert_audio(struct import_job *job, const char *video,
			  const char *audio)
{
	const char *argv[] = {
		"ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
		"-i", video, "-map", "0:a:0", "-vn", "-c:a", "flac", audio,
		NULL,
	};
	char *out = NULL;
	char *err = NULL;
	bool ok;

	ok = run_command(argv, &out, &err);
	if (!ok)
		job->message = error_message("转换失败", err);
	g_free(out);
	g_free(err);
	return ok;
}

static bool run_import(struct import_job *job)
{
	char *video = NULL;
	char *name = NULL;
	char *dir = NULL;
	char *audio = NULL;
	bool ok = false;

	post_progress(job, 0.02, "解析链接");
	resolve_short_link(job);

	if (!download_video(job, &video))
		goto out;	/* 跳到 out：下载失败，message 已设置 */

	name = track_name(video);
	dir = audio_dir();
	audio = unique_audio_path(dir, name);

	post_progress(job, 0.90, "转换为音频");
	if (!convert_audio(job, video, audio))
		goto out;	/* 跳到 out：转换失败，message 已设置 */

	job->audio_path = audio;
	audio = NULL;
	job->message = g_strdup_printf("已导入: %s", name);
	ok = true;
out:	/* 统一释放临时资源 */
	g_free(video);
	g_free(name);
	g_free(dir);
	g_free(audio);
	return ok;
}

/* ---------- 工作线程与结果投递 ---------- */

static gboolean deliver_result(gpointer p)
{
	struct import_job *job = p;

	job->done(job->ok, job->message ? job->message : "导入失败",
		  job->audio_path, job->data);
	g_free(job->url);
	g_free(job->audio_path);
	g_free(job->message);
	g_free(job);
	return G_SOURCE_REMOVE;
}

static gpointer import_worker(gpointer p)
{
	struct import_job *job = p;

	job->ok = run_import(job);
	if (job->ok)
		post_progress(job, 1.0, "导入完成");
	g_idle_add(deliver_result, job);
	return NULL;
}

void importer_start(const char *url, import_progress_fn progress,
		    import_done_fn done, void *data)
{
	struct import_job *job = g_new0(struct import_job, 1);
	GThread *thread;

	job->url = g_strdup(url);
	job->progress = progress;
	job->done = done;
	job->data = data;
	thread = g_thread_try_new("import", import_worker, job, NULL);
	if (!thread) {
		job->message = g_strdup("无法创建后台线程");
		g_idle_add(deliver_result, job);
		return;
	}
	g_thread_unref(thread);
}
