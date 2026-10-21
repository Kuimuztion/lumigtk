/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * lumigtk: GTK4 本地音乐播放器，从剪贴板导入 B 站 / 抖音 / 快手视频链接。
 */
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "importer.h"
#include "player.h"
#include "settings.h"

enum play_mode {
	MODE_SEQUENTIAL,
	MODE_LOOP,
	MODE_REPEAT_ONE,
	MODE_SHUFFLE,
};

static GtkWidget *window;
static GtkWidget *track_list;
static GtkWidget *title_label;
static GtkWidget *seek_scale;
static GtkWidget *time_label;
static GtkWidget *play_button;
static GtkWidget *mode_dropdown;
static GtkWidget *progress_bar;
static GtkWidget *status_label;

/* 设置窗口控件；窗口销毁时由 on_settings_destroy 清空 */
static GtkWidget *settings_win;
static GtkWidget *theme_dropdown;
static GtkWidget *wallpaper_label;
static GtkWidget *palette_label;
static GtkWidget *panel_scale;
static GtkWidget *autoplay_switch;

static GPtrArray *tracks;	/* char *：音频文件完整路径 */
static char *playing;		/* 当前曲目路径，可能为 NULL */
static struct app_settings cfg;
static bool importing;
static bool seek_updating;
static bool refreshing;		/* 正在由代码更新控件，忽略其信号 */
static bool seek_dragging;

static void rebuild_list(void);
static void play_track(int idx);
static void on_row_left_click(GtkGesture *g, gint n, gdouble x, gdouble y, gpointer unused);
static void apply_look(void);

static void set_status(const char *msg)
{
	gtk_label_set_text(GTK_LABEL(status_label), msg);
}

static enum play_mode current_mode(void)
{
	return (enum play_mode)gtk_drop_down_get_selected(
		GTK_DROP_DOWN(mode_dropdown));
}

static int compare_paths(gconstpointer a, gconstpointer b)
{
	return g_strcmp0(*(char *const *)a, *(char *const *)b);
}

static int find_track(const char *path)
{
	guint i;

	if (!path)
		return -1;
	for (i = 0; i < tracks->len; ++i) {
		if (strcmp(g_ptr_array_index(tracks, i), path) == 0)
			return (int)i;
	}
	return -1;
}

static char *title_of(const char *path)
{
	char *base = g_path_get_basename(path);
	char *dot = strrchr(base, '.');

	if (dot)
		*dot = '\0';
	return base;
}

static void scan_library(void)
{
	char *dir = audio_dir();
	GDir *d;
	const char *name;

	g_ptr_array_remove_range(tracks, 0, tracks->len);
	d = g_dir_open(dir, 0, NULL);
	if (d) {
		while ((name = g_dir_read_name(d))) {
			if (g_str_has_suffix(name, ".flac"))
				g_ptr_array_add(tracks,
						g_build_filename(dir, name,
								 NULL));
		}
		g_dir_close(d);
	}
	g_ptr_array_sort(tracks, compare_paths);
	g_free(dir);
}

static void select_row(int idx)
{
	GtkWidget *child = gtk_widget_get_first_child(track_list);
	int i = 0;

	for (; child; child = gtk_widget_get_next_sibling(child), ++i) {
		if (i == idx)
			gtk_widget_add_css_class(child, "selected");
		else
			gtk_widget_remove_css_class(child, "selected");
	}
}

static void update_play_button(void)
{
	gtk_button_set_icon_name(GTK_BUTTON(play_button),
		player_is_playing() ? "media-playback-pause-symbolic" :
				      "media-playback-start-symbolic");
}

/* ---------- 重命名 ---------- */

struct rename_ctx {
	GtkWidget *dlg;
	GtkWidget *entry;
	char *old_path;
};

static void rename_ctx_free(gpointer p)
{
	struct rename_ctx *c = p;

	g_free(c->old_path);
	g_free(c);
}

static void do_rename(struct rename_ctx *c)
{
	const char *text = gtk_editable_get_text(GTK_EDITABLE(c->entry));
	char *name = g_strstrip(g_strdup(text));
	char *dir;
	char *new_path;

	/* 允许用户输入带 .flac 的名字，去掉后缀；替换路径分隔符 */
	if (g_str_has_suffix(name, ".flac"))
		name[strlen(name) - 5] = '\0';
	g_strdelimit(name, "/\\", '_');
	if (!*name) {
		set_status("名称不能为空");
		g_free(name);
		return;
	}

	dir = g_path_get_dirname(c->old_path);
	new_path = g_strdup_printf("%s/%s.flac", dir, name);
	g_free(dir);
	g_free(name);

	if (strcmp(new_path, c->old_path) != 0) {
		if (g_file_test(new_path, G_FILE_TEST_EXISTS)) {
			set_status("已存在同名歌曲");
			g_free(new_path);
			return;
		}
		if (g_rename(c->old_path, new_path) != 0) {
			set_status("重命名失败");
			g_free(new_path);
			return;
		}
		if (playing && strcmp(playing, c->old_path) == 0) {
			/* 正在播放的文件被改名：停止播放，避免读取失效 */
			player_stop();
			g_clear_pointer(&playing, g_free);
			gtk_label_set_text(GTK_LABEL(title_label), "未播放");
			update_play_button();
		}
		scan_library();
		rebuild_list();
		set_status("已重命名");
	}
	g_free(new_path);
	gtk_window_destroy(GTK_WINDOW(c->dlg));
}

static void open_rename_dialog(int idx)
{
	const char *old = g_ptr_array_index(tracks, idx);
	GtkWidget *dlg = gtk_window_new();
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *entry = gtk_entry_new();
	GtkWidget *ok = gtk_button_new_with_label("确定");
	GtkWidget *cancel = gtk_button_new_with_label("取消");
	struct rename_ctx *c = g_new0(struct rename_ctx, 1);
	char *title = title_of(old);

	c->dlg = dlg;
	c->entry = entry;
	c->old_path = g_strdup(old);

	gtk_window_set_title(GTK_WINDOW(dlg), "重命名");
	gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(window));
	gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_widget_set_size_request(entry, 320, -1);
	gtk_editable_set_text(GTK_EDITABLE(entry), title);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_widget_add_css_class(ok, "accent");

	gtk_box_append(GTK_BOX(buttons), cancel);
	gtk_box_append(GTK_BOX(buttons), ok);
	gtk_box_append(GTK_BOX(box), entry);
	gtk_box_append(GTK_BOX(box), buttons);
	gtk_window_set_child(GTK_WINDOW(dlg), box);

	/* 上下文随对话框的 GObject 一起释放 */
	g_object_set_data_full(G_OBJECT(dlg), "rename-ctx", c, rename_ctx_free);
	g_signal_connect_swapped(entry, "activate", G_CALLBACK(do_rename), c);
	g_signal_connect_swapped(ok, "clicked", G_CALLBACK(do_rename), c);
	g_signal_connect_swapped(cancel, "clicked",
				 G_CALLBACK(gtk_window_destroy), dlg);

	gtk_window_present(GTK_WINDOW(dlg));
	gtk_widget_grab_focus(entry);
	g_free(title);
}

static void on_row_right_click(GtkGesture *g, gint n, gdouble x, gdouble y,
	gpointer unused)
{
	GtkWidget *row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
	int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "track-index"));

	gtk_gesture_set_state(g, GTK_EVENT_SEQUENCE_CLAIMED);
	if (idx >= 0 && idx < (int)tracks->len)
		open_rename_dialog(idx);
}

static void rebuild_list(void)
{
	GtkWidget *child;
	guint i;

	while ((child = gtk_widget_get_first_child(track_list)))
		gtk_box_remove(GTK_BOX(track_list), child);
	for (i = 0; i < tracks->len; ++i) {
		char *title = title_of(g_ptr_array_index(tracks, i));
		GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		GtkWidget *label = gtk_label_new(title);
		GtkGesture *left = gtk_gesture_click_new();
		GtkGesture *right = gtk_gesture_click_new();

		gtk_widget_add_css_class(row, "track-row");
		g_object_set_data(G_OBJECT(row), "track-index", GINT_TO_POINTER((int)i));
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_widget_set_hexpand(label, TRUE);
		gtk_box_append(GTK_BOX(row), label);

		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(left), GDK_BUTTON_PRIMARY);
		g_signal_connect(left, "pressed", G_CALLBACK(on_row_left_click), NULL);
		gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(left));

		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
		g_signal_connect(right, "pressed", G_CALLBACK(on_row_right_click), NULL);
		gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(right));

		gtk_box_append(GTK_BOX(track_list), row);
		g_free(title);
	}
	select_row(find_track(playing));
}

/* ---------- 播放控制 ---------- */

static int random_other(int from)
{
	gint32 n = (gint32)tracks->len;
	int r;

	if (n == 1)
		return 0;
	do {
		r = g_random_int_range(0, n);
	} while (r == from);
	return r;
}


static int prev_track(int from)
{
	int n = (int)tracks->len;

	if (n == 0)
		return -1;
	if (from < 0)
		from = 0;
	switch (current_mode()) {
	case MODE_LOOP:
		return (from + n - 1) % n;
	case MODE_SHUFFLE:
		return random_other(from);
	default:
		return from > 0 ? from - 1 : 0;
	}
}

static int next_track(int from)
{
	int n = (int)tracks->len;

	if (n == 0)
		return -1;
	switch (current_mode()) {
	case MODE_LOOP:
		return (from + 1) % n;
	case MODE_SHUFFLE:
		return random_other(from);
	default:
		return from + 1 < n ? from + 1 : -1;
	}
}

/* 播放结束时调用：单曲循环重复当前曲目，其余模式与下一首一致 */
static int auto_next_track(int from)
{
	if (current_mode() == MODE_REPEAT_ONE)
		return from;
	return next_track(from);
}

static void play_track(int idx)
{
	const char *path;
	char *title;

	if (idx < 0 || idx >= (int)tracks->len)
		return;
	path = g_ptr_array_index(tracks, idx);
	if (!player_open(path)) {
		set_status("无法播放该文件");
		return;
	}
	g_free(playing);
	playing = g_strdup(path);
	title = title_of(path);
	gtk_label_set_text(GTK_LABEL(title_label), title);
	g_free(title);
	select_row(idx);
	update_play_button();
	set_status("正在播放");
}

static void on_eos(void *unused)
{
	int idx = auto_next_track(find_track(playing));

	if (idx < 0) {
		player_stop();
		update_play_button();
		set_status("播放结束");
		return;
	}
	play_track(idx);
}

static void format_mmss(char *buf, size_t len, int64_t ns)
{
	int64_t s = ns / 1000000000;

	g_snprintf(buf, len, "%02d:%02d", (int)(s / 60), (int)(s % 60));
}

static void show_time(int64_t pos, int64_t dur)
{
	char a[16];
	char b[16];
	char buf[40];

	format_mmss(a, sizeof(a), pos);
	format_mmss(b, sizeof(b), dur);
	g_snprintf(buf, sizeof(buf), "%s / %s", a, b);
	gtk_label_set_text(GTK_LABEL(time_label), buf);
}

static gboolean on_tick(gpointer unused)
{
	int64_t pos = player_position();
	int64_t dur = player_duration();

	if (dur <= 0)
		return G_SOURCE_CONTINUE;
	if (seek_dragging) {
		/* 拖动期间时间标签跟随滑块，不读取播放器位置 */
		show_time((int64_t)(gtk_range_get_value(GTK_RANGE(seek_scale)) * 1000000000.0), dur);
		return G_SOURCE_CONTINUE;
	}
	if (pos < 0)
		return G_SOURCE_CONTINUE;
	seek_updating = true;
	gtk_range_set_range(GTK_RANGE(seek_scale), 0.0, (double)dur / 1e9);
	gtk_range_set_value(GTK_RANGE(seek_scale), (double)pos / 1e9);
	seek_updating = false;
	show_time(pos, dur);
	return G_SOURCE_CONTINUE;
}


static void on_play_clicked(GtkWidget *w, gpointer unused)
{
	if (!playing) {
		if (tracks->len > 0)
			play_track(0);
		else
			set_status("曲库为空，请先复制视频链接后点击导入");
		return;
	}
	player_toggle();
	update_play_button();
}

static void on_prev_clicked(GtkWidget *w, gpointer unused)
{
	play_track(prev_track(find_track(playing)));
}

static void on_next_clicked(GtkWidget *w, gpointer unused)
{
	int idx = next_track(find_track(playing));

	if (idx < 0)
		set_status("已经是最后一首");
	else
		play_track(idx);
}

static void on_row_left_click(GtkGesture *g, gint n, gdouble x, gdouble y,
	gpointer unused)
{
	GtkWidget *row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
	int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "track-index"));

	play_track(idx);
}


static void on_mode_changed(GObject *obj, GParamSpec *pspec, gpointer unused)
{
	if (refreshing)
		return;
	cfg.play_mode = (int)current_mode();
	settings_save(&cfg);
}

/* ---------- 进度条拖动：拖动时静音，松开后定位并恢复声音 ---------- */

static gboolean on_seek_event(GtkEventControllerLegacy *c, GdkEvent *ev,
	gpointer unused)
{
	GtkWidget *scale = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(c));
	GdkEventType type = gdk_event_get_event_type(ev);

	if (type == GDK_BUTTON_PRESS && !seek_dragging) {
		seek_dragging = true;
		player_set_mute(true);
	} else if (type == GDK_BUTTON_RELEASE && seek_dragging) {
		seek_dragging = false;
		if (playing)
			player_seek((int64_t)(gtk_range_get_value(GTK_RANGE(scale)) * 1000000000.0));
		player_set_mute(false);
	}
	return FALSE;
}

static void on_seek_changed(GtkRange *range, gpointer unused)
{
	int64_t dur = player_duration();

	if (seek_updating)
		return;
	if (seek_dragging) {
		if (dur > 0)
			show_time((int64_t)(gtk_range_get_value(range) * 1000000000.0), dur);
		return;
	}
	if (playing)
		player_seek((int64_t)(gtk_range_get_value(range) * 1000000000.0));
}


/* ---------- 导入（剪贴板） ---------- */

static void on_import_progress(double fraction, const char *text,
			       void *unused)
{
	gtk_widget_set_visible(progress_bar, TRUE);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), fraction);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar), text);
	set_status(text);
}

static void on_import_done(bool ok, const char *message,
			   const char *audio_path, void *unused)
{
	int idx;

	importing = false;
	gtk_widget_set_visible(progress_bar, FALSE);
	set_status(message);
	if (!ok)
		return;

	scan_library();
	rebuild_list();
	idx = find_track(audio_path);
	if (cfg.autoplay_import && idx >= 0)
		play_track(idx);
	else
		select_row(idx);
}

static void on_clipboard_read(GObject *src, GAsyncResult *res, gpointer unused)
{
	GError *err = NULL;
	char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(src), res,
						    &err);
	char *url = NULL;

	if (err) {
		importing = false;
		set_status("无法读取剪贴板");
		g_error_free(err);
		return;
	}
	url = text ? importer_extract_url(text) : NULL;
	g_free(text);
	if (!url) {
		importing = false;
		set_status("剪贴板中没有受支持的链接（B 站 / 抖音 / 快手）");
		return;
	}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
	gtk_widget_set_visible(progress_bar, TRUE);
	importer_start(url, on_import_progress, on_import_done, NULL);
	g_free(url);
}

static void on_import_clicked(GtkWidget *w, gpointer unused)
{
	GdkClipboard *clip;

	if (importing) {
		set_status("已有导入任务在进行中");
		return;
	}
	importing = true;
	clip = gdk_display_get_clipboard(gtk_widget_get_display(window));
	set_status("正在读取剪贴板");
	gdk_clipboard_read_text_async(clip, NULL, on_clipboard_read, NULL);
}

/* ---------- 设置窗口 ---------- */

static void refresh_settings_widgets(void)
{
	char *base;
	char *markup;

	if (!settings_win)
		return;

	refreshing = true;
	base = cfg.wallpaper ? g_path_get_basename(cfg.wallpaper)
			     : g_strdup("未设置");
	gtk_label_set_text(GTK_LABEL(wallpaper_label), base);
	g_free(base);

	markup = g_strdup_printf(
		"<span size='x-large' foreground='#%02x%02x%02x'>"
		"████████</span>  "
		"<span size='x-large' foreground='#%02x%02x%02x'>"
		"████████</span>  "
		"<span size='x-large' foreground='#%02x%02x%02x'>"
		"████████</span>",
		cfg.colors[0][0], cfg.colors[0][1], cfg.colors[0][2],
		cfg.colors[1][0], cfg.colors[1][1], cfg.colors[1][2],
		cfg.colors[2][0], cfg.colors[2][1], cfg.colors[2][2]);
	gtk_label_set_markup(GTK_LABEL(palette_label), markup);
	g_free(markup);

	gtk_drop_down_set_selected(GTK_DROP_DOWN(theme_dropdown),
				   cfg.mode == THEME_DARK ? 1 : 0);
	gtk_range_set_value(GTK_RANGE(panel_scale), cfg.panel_alpha);
	gtk_switch_set_active(GTK_SWITCH(autoplay_switch),
			      cfg.autoplay_import);
	refreshing = false;
}

/* 应用当前设置：样式、窗口壁纸类、保存并刷新设置窗口 */
static void apply_look(void)
{
	settings_apply(&cfg);
	if (cfg.wallpaper)
		gtk_widget_add_css_class(window, "wallpaper");
	else
		gtk_widget_remove_css_class(window, "wallpaper");
	settings_save(&cfg);
	refresh_settings_widgets();
}

static void on_theme_changed(GObject *obj, GParamSpec *pspec, gpointer unused)
{
	if (refreshing)
		return;
	cfg.mode = gtk_drop_down_get_selected(GTK_DROP_DOWN(theme_dropdown)) == 1
		   ? THEME_DARK : THEME_LIGHT;
	apply_look();
}

static void on_panel_changed(GtkRange *range, gpointer unused)
{
	if (refreshing)
		return;
	cfg.panel_alpha = gtk_range_get_value(range);
	apply_look();
}

static gboolean on_autoplay_changed(GtkSwitch *sw, gboolean state,
				    gpointer unused)
{
	if (!refreshing) {
		cfg.autoplay_import = state;
		settings_save(&cfg);
	}
	return FALSE;
}

static void on_wallpaper_chosen(GObject *src, GAsyncResult *res,
				gpointer unused)
{
	GError *err = NULL;
	GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res,
						  &err);
	char *path;

	g_object_unref(src);
	if (!file) {
		g_clear_error(&err);	/* 用户取消 */
		return;
	}
	path = g_file_get_path(file);
	g_object_unref(file);
	if (path) {
		settings_set_wallpaper(&cfg, path);
		set_status("已设置壁纸并从图片提取配色");
		apply_look();
	}
	g_free(path);
}

static void on_pick_wallpaper(GtkWidget *w, gpointer unused)
{
	GtkFileDialog *fd = gtk_file_dialog_new();
	GtkFileFilter *filter = gtk_file_filter_new();
	GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);

	gtk_file_filter_set_name(filter, "图片");
	gtk_file_filter_add_suffix(filter, "png");
	gtk_file_filter_add_suffix(filter, "jpg");
	gtk_file_filter_add_suffix(filter, "jpeg");
	gtk_file_filter_add_suffix(filter, "webp");
	gtk_file_filter_add_suffix(filter, "bmp");
	g_list_store_append(store, filter);

	gtk_file_dialog_set_title(fd, "选择壁纸");
	gtk_file_dialog_set_filters(fd, G_LIST_MODEL(store));
	gtk_file_dialog_open(fd, GTK_WINDOW(settings_win), NULL,
			     on_wallpaper_chosen, NULL);

	g_object_unref(store);
	g_object_unref(filter);
}

static void on_clear_wallpaper(GtkWidget *w, gpointer unused)
{
	settings_clear_wallpaper(&cfg);
	set_status("已清除壁纸");
	apply_look();
}

static void on_settings_destroy(GtkWidget *w, gpointer unused)
{
	settings_win = NULL;
	theme_dropdown = NULL;
	wallpaper_label = NULL;
	palette_label = NULL;
	panel_scale = NULL;
	autoplay_switch = NULL;
}

static GtkWidget *form_row(const char *title, GtkWidget *w)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget *label = gtk_label_new(title);

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_box_append(GTK_BOX(row), label);
	gtk_box_append(GTK_BOX(row), w);
	return row;
}

static void open_settings(GtkWidget *w, gpointer unused)
{
	static const char *const themes[] = { "浅色", "深色", NULL };
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
	GtkWidget *wp_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *pick = gtk_button_new_with_label("选择壁纸");
	GtkWidget *clear = gtk_button_new_with_label("清除壁纸");
	GtkWidget *dir_label;
	GtkWidget *cap;
	char *dir;

	if (settings_win) {
		gtk_window_present(GTK_WINDOW(settings_win));
		return;
	}

	settings_win = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(settings_win), "设置");
	gtk_window_set_transient_for(GTK_WINDOW(settings_win),
				     GTK_WINDOW(window));
	gtk_window_set_default_size(GTK_WINDOW(settings_win), 460, -1);
	g_signal_connect(settings_win, "destroy",
			 G_CALLBACK(on_settings_destroy), NULL);

	gtk_widget_set_margin_top(box, 18);
	gtk_widget_set_margin_bottom(box, 18);
	gtk_widget_set_margin_start(box, 18);
	gtk_widget_set_margin_end(box, 18);

	/* 主题 */
	theme_dropdown = gtk_drop_down_new_from_strings(themes);
	gtk_box_append(GTK_BOX(box), form_row("主题", theme_dropdown));
	g_signal_connect(theme_dropdown, "notify::selected",
			 G_CALLBACK(on_theme_changed), NULL);

	/* 壁纸 */
	wallpaper_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(wallpaper_label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(wallpaper_label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_hexpand(wallpaper_label, TRUE);
	gtk_box_append(GTK_BOX(wp_row), wallpaper_label);
	gtk_box_append(GTK_BOX(wp_row), pick);
	gtk_box_append(GTK_BOX(wp_row), clear);
	cap = gtk_label_new("壁纸");
	gtk_label_set_xalign(GTK_LABEL(cap), 0.0);
	gtk_box_append(GTK_BOX(box), cap);
	gtk_box_append(GTK_BOX(box), wp_row);
	g_signal_connect(pick, "clicked", G_CALLBACK(on_pick_wallpaper), NULL);
	g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_wallpaper), NULL);

	/* 配色预览（由壁纸自动提取，只读） */
	palette_label = gtk_label_new(NULL);
	gtk_label_set_use_markup(GTK_LABEL(palette_label), TRUE);
	gtk_box_append(GTK_BOX(box), form_row("自动配色", palette_label));

	/* 面板透明度 */
	panel_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
					       0.3, 1.0, 0.05);
	gtk_scale_set_draw_value(GTK_SCALE(panel_scale), FALSE);
	gtk_widget_set_size_request(panel_scale, 200, -1);
	gtk_box_append(GTK_BOX(box), form_row("面板不透明度", panel_scale));
	g_signal_connect(panel_scale, "value-changed",
			 G_CALLBACK(on_panel_changed), NULL);

	/* 导入后自动播放 */
	autoplay_switch = gtk_switch_new();
	gtk_widget_set_valign(autoplay_switch, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(box), form_row("导入后自动播放", autoplay_switch));
	g_signal_connect(autoplay_switch, "state-set",
			 G_CALLBACK(on_autoplay_changed), NULL);

	/* 音频目录（只读提示） */
	dir = audio_dir();
	cap = gtk_label_new("歌曲目录（重新安装不会清除）");
	gtk_label_set_xalign(GTK_LABEL(cap), 0.0);
	dir_label = gtk_label_new(dir);
	gtk_label_set_xalign(GTK_LABEL(dir_label), 0.0);
	gtk_label_set_wrap(GTK_LABEL(dir_label), TRUE);
	gtk_label_set_selectable(GTK_LABEL(dir_label), TRUE);
	gtk_widget_add_css_class(dir_label, "muted");
	gtk_box_append(GTK_BOX(box), cap);
	gtk_box_append(GTK_BOX(box), dir_label);
	g_free(dir);

	gtk_window_set_child(GTK_WINDOW(settings_win), box);
	refresh_settings_widgets();
	gtk_window_present(GTK_WINDOW(settings_win));
}

/* ---------- 界面构建 ---------- */

static void on_window_destroy(GtkWidget *w, gpointer unused)
{
	if (settings_win)
		gtk_window_destroy(GTK_WINDOW(settings_win));
}

static void build_ui(GtkApplication *app)
{
	static const char *const modes[] = {
		"顺序播放", "列表循环", "单曲循环", "随机播放", NULL,
	};
	GtkWidget *header;
	GtkWidget *import_button;
	GtkWidget *vbox;
	GtkWidget *scroller;
	GtkWidget *bottom;
	GtkWidget *controls;
	GtkWidget *prev_button;
	GtkWidget *next_button;
	GtkWidget *settings_button;
	GtkWidget *settings_content;
	GtkWidget *spacer;

	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "LUMIGTK");
	gtk_window_set_default_size(GTK_WINDOW(window), 960, 680);
	gtk_widget_add_css_class(window, "lumigtk");
	g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

	/* 标题栏：右上角只保留导入 */
	header = gtk_header_bar_new();
	import_button = gtk_button_new_with_label("导入");
	gtk_widget_add_css_class(import_button, "accent");
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), import_button);
	gtk_window_set_titlebar(GTK_WINDOW(window), header);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_top(vbox, 12);
	gtk_widget_set_margin_bottom(vbox, 12);
	gtk_widget_set_margin_start(vbox, 12);
	gtk_widget_set_margin_end(vbox, 12);
	gtk_window_set_child(GTK_WINDOW(window), vbox);

	/* 曲目列表 */
	scroller = gtk_scrolled_window_new();
	gtk_widget_set_vexpand(scroller, TRUE);
	gtk_widget_add_css_class(scroller, "panel");
track_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
gtk_widget_set_hexpand(track_list, TRUE);
gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), track_list);
	gtk_box_append(GTK_BOX(vbox), scroller);

	/* 底部面板 */
	bottom = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_add_css_class(bottom, "panel");

	title_label = gtk_label_new("未播放");
	gtk_widget_add_css_class(title_label, "now-title");
	gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);

	seek_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
					      0.0, 1.0, 1.0);
	gtk_scale_set_draw_value(GTK_SCALE(seek_scale), FALSE);
	gtk_widget_set_hexpand(seek_scale, TRUE);

	controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	prev_button = gtk_button_new_from_icon_name(
		"media-skip-backward-symbolic");
	play_button = gtk_button_new_from_icon_name(
		"media-playback-start-symbolic");
	next_button = gtk_button_new_from_icon_name(
		"media-skip-forward-symbolic");
	time_label = gtk_label_new("00:00 / 00:00");
	mode_dropdown = gtk_drop_down_new_from_strings(modes);
	spacer = gtk_label_new("");
	gtk_widget_set_hexpand(spacer, TRUE);

	/* 设置按钮：右下角，图标加文字，不使用 emoji */
	settings_button = gtk_button_new();
	settings_content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append(GTK_BOX(settings_content),
		       gtk_image_new_from_icon_name(
			       "emblem-system-symbolic"));
	gtk_box_append(GTK_BOX(settings_content), gtk_label_new("设置"));
	gtk_button_set_child(GTK_BUTTON(settings_button), settings_content);

	gtk_box_append(GTK_BOX(controls), prev_button);
	gtk_box_append(GTK_BOX(controls), play_button);
	gtk_box_append(GTK_BOX(controls), next_button);
	gtk_box_append(GTK_BOX(controls), time_label);
	gtk_box_append(GTK_BOX(controls), mode_dropdown);
	gtk_box_append(GTK_BOX(controls), spacer);
	gtk_box_append(GTK_BOX(controls), settings_button);

	/* 导入进度条：只在导入时显示 */
	progress_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar), TRUE);
	gtk_widget_set_visible(progress_bar, FALSE);

	status_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(status_label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_END);
	gtk_widget_add_css_class(status_label, "muted");

	gtk_box_append(GTK_BOX(bottom), title_label);
	gtk_box_append(GTK_BOX(bottom), seek_scale);
	gtk_box_append(GTK_BOX(bottom), controls);
	gtk_box_append(GTK_BOX(bottom), progress_bar);
	gtk_box_append(GTK_BOX(bottom), status_label);
	gtk_box_append(GTK_BOX(vbox), bottom);

	/* 恢复上次的播放模式 */
	gtk_drop_down_set_selected(GTK_DROP_DOWN(mode_dropdown),
				   (guint)cfg.play_mode);

	g_signal_connect(import_button, "clicked",
			 G_CALLBACK(on_import_clicked), NULL);
	g_signal_connect(prev_button, "clicked",
			 G_CALLBACK(on_prev_clicked), NULL);
	g_signal_connect(play_button, "clicked",
			 G_CALLBACK(on_play_clicked), NULL);
	g_signal_connect(next_button, "clicked",
			 G_CALLBACK(on_next_clicked), NULL);
	g_signal_connect(settings_button, "clicked",
			 G_CALLBACK(open_settings), NULL);
	g_signal_connect(mode_dropdown, "notify::selected",
			 G_CALLBACK(on_mode_changed), NULL);
	g_signal_connect(seek_scale, "value-changed",
			 G_CALLBACK(on_seek_changed), NULL);
	{
		GtkEventController *ec = gtk_event_controller_legacy_new();

		gtk_event_controller_set_propagation_phase(ec, GTK_PHASE_CAPTURE);
		g_signal_connect(ec, "event", G_CALLBACK(on_seek_event), NULL);
		gtk_widget_add_controller(seek_scale, ec);
	}

	gtk_window_present(GTK_WINDOW(window));
}

static void on_activate(GtkApplication *app, gpointer unused)
{
	settings_init_css();
	build_ui(app);
	apply_look();
	scan_library();
	rebuild_list();
	g_timeout_add(250, on_tick, NULL);
	if (tracks->len == 0)
		set_status("曲库为空：复制视频链接后点击右上角“导入”");
	else
		set_status("就绪");
}

int main(int argc, char **argv)
{
	GtkApplication *app;
	int status;

	gst_init(&argc, &argv);
	settings_load(&cfg);
	if (!player_init(on_eos, NULL)) {
		g_printerr("GStreamer playbin 初始化失败\n");
		return 1;
	}
	tracks = g_ptr_array_new_with_free_func(g_free);

	app = gtk_application_new("dev.lumigtk.app", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
	status = g_application_run(G_APPLICATION(app), argc, argv);

	g_object_unref(app);
	player_shutdown();
	g_ptr_array_unref(tracks);
	g_free(playing);
	g_free(cfg.wallpaper);
	return status;
}
