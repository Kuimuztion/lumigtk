/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SOLFA_SETTINGS_H
#define SOLFA_SETTINGS_H

#include <stdbool.h>

enum theme_mode {
	THEME_LIGHT,
	THEME_DARK,
};

struct app_settings {
	enum theme_mode mode;
	char *wallpaper;		/* 壁纸绝对路径，无壁纸时为 NULL */
	double panel_alpha;		/* 面板不透明度，0.3 ~ 1.0 */
	int play_mode;			/* 0 顺序，1 循环，2 随机 */
	bool autoplay_import;		/* 导入完成后自动播放 */
	int colors[3][3];		/* 配色：主色、辅色、强调色（RGB） */
};

/* 读取 ~/.config/solfa/settings.ini，缺失项使用默认值 */
void settings_load(struct app_settings *s);
void settings_save(const struct app_settings *s);

/* 设置壁纸并从图片中提取三色配色；图片无彩色时使用默认配色 */
void settings_set_wallpaper(struct app_settings *s, const char *path);
void settings_clear_wallpaper(struct app_settings *s);

/* 在 GDK 显示上创建 CSS 提供者，须在 GTK 初始化之后调用一次 */
void settings_init_css(void);

/* 根据当前设置重新生成并加载样式表 */
void settings_apply(const struct app_settings *s);

#endif
