/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LUMIGTK_PLAYER_H
#define LUMIGTK_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*player_eos_fn)(void *data);

bool player_init(player_eos_fn on_eos, void *data);
void player_shutdown(void);
bool player_open(const char *path);
void player_toggle(void);
void player_stop(void);
bool player_is_playing(void);
int64_t player_position(void);	/* 纳秒，失败返回 -1 */
int64_t player_duration(void);	/* 纳秒，失败返回 -1 */
void player_seek(int64_t ns);
void player_set_mute(bool mute);

#endif
