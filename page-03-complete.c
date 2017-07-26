/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "pages.h"
#include <string.h>
#include <stdlib.h>

struct _PageComplete
{
	CmkWidget parent;
	ClutterActor *termBorder;
	CmkScrollBox *termScroll;
	CmkLabel *termText;
	CmkButton *nextButton;
};

static void on_dispose(GObject *self_);
static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static void on_next_button_activate(PageComplete *self);

G_DEFINE_TYPE(PageComplete, page_complete, CMK_TYPE_WIDGET);

CmkWidget * page_complete_new(void)
{
	return CMK_WIDGET(g_object_new(page_complete_get_type(), NULL));
}

static void page_complete_class_init(PageCompleteClass *class)
{
	//G_OBJECT_CLASS(class)->dispose = on_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = on_allocate;
}

static PageComplete *pageComplete;

void spawn_installer_process(const gchar *drive, const gchar *name, const gchar *username, const gchar *hostname, const gchar *password);

static void page_complete_init(PageComplete *self)
{
	pageComplete = self;

	self->termBorder = clutter_actor_new();
	const ClutterColor border = {180, 180, 180, 255};
	clutter_actor_set_background_color(self->termBorder, &border);
	clutter_actor_add_child(CLUTTER_ACTOR(self), self->termBorder);

	self->termScroll = cmk_scroll_box_new(CLUTTER_SCROLL_VERTICALLY);
	clutter_actor_set_background_color(CLUTTER_ACTOR(self->termScroll), clutter_color_get_static(CLUTTER_COLOR_BLACK));
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->termScroll), clutter_bin_layout_new(CLUTTER_BIN_ALIGNMENT_FILL, CLUTTER_BIN_ALIGNMENT_FILL));
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->termScroll));

	self->termText = cmk_label_new();
	cmk_label_set_font_face(self->termText, "Noto Mono");
	clutter_actor_add_child(CLUTTER_ACTOR(self->termScroll), CLUTTER_ACTOR(self->termText));

	self->nextButton = cmk_button_new_with_text("Abort Install", CMK_BUTTON_TYPE_RAISED);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->nextButton));
	g_signal_connect_swapped(self->nextButton, "activate", G_CALLBACK(on_next_button_activate), self);
	//spawn_installer_process("", "", "", "", "");
}

static void write_line(const gchar *line)
{
	g_return_if_fail(PAGE_IS_COMPLETE(pageComplete));
	const gchar *text = cmk_label_get_text(pageComplete->termText);
	gchar *new = g_strconcat(text, line, "\n", NULL);
	cmk_label_set_text(pageComplete->termText, new);
	g_free(new);
	cmk_scroll_box_scroll_to_bottom(pageComplete->termScroll);
}

static void on_read_line_async(GDataInputStream *stream, GAsyncResult *res, GSubprocess *proc)
{
	gsize len;
	GError *error = NULL;
	gchar *line = g_data_input_stream_read_line_finish(stream, res, &len, &error);

	if(!line)
	{
		if(error)
		{
			gchar *l = g_strdup_printf("I/O ERROR: %s", error->message);
			write_line(l);
			g_free(l);
			g_error_free(error);
		}
		else
		{
		}
		return;
	}

	if(g_str_has_prefix(line, "WAITING "))
	{
		const gchar *waiting = line + 8;
		const gchar *write = "";
		g_message("Waiting on '%s'", waiting);
		
		if(g_str_has_prefix(waiting, "dest"))
			write = "/dev/a17f1"; // TODO: Temporary hardcode to my flash drive //g_object_get_data(G_OBJECT(stream), "destination");
		else if(g_str_has_prefix(waiting, "packages"))
			write = ""; // TODO
		else if(g_str_has_prefix(waiting, "password"))
			write = g_object_get_data(G_OBJECT(stream), "password");
		else if(g_str_has_prefix(waiting, "locale"))
			write = ""; // TODO
		else if(g_str_has_prefix(waiting, "zone"))
			write = ""; // TODO
		else if(g_str_has_prefix(waiting, "hostname"))
			write = g_object_get_data(G_OBJECT(stream), "hostname");
		else if(g_str_has_prefix(waiting, "username"))
			write = g_object_get_data(G_OBJECT(stream), "username");
		else if(g_str_has_prefix(waiting, "services"))
			write = ""; // TODO
		
		GOutputStream *sin = g_subprocess_get_stdin_pipe(proc);
		GError *error = NULL;
		gsize bw = -1;
		gboolean s = g_output_stream_printf(sin, &bw, NULL, &error, "%s=%s\n", waiting, write);
		if(!s || error)
		{
			g_message("Error writing to installer: %s", error ? error->message : "No error");
		}
		g_output_stream_flush(sin, NULL, NULL);
		g_message("written %s (%i)", write, bw);
	}
	else if(g_str_has_prefix(line, "PROGRESS "))
	{
		const gchar *sprogress = line + 9;
		// TODO: Check errors
		double prog = strtod(sprogress, NULL);
		g_message("Progress: %f", prog);
	}
	else
	{
		write_line(line);
	}

	g_free(line);
	
	g_data_input_stream_read_line_async(stream,
		G_PRIORITY_DEFAULT,
		NULL,
		(GAsyncReadyCallback)on_read_line_async,
		NULL);
}

static void on_proc_complete(GSubprocess *proc, GAsyncResult *res, gpointer x)
{
	gboolean s = g_subprocess_wait_finish(proc, res, NULL);

	gboolean exited = g_subprocess_get_if_exited(proc);
	if(!exited)
	{
		write_line("Process aborted!");
		g_message("Process done: wait_finish: %i, exited: %i, exit_status: %i", s, exited, 0);
		return;
	}
	else
	{
		gint status = g_subprocess_get_exit_status(proc);
		if(status == 0)
			write_line("Installation complete!");
		else
			write_line("An error occurred during installation.");
		g_message("Process done: wait_finish: %i, exited: %i, exit_status: %i", s, exited, status);
	}
}

void spawn_installer_process(const gchar *drive, const gchar *name, const gchar *username, const gchar *hostname, const gchar *password)
{
	g_message("spawn %s, %s, %s, %s, %s", drive, name, username, hostname, password);

	GError *error = NULL;
	GSubprocess *proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE, &error, "pkexec", "/home/aidan/projects/vos-installer/build/cli/vos-install-cli", "--ext4", NULL);

	g_message("proc: %p %p %s", proc, error, error ? error->message : "");

	g_subprocess_wait_async(proc, NULL, (GAsyncReadyCallback)on_proc_complete, NULL);

	GInputStream *sout = g_subprocess_get_stdout_pipe(proc);
	GDataInputStream *stream = g_data_input_stream_new(sout);
	
	g_object_set_data_full(G_OBJECT(stream), "destination", g_strdup(drive), g_free);
	g_object_set_data_full(G_OBJECT(stream), "name", g_strdup(name), g_free);
	g_object_set_data_full(G_OBJECT(stream), "username", g_strdup(username), g_free);
	g_object_set_data_full(G_OBJECT(stream), "hostname", g_strdup(hostname), g_free);
	g_object_set_data_full(G_OBJECT(stream), "password", g_strdup(password), g_free);
	
	g_data_input_stream_read_line_async(stream,
		G_PRIORITY_DEFAULT,
		NULL,
		(GAsyncReadyCallback)on_read_line_async,
		proc);
}

static void on_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	PageComplete *self = PAGE_COMPLETE(self_);
	gfloat width = clutter_actor_box_get_width(box);
	gfloat height = clutter_actor_box_get_height(box);
	gfloat pad = CMK_DP(self, 30);

	ClutterActorBox termBorderBox = {
		pad,
		pad,
		width-pad,
		height-pad*3
	};
	clutter_actor_allocate(self->termBorder, &termBorderBox, flags);

	ClutterActorBox termScrollBox = {
		pad+CMK_DP(self, 1),
		pad+CMK_DP(self, 1),
		width-pad-CMK_DP(self, 1),
		height-pad*3-CMK_DP(self, 1)
	};
	clutter_actor_allocate(CLUTTER_ACTOR(self->termScroll), &termScrollBox, flags);

	gfloat natW, natH;
	clutter_actor_get_preferred_size(CLUTTER_ACTOR(self->nextButton), NULL, NULL, &natW, &natH);
	ClutterActorBox nextButton = {
		width-pad - natW,
		height-pad - natH,
		width-pad,
		height-pad
	};
	clutter_actor_allocate(CLUTTER_ACTOR(self->nextButton), &nextButton, flags);
	
	CLUTTER_ACTOR_CLASS(page_complete_parent_class)->allocate(self_, box, flags );
}

static void on_next_button_activate(PageComplete *self)
{
	clutter_main_quit();
}
