/* SPDX-License-Identifier: GPL-3.0-only */
#include <gst/gst.h>
#include <stdbool.h>
#include <stdint.h>

#include "player.h"

static GstElement *playbin;
static player_eos_fn eos_fn;
static void *eos_data;
static bool want_play;

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer unused)
{
	GError *err = NULL;
	gchar *dbg = NULL;

	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		if (eos_fn)
			eos_fn(eos_data);
		break;
	case GST_MESSAGE_ERROR:
		gst_message_parse_error(msg, &err, &dbg);
		g_warning("播放错误: %s", err->message);
		g_clear_error(&err);
		g_free(dbg);
		break;
	default:
		break;
	}
	return G_SOURCE_CONTINUE;
}

bool player_init(player_eos_fn on_eos, void *data)
{
	GstBus *bus;

	eos_fn = on_eos;
	eos_data = data;
	playbin = gst_element_factory_make("playbin", "player");
	if (!playbin)
		return false;
	bus = gst_element_get_bus(playbin);
	gst_bus_add_watch(bus, bus_cb, NULL);
	gst_object_unref(bus);
	return true;
}

void player_shutdown(void)
{
	if (!playbin)
		return;
	gst_element_set_state(playbin, GST_STATE_NULL);
	gst_object_unref(playbin);
	playbin = NULL;
	want_play = false;
}

bool player_open(const char *path)
{
	char *uri;
	GstStateChangeReturn ret;

	if (!playbin)
		return false;
	uri = g_filename_to_uri(path, NULL, NULL);
	if (!uri)
		return false;
	gst_element_set_state(playbin, GST_STATE_NULL);
	g_object_set(playbin, "uri", uri, NULL);
	g_free(uri);
	ret = gst_element_set_state(playbin, GST_STATE_PLAYING);
	want_play = ret != GST_STATE_CHANGE_FAILURE;
	return want_play;
}

void player_toggle(void)
{
	if (!playbin)
		return;
	want_play = !want_play;
	gst_element_set_state(playbin,
			      want_play ? GST_STATE_PLAYING : GST_STATE_PAUSED);
}

void player_stop(void)
{
	want_play = false;
	if (playbin)
		gst_element_set_state(playbin, GST_STATE_NULL);
}

bool player_is_playing(void)
{
	return want_play;
}

int64_t player_position(void)
{
	gint64 pos = -1;

	if (!playbin || !gst_element_query_position(playbin, GST_FORMAT_TIME,
						    &pos))
		return -1;
	return pos;
}

int64_t player_duration(void)
{
	gint64 dur = -1;

	if (!playbin || !gst_element_query_duration(playbin, GST_FORMAT_TIME,
						    &dur))
		return -1;
	return dur;
}

void player_seek(int64_t ns)
{
	if (playbin)
		gst_element_seek_simple(playbin, GST_FORMAT_TIME,
					GST_SEEK_FLAG_FLUSH, ns);
}

void player_set_mute(bool mute)
{
	if (playbin)
		g_object_set(playbin, "mute", mute, NULL);
}
