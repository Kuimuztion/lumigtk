/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SOLFA_IMPORTER_H
#define SOLFA_IMPORTER_H

#include <stdbool.h>

/* 以下两个回调都在主线程中执行 */
typedef void (*import_progress_fn)(double fraction, const char *text,
				   void *data);
typedef void (*import_done_fn)(bool ok, const char *message,
			       const char *audio_path, void *data);

/* 从任意文本（如带中文标题的分享文字）中提取第一个受支持平台的链接。
 * 没有则返回 NULL；返回值需 g_free()。 */
char *importer_extract_url(const char *text);

/* 在后台线程中下载视频（/tmp）并转换为 flac（~/.config/solfa/audio） */
void importer_start(const char *url, import_progress_fn progress,
		    import_done_fn done, void *data);

#endif
