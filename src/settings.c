/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "settings.h"

#define HUE_BINS	12	/* 每 30 度一个色相区间 */
#define SAMPLE_SIZE	64
#define MIN_SAT		0.25
#define MIN_VAL		0.20

static const int default_colors[3][3] = {
	{ 251, 114, 153 },	/* 粉 */
	{ 95, 201, 160 },	/* 绿 */
	{ 255, 209, 102 },	/* 黄 */
};

static const int light_base[3] = { 246, 247, 250 };
static const int dark_base[3] = { 22, 24, 30 };
static const int light_fg[3] = { 28, 30, 36 };
static const int dark_fg[3] = { 232, 234, 240 };
static const int white[3] = { 255, 255, 255 };

static GtkCssProvider *css_provider;

static const char *static_rules =
	"window.solfa { background-color: @fm_bg; color: @fm_fg; }\n"
	".panel { background-color: @fm_panel; border-radius: 12px;"
	" padding: 10px; }\n"
	".panel viewport, .panel list, .panel row {"
	" background-color: transparent; background-image: none; }\n"
	"row { background-color: transparent; }\n"
	"row:hover { background-color: alpha(@fm_fg, 0.06); }\n"
	"row:selected { background-color: @fm_sel; }\n"
	".track-row { padding: 6px 10px; border-radius: 8px;"
	" background-color: transparent; }\n"
	".track-row:hover { background-color: alpha(@fm_fg, 0.08); }\n"
	".track-row.selected { background-color: @fm_sel; }\n"
	".fm-list, .fm-list > viewport { background-color: transparent;"
	" background-image: none; }\n"
	".fm-list row { background-color: transparent; }\n"
	".fm-list row:hover { background-color: alpha(@fm_fg, 0.06); }\n"
	".fm-list row:selected { background-color: @fm_sel; }\n"
	"headerbar { background-color: @fm_panel; color: @fm_fg; }\n"
	"windowcontrols button, windowcontrols image { color: @fm_fg; }\n"
	"windowcontrols button:hover { background-color: alpha(@fm_fg, 0.12); }\n"
	"button.accent { background-image: none; background-color: @fm_accent;"
	" color: @fm_on_accent; }\n"
	"button.accent:hover { background-color: @fm_accent2; }\n"
	".now-title { color: @fm_accent3; font-weight: bold;"
	" font-size: 1.15em; }\n"
	".muted { opacity: 0.75; }\n"
	"progressbar trough { background-color: alpha(@fm_fg, 0.12);"
	" border-radius: 6px; }\n"
	"progressbar progress { background-color: @fm_accent2;"
	" border-radius: 6px; }\n"
	"scale highlight { background-color: @fm_accent; }\n";


static void set_default_colors(struct app_settings *s)
{
	memcpy(s->colors, default_colors, sizeof(default_colors));
}

static char *settings_path(void)
{
	char *dir = config_dir();
	char *path = g_build_filename(dir, "settings.ini", NULL);

	g_free(dir);
	return path;
}

void settings_load(struct app_settings *s)
{
	GKeyFile *kf = g_key_file_new();
	char *path = settings_path();
	char *str;
	int i;

	memset(s, 0, sizeof(*s));
	s->panel_alpha = 0.88;
	s->play_mode = 0;
	set_default_colors(s);

	if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
		str = g_key_file_get_string(kf, "ui", "mode", NULL);
		if (str && strcmp(str, "dark") == 0)
			s->mode = THEME_DARK;
		g_free(str);

		str = g_key_file_get_string(kf, "ui", "wallpaper", NULL);
		if (str && g_file_test(str, G_FILE_TEST_IS_REGULAR))
			s->wallpaper = str;
		else
			g_free(str);

		if (g_key_file_has_key(kf, "ui", "panel_alpha", NULL))
			s->panel_alpha = g_key_file_get_double(kf, "ui",
							       "panel_alpha",
							       NULL);
		if (s->panel_alpha < 0.3)
			s->panel_alpha = 0.3;
		if (s->panel_alpha > 1.0)
			s->panel_alpha = 1.0;

		if (g_key_file_has_key(kf, "player", "play_mode", NULL))
			s->play_mode = g_key_file_get_integer(kf, "player",
							      "play_mode",
							      NULL);
		if (s->play_mode < 0 || s->play_mode > 3)
			s->play_mode = 0;

		s->autoplay_import = g_key_file_get_boolean(kf, "player",
							    "autoplay_import",
							    NULL);

		for (i = 0; i < 3; ++i) {
			char key[16];
			unsigned int ur, ug, ub;

			g_snprintf(key, sizeof(key), "color%d", i + 1);
			str = g_key_file_get_string(kf, "palette", key, NULL);
			if (str && sscanf(str, "#%2x%2x%2x", &ur, &ug, &ub) == 3) {
				s->colors[i][0] = (int)ur;
				s->colors[i][1] = (int)ug;
				s->colors[i][2] = (int)ub;
			}
			g_free(str);
		}
	}
	g_key_file_free(kf);
	g_free(path);
}

void settings_save(const struct app_settings *s)
{
	GKeyFile *kf = g_key_file_new();
	char *path = settings_path();
	int i;

	g_key_file_set_string(kf, "ui", "mode",
			      s->mode == THEME_DARK ? "dark" : "light");
	if (s->wallpaper)
		g_key_file_set_string(kf, "ui", "wallpaper", s->wallpaper);
	g_key_file_set_double(kf, "ui", "panel_alpha", s->panel_alpha);
	g_key_file_set_integer(kf, "player", "play_mode", s->play_mode);
	g_key_file_set_boolean(kf, "player", "autoplay_import",
			       s->autoplay_import);

	for (i = 0; i < 3; ++i) {
		char key[16];
		char *hex;

		g_snprintf(key, sizeof(key), "color%d", i + 1);
		hex = g_strdup_printf("#%02x%02x%02x", s->colors[i][0],
				      s->colors[i][1], s->colors[i][2]);
		g_key_file_set_string(kf, "palette", key, hex);
		g_free(hex);
	}

	g_key_file_save_to_file(kf, path, NULL);
	g_key_file_free(kf);
	g_free(path);
}

/* ---------- HSV 与色相分箱 ---------- */

static void rgb_to_hsv(double r, double g, double b, double *h, double *s,
		       double *v)
{
	double mx = r > g ? r : g;
	double mn = r < g ? r : g;
	double d;

	mx = mx > b ? mx : b;
	mn = mn < b ? mn : b;
	d = mx - mn;
	*v = mx;
	*s = mx > 0.0 ? d / mx : 0.0;
	if (d <= 0.0) {
		*h = 0.0;
		return;
	}
	if (mx == r)
		*h = 60.0 * ((g - b) / d);
	else if (mx == g)
		*h = 60.0 * ((b - r) / d + 2.0);
	else
		*h = 60.0 * ((r - g) / d + 4.0);
	if (*h < 0.0)
		*h += 360.0;
}

static void hsv_to_rgb(double h, double s, double v, int out[3])
{
	double sector = h / 60.0;
	int i = (int)sector;
	double f = sector - i;
	double p = v * (1.0 - s);
	double q = v * (1.0 - s * f);
	double t = v * (1.0 - s * (1.0 - f));
	double r, g, b;

	if (i < 0)
		i = 0;
	if (i > 5)
		i = 5;
	switch (i) {
	case 0:
		r = v; g = t; b = p;
		break;
	case 1:
		r = q; g = v; b = p;
		break;
	case 2:
		r = p; g = v; b = t;
		break;
	case 3:
		r = p; g = q; b = v;
		break;
	case 4:
		r = t; g = p; b = v;
		break;
	default:
		r = v; g = p; b = q;
		break;
	}
	out[0] = (int)(r * 255.0 + 0.5);
	out[1] = (int)(g * 255.0 + 0.5);
	out[2] = (int)(b * 255.0 + 0.5);
}

static int hue_bin(double h)
{
	int bin = (int)(h / 30.0);

	return bin >= HUE_BINS ? HUE_BINS - 1 : bin;
}

static int bin_distance(int a, int b)
{
	int d = a > b ? a - b : b - a;

	return d > HUE_BINS / 2 ? HUE_BINS - d : d;
}

/* 与已选颜色的色相至少相隔 2 个区间（约 60 度） */
static bool far_enough(int bin, const int *chosen, int n)
{
	int i;

	for (i = 0; i < n; ++i) {
		if (bin_distance(bin, chosen[i]) < 2)
			return false;
	}
	return true;
}

/*
 * acc[bin] = { 加权 r, 加权 g, 加权 b, 权重 }，r/g/b 取值 0..1。
 * 选出权重最大的三个色相差异明显的颜色；不足三色时按色环旋转补齐。
 */
static bool pick_palette(const double acc[HUE_BINS][4], int out[3][3])
{
	int order[HUE_BINS];
	int chosen[3];
	double hue[3], sat[3], val[3];
	double total = 0.0;
	int n = 0;
	int i, j;

	for (i = 0; i < HUE_BINS; ++i) {
		order[i] = i;
		total += acc[i][3];
	}
	if (total <= 0.0)
		return false;

	for (i = 0; i < HUE_BINS; ++i) {
		for (j = i + 1; j < HUE_BINS; ++j) {
			if (acc[order[j]][3] > acc[order[i]][3]) {
				int tmp = order[i];

				order[i] = order[j];
				order[j] = tmp;
			}
		}
	}

	for (i = 0; i < HUE_BINS && n < 3; ++i) {
		int b = order[i];
		double w = acc[b][3];
		double r, g, bl;

		if (w < total * 0.03)
			break;
		if (n > 0 && !far_enough(b, chosen, n))
			continue;
		chosen[n] = b;
		r = acc[b][0] / w;
		g = acc[b][1] / w;
		bl = acc[b][2] / w;
		rgb_to_hsv(r, g, bl, &hue[n], &sat[n], &val[n]);
		++n;
	}
	if (n == 0)
		return false;

	for (i = n; i < 3; ++i) {
		hue[i] = hue[0] + 120.0 * i;
		if (hue[i] >= 360.0)
			hue[i] -= 360.0;
		sat[i] = sat[0];
		val[i] = val[0];
	}

	/* 保证颜色鲜明，避免发灰或过暗 */
	for (i = 0; i < 3; ++i) {
		if (sat[i] < 0.55)
			sat[i] = 0.55;
		if (val[i] < 0.60)
			val[i] = 0.60;
		if (val[i] > 0.95)
			val[i] = 0.95;
		hsv_to_rgb(hue[i], sat[i], val[i], out[i]);
	}
	return true;
}

static bool palette_from_image(const char *path, int out[3][3])
{
	GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(path, SAMPLE_SIZE,
							  SAMPLE_SIZE, TRUE,
							  NULL);
	double acc[HUE_BINS][4];
	const guchar *px;
	int w, h, stride, nch, x, y;
	bool ok;

	if (!pb)
		return false;

	memset(acc, 0, sizeof(acc));
	w = gdk_pixbuf_get_width(pb);
	h = gdk_pixbuf_get_height(pb);
	stride = gdk_pixbuf_get_rowstride(pb);
	nch = gdk_pixbuf_get_n_channels(pb);
	px = gdk_pixbuf_get_pixels(pb);

	for (y = 0; y < h; ++y) {
		for (x = 0; x < w; ++x) {
			const guchar *p = px + y * stride + x * nch;
			double r = p[0] / 255.0;
			double g = p[1] / 255.0;
			double b = p[2] / 255.0;
			double hue, sat, val, wgt;
			int k;

			rgb_to_hsv(r, g, b, &hue, &sat, &val);
			if (sat < MIN_SAT || val < MIN_VAL)
				continue;
			wgt = sat * val;
			k = hue_bin(hue);
			acc[k][0] += r * wgt;
			acc[k][1] += g * wgt;
			acc[k][2] += b * wgt;
			acc[k][3] += wgt;
		}
	}
	g_object_unref(pb);
	ok = pick_palette(acc, out);
	return ok;
}

void settings_set_wallpaper(struct app_settings *s, const char *path)
{
	int colors[3][3];

	g_free(s->wallpaper);
	s->wallpaper = g_strdup(path);
	if (palette_from_image(path, colors))
		memcpy(s->colors, colors, sizeof(colors));
	else
		set_default_colors(s);
}

void settings_clear_wallpaper(struct app_settings *s)
{
	g_clear_pointer(&s->wallpaper, g_free);
}

/* ---------- 样式表 ---------- */

void settings_init_css(void)
{
	css_provider = gtk_css_provider_new();
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(), GTK_STYLE_PROVIDER(css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void def_color(GString *out, const char *name, const int c[3],
		      double alpha)
{
	if (alpha < 0.0)
		g_string_append_printf(out, "@define-color %s rgb(%d,%d,%d);\n",
				       name, c[0], c[1], c[2]);
	else
		g_string_append_printf(out,
				       "@define-color %s rgba(%d,%d,%d,%.2f);\n",
				       name, c[0], c[1], c[2], alpha);
}

static void mix(const int base[3], const int c[3], double t, int out[3])
{
	int i;

	for (i = 0; i < 3; ++i)
		out[i] = (int)(base[i] * (1.0 - t) + c[i] * t + 0.5);
}

static bool is_light(const int c[3])
{
	return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2] > 150.0;
}

void settings_apply(const struct app_settings *s)
{
	bool dark = s->mode == THEME_DARK;
	const int *base = dark ? dark_base : light_base;
	const int *fg = dark ? dark_fg : light_fg;
	const int *on = is_light(s->colors[0]) ? light_fg : white;
	int bg[3];
	GString *css = g_string_new(NULL);
	char *uri = NULL;

	if (!css_provider)
		return;

	mix(base, s->colors[0], 0.08, bg);
	if (s->wallpaper)
		uri = g_filename_to_uri(s->wallpaper, NULL, NULL);

	def_color(css, "fm_bg", bg, -1.0);
	def_color(css, "fm_panel", bg, s->panel_alpha);
	def_color(css, "fm_fg", fg, -1.0);
	def_color(css, "fm_accent", s->colors[0], -1.0);
	def_color(css, "fm_accent2", s->colors[1], -1.0);
	def_color(css, "fm_accent3", s->colors[2], -1.0);
	def_color(css, "fm_on_accent", on, -1.0);
	def_color(css, "fm_sel", s->colors[1], 0.35);

	if (uri)
		g_string_append_printf(css,
			"window.solfa.wallpaper { background-image: url(\"%s\");"
			" background-size: cover; background-position: center; }\n",
			uri);
	g_string_append(css, static_rules);

	gtk_css_provider_load_from_string(css_provider, css->str);
	g_object_set(gtk_settings_get_default(),
		     "gtk-application-prefer-dark-theme", dark, NULL);

	g_free(uri);
	g_string_free(css, TRUE);
}
