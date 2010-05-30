/*
 * gitg-utils.c
 * This file is part of gitg - git repository viewer
 *
 * Copyright (C) 2009 - Jesse van den Kieboom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <glib.h>
#include <stdlib.h>
#include <gconf/gconf-client.h>
#include <math.h>

#include "gitg-utils.h"
#include "gitg-dirs.h"

inline static guint8
atoh(gchar c)
{
	if (c >= 'a')
		return c - 'a' + 10;
	if (c >= 'A')
		return c - 'A' + 10;

	return c - '0';
}

void
gitg_utils_partial_sha1_to_hash(gchar const *sha, gint length, gchar *hash)
{
	if (length % 2 == 1)
	{
		--length;
	}

	int i;

	for (i = 0; i < length / 2; ++i)
	{
		gchar h = atoh(*(sha++)) << 4;
		hash[i] = h | atoh(*(sha++));
	}
}

void
gitg_utils_sha1_to_hash(gchar const *sha, gchar *hash)
{
	gitg_utils_partial_sha1_to_hash (sha, HASH_SHA_SIZE, hash);
}

void
gitg_utils_hash_to_sha1(gchar const *hash, gchar *sha)
{
	char const *repr = "0123456789abcdef";
	int i;
	int pos = 0;

	for (i = 0; i < HASH_BINARY_SIZE; ++i)
	{
		sha[pos++] = repr[(hash[i] >> 4) & 0x0f];
		sha[pos++] = repr[(hash[i] & 0x0f)];
	}
}

gchar *
gitg_utils_hash_to_sha1_new(gchar const *hash)
{
	gchar *ret = g_new(gchar, HASH_SHA_SIZE + 1);
	gitg_utils_hash_to_sha1(hash, ret);

	ret[HASH_SHA_SIZE] = '\0';
	return ret;
}

gchar *
gitg_utils_partial_sha1_to_hash_new (gchar const *sha, gint length, gint *retlen)
{
	if (length == -1)
	{
		length = strlen (sha);
	}

	if (length % 2 != 0)
	{
		--length;
	}

	*retlen = length / 2;
	gchar *ret = g_new (gchar, *retlen);

	gitg_utils_partial_sha1_to_hash (sha, length, ret);

	return ret;
}

gchar *
gitg_utils_sha1_to_hash_new(gchar const *sha1)
{
	gchar *ret = g_new(gchar, HASH_BINARY_SIZE);
	gitg_utils_sha1_to_hash(sha1, ret);

	return ret;
}

GFile *
gitg_utils_find_dot_git (GFile *location)
{
	location = g_file_dup (location);

	do
	{
		GFile *tmp;
		gboolean exists;

		tmp = g_file_get_child (location, ".git");
		exists = g_file_query_exists (tmp, NULL);

		if (exists)
		{
			g_object_unref (location);
			location = tmp;

			break;
		}

		g_object_unref (tmp);

		tmp = g_file_get_parent (location);

		g_object_unref (location);
		location = tmp;
	} while (location != NULL);

	return location;
}

static void
append_escape (GString *gstr, gchar const *item)
{
	gchar *escape = g_shell_quote (item);

	g_string_append_printf (gstr, " %s", escape);
	g_free (escape);
}

gboolean
gitg_utils_export_files (GitgRepository *repository,
                         GitgRevision *revision,
                         gchar const *todir,
                         gchar * const *paths)
{
	GString *gstr = g_string_new("sh -c \"git --git-dir");

	GFile *git_dir = gitg_repository_get_git_dir (repository);
	gchar *git_path = g_file_get_path (git_dir);

	append_escape (gstr, git_path);

	g_free (git_path);
	g_object_unref (git_dir);

	// Append the revision
	gchar *sha = gitg_revision_get_sha1 (revision);
	g_string_append_printf (gstr, " archive --format=tar %s", sha);
	g_free(sha);

	// Append the files
	while (*paths)
	{
		append_escape (gstr, *paths);
		paths++;
	}

	g_string_append (gstr, " | tar -xC");
	append_escape (gstr, todir);
	g_string_append (gstr, "\"");

	GError *error = NULL;
	gint status;

	gboolean ret = g_spawn_command_line_sync (gstr->str, NULL, NULL, &status, &error);

	if (!ret)
	{
		g_warning ("Export failed:\n%s\n%s", gstr->str, error->message);
		g_error_free (error);
	}

	g_string_free (gstr, TRUE);
	return ret;
}

static void
utf8_validate_fallback (gchar  *text,
                        gssize  size)
{
	gchar const *end;

	while (!g_utf8_validate (text, size, &end))
	{
		*((gchar *)end) = '?';
	}
}

static gchar *
convert_fallback (gchar const *text,
                  gssize       size,
                  gchar const *fallback)
{
	gchar *res;
	gsize read, written;
	GString *str = g_string_new ("");

	while ((res = g_convert(text,
	                        size,
	                        "UTF-8",
	                        "ASCII",
	                        &read,
	                        &written,
	                        NULL)) == NULL)
	{
		res = g_convert (text, read, "UTF-8", "ASCII", NULL, NULL, NULL);
		str = g_string_append (str, res);

		str = g_string_append (str, fallback);
		text = text + read + 1;
		size = size - read;
	}

	str = g_string_append (str, res);
	g_free (res);

	utf8_validate_fallback (str->str, str->len);
	return g_string_free (str, FALSE);
}

gchar *
gitg_utils_convert_utf8 (gchar const *str, gssize size)
{
	static gchar *encodings[] = {
		"ISO-8859-15",
		"ASCII"
	};

	if (str == NULL)
	{
		return NULL;
	}

	if (size == -1)
	{
		size = strlen (str);
	}

	if (g_utf8_validate (str, size, NULL))
	{
		return g_strndup (str, size);
	}

	int i;
	for (i = 0; i < sizeof (encodings) / sizeof (gchar *); ++i)
	{
		gsize read;
		gsize written;

		gchar *ret = g_convert (str,
		                        size,
		                        "UTF-8",
		                        encodings[i],
		                        &read,
		                        &written,
		                        NULL);

		if (ret && read == size)
		{
			utf8_validate_fallback (ret, written);
			return ret;
		}

		g_free (ret);
	}

	return convert_fallback (str, size, "?");
}

guint
gitg_utils_hash_hash(gconstpointer v)
{
	/* 31 bit hash function, copied from g_str_hash */
	const signed char *p = v;
	guint32 h = *p;
	int i;

	for (i = 1; i < HASH_BINARY_SIZE; ++i)
		h = (h << 5) - h + p[i];

	return h;
}

gboolean 
gitg_utils_hash_equal(gconstpointer a, gconstpointer b)
{
	return memcmp(a, b, HASH_BINARY_SIZE) == 0;
}

gint
gitg_utils_null_length(gconstpointer *ptr)
{
	gint ret = 0;

	while (*ptr++)
		++ret;

	return ret;
}

gchar *
gitg_utils_get_content_type(GFile *file)
{
	GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, NULL, NULL);

	if (!info || !g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE))
		return NULL;

	gchar *content_type = g_strdup(g_file_info_get_content_type(info));
	g_object_unref(info);

	return content_type;
}

gboolean
gitg_utils_can_display_content_type (gchar const *content_type)
{
	return g_content_type_is_a (content_type, "text/plain") ||
	       g_content_type_equals (content_type, "application/octet-stream");
}

GtkSourceLanguage *
gitg_utils_get_language(gchar const *filename, gchar const *content_type)
{
	if (!gitg_utils_can_display_content_type(content_type))
		return NULL;

	GtkSourceLanguageManager *manager = gtk_source_language_manager_get_default();
	return gtk_source_language_manager_guess_language(manager, filename, content_type);
}

gint 
gitg_utils_sort_names(gchar const *s1, gchar const *s2)
{
	if (s1 == NULL)
		return -1;

	if (s2 == NULL)
		return 1;

	gchar *c1 = s1 ? g_utf8_casefold(s1, -1) : NULL;
	gchar *c2 = s2 ? g_utf8_casefold(s2, -1) : NULL;

	gint ret = g_utf8_collate(c1, c2);

	g_free(c1);
	g_free(c2);

	return ret;
}

gchar *
gitg_utils_guess_content_type(GtkTextBuffer *buffer)
{
	GtkTextIter start;
	GtkTextIter end;

	gtk_text_buffer_get_start_iter(buffer, &start);
	end = start;

	gtk_text_iter_forward_chars(&end, 256);
	gchar *data = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

	gchar *content_type = g_content_type_guess(NULL, (guchar *)data, strlen(data), NULL);
	g_free(data);

	return content_type;
}

/* Copied from gedit-utils.c */
void
gitg_utils_menu_position_under_widget (GtkMenu  *menu,
					gint     *x,
					gint     *y,
					gboolean *push_in,
					gpointer  user_data)
{
	GtkWidget *w = GTK_WIDGET (user_data);
	GtkRequisition requisition;

	gdk_window_get_origin (gtk_widget_get_window (w), x, y);
	gtk_widget_size_request (GTK_WIDGET (menu), &requisition);

	GtkAllocation alloc;
	gtk_widget_get_allocation (w, &alloc);

	if (gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL)
	{
		*x += alloc.x + alloc.width - requisition.width;
	}
	else
	{
		*x += alloc.x;
	}

	*y += alloc.y + alloc.height;
	*push_in = TRUE;
}

void
gitg_utils_menu_position_under_tree_view (GtkMenu  *menu,
					   gint     *x,
					   gint     *y,
					   gboolean *push_in,
					   gpointer  user_data)
{
	GtkTreeView *tree = GTK_TREE_VIEW (user_data);
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	model = gtk_tree_view_get_model (tree);
	g_return_if_fail (model != NULL);

	selection = gtk_tree_view_get_selection (tree);
	g_return_if_fail (selection != NULL);

	if (gtk_tree_selection_get_selected (selection, NULL, &iter))
	{
		GtkTreePath *path;
		GdkRectangle rect;

		gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (tree)), x, y);

		path = gtk_tree_model_get_path (model, &iter);
		gtk_tree_view_get_cell_area (tree, path,
					     gtk_tree_view_get_column (tree, 0), /* FIXME 0 for RTL ? */
					     &rect);
		gtk_tree_path_free (path);

		*x += rect.x;
		*y += rect.y + rect.height;

		if (gtk_widget_get_direction (GTK_WIDGET (tree)) == GTK_TEXT_DIR_RTL)
		{
			GtkRequisition requisition;
			gtk_widget_size_request (GTK_WIDGET (menu), &requisition);
			*x += rect.width - requisition.width;
		}
	}
	else
	{
		/* no selection -> regular "under widget" positioning */
		gitg_utils_menu_position_under_widget (menu,
							x, y, push_in,
							tree);
	}
}

gchar *
gitg_utils_get_monospace_font_name (void)
{
	GConfClient *client = gconf_client_get_default();
	gchar *name = gconf_client_get_string(client, "/desktop/gnome/interface/monospace_font_name", NULL);

	g_object_unref(client);

	return name;
}

void 
gitg_utils_set_monospace_font(GtkWidget *widget)
{
	gchar *name = gitg_utils_get_monospace_font_name();

	if (name)
	{
		PangoFontDescription *description = pango_font_description_from_string(name);

		if (description)
		{
			gtk_widget_modify_font(widget, description);
			pango_font_description_free(description);
		}
	}

	g_free(name);
}

GtkBuilder *
gitg_utils_new_builder(gchar const *filename)
{
	GtkBuilder *b = gtk_builder_new();
	GError *error = NULL;

	gchar *path = gitg_dirs_get_data_filename("ui", filename, NULL);

	if (!gtk_builder_add_from_file(b, path, &error))
	{
		g_critical("Could not open UI file: %s (%s)", path, error->message);
		g_error_free(error);

		g_free(path);
		exit(1);
	}

	g_free(path);
	return b;
}

gchar *
gitg_utils_timestamp_to_str(guint64 timestamp)
{
	time_t t = timestamp;

	struct tm *tms = localtime(&t);
	gchar buf[255];

	strftime(buf, 254, "%c", tms);
	return gitg_utils_convert_utf8(buf, -1);
}

GtkCellRenderer *
gitg_utils_find_cell_at_pos (GtkTreeView *tree_view, GtkTreeViewColumn *column, GtkTreePath *path, gint x)
{
	GList *cells;
	GList *item;
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_tree_view_get_model (tree_view);

	gtk_tree_model_get_iter (model, &iter, path);

	gtk_tree_view_column_cell_set_cell_data (column, model, &iter, FALSE, FALSE);

	cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
	GtkCellRenderer *ret = NULL;

	for (item = cells; item; item = g_list_next (item))
	{
		GtkCellRenderer *renderer = GTK_CELL_RENDERER (item->data);
		gint start;
		gint width;

		if (!gtk_tree_view_column_cell_get_position (column, renderer, &start, &width))
		{
			continue;
		}

		gtk_cell_renderer_get_size (renderer, GTK_WIDGET (tree_view), NULL, NULL, NULL, &width, 0);

		if (x >= start && x <= start + width)
		{
			ret = renderer;
			break;
		}
	}

	g_list_free (cells);
	return ret;
}

typedef struct
{
	gint position;
	gboolean reversed;
} PanedRestoreInfo;

static void
free_paned_restore_info (PanedRestoreInfo *info)
{
	g_slice_free (PanedRestoreInfo, info);
}

static void
paned_set_position (GtkPaned *paned, gint position, gboolean reversed)
{
	if (position == -1)
	{
		return;
	}

	if (!reversed)
	{
		gtk_paned_set_position (paned, position);
	}
	else
	{
		GtkAllocation alloc;
		gtk_widget_get_allocation (GTK_WIDGET (paned), &alloc);

		gtk_paned_set_position (paned, alloc.width - position);
	}
}

static void
on_paned_mapped (GtkPaned *paned, PanedRestoreInfo *info)
{
	paned_set_position (paned, info->position, info->reversed);

	g_signal_handlers_disconnect_by_func (paned, on_paned_mapped, info);
}

void
gitg_utils_restore_pane_position (GtkPaned *paned, gint position, gboolean reversed)
{
	g_return_if_fail (GTK_IS_PANED (paned));

	if (gtk_widget_get_mapped (GTK_WIDGET (paned)))
	{
		paned_set_position (paned, position, reversed);

		return;
	}

	PanedRestoreInfo *info = g_slice_new (PanedRestoreInfo);
	info->position = position;
	info->reversed = reversed;

	g_signal_connect_data (paned,
	                       "map",
	                       G_CALLBACK (on_paned_mapped), 
	                       info,
	                       (GClosureNotify)free_paned_restore_info,
	                       G_CONNECT_AFTER);
}

gchar *
gitg_utils_rewrite_hunk_counters (gchar const *header,
                                  guint old_count,
                                  guint new_count)
{
	if (!header)
	{
		return NULL;
	}

	gchar *copy = g_strdup (header);
	gchar *ptr1 = g_utf8_strchr (copy, -1, ',');

	if (!ptr1)
	{
		g_free (copy);
		return NULL;
	}

	gchar *ptrs1 = g_utf8_strchr (ptr1 + 1, -1, ' ');

	if (!ptrs1)
	{
		g_free (copy);
		return NULL;
	}

	gchar *ptr2 = g_utf8_strchr (ptrs1 + 1, -1, ',');

	if (!ptr2)
	{
		g_free (copy);
		return NULL;
	}

	gchar *ptrs2 = g_utf8_strchr (ptr2 + 1, -1, ' ');

	if (!ptrs2)
	{
		g_free (copy);
		return NULL;
	}

	*ptr1 = *ptr2 = '\0';

	gchar *ret;

	ret = g_strdup_printf ("%s,%d%s,%d%s",
	                       copy,
	                       old_count,
	                       ptrs1,
	                       new_count,
	                       ptrs2);

	g_free (copy);
	return ret;
}

void
gitg_utils_rounded_rectangle(cairo_t *ctx, gdouble x, gdouble y, gdouble width, gdouble height, gdouble radius)
{
	cairo_move_to (ctx, x + radius, y);
	cairo_rel_line_to (ctx, width - 2 * radius, 0);
	cairo_arc (ctx, x + width - radius, y + radius, radius, 1.5 * M_PI, 0.0);

	cairo_rel_line_to (ctx, 0, height - 2 * radius);
	cairo_arc (ctx, x + width - radius, y + height - radius, radius, 0.0, 0.5 * M_PI);

	cairo_rel_line_to (ctx, -(width - radius * 2), 0);
	cairo_arc (ctx, x + radius, y + height - radius, radius, 0.5 * M_PI, M_PI);

	cairo_rel_line_to (ctx, 0, -(height - radius * 2));
	cairo_arc (ctx, x + radius, y + radius, radius, M_PI, 1.5 * M_PI);
}
