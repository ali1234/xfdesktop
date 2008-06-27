/*
 *  xfdesktop
 *
 *  Copyright (c) 2008 Stephan Arts <stephan@xfce.org>
 *  Copyright (c) 2008 Brian Tarricone <bjt23@cornell.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include <libxfce4util/libxfce4util.h>
#include <xfconf/xfconf.h>
#include <libxfcegui4/libxfcegui4.h>

#include "xfdesktop-common.h"
#include "xfdesktop-settings_glade.h"

#define SHOW_DESKTOP_MENU_PROP               "/desktop-menu/show"

#define WINLIST_SHOW_WINDOWS_MENU_PROP       "/windowlist-menu/show"
#define WINLIST_SHOW_STICKY_WIN_ONCE_PROP    "/windowlist-menu/show-sticky-once"
#define WINLIST_SHOW_WS_NAMES_PROP           "/windowlist-menu/show-workspace-names"
#define WINLIST_SHOW_WS_SUBMENUS_PROP        "/windowlist-menu/show-submenus"

#define DESKTOP_ICONS_STYLE_PROP             "/desktop-icons/style"
#define DESKTOP_ICONS_ICON_SIZE_PROP         "/desktop-icons/icon-size"
#define DESKTOP_ICONS_FONT_SIZE_PROP         "/desktop-icons/font-size"
#define DESKTOP_ICONS_CUSTOM_FONT_SIZE_PROP  "/desktop-icons/use-custom-font-size"

#define PER_SCREEN_PROP_FORMAT               "/backdrop/screen%d/monitor%d"

enum
{
    COL_PIX = 0,
    COL_NAME,
    COL_FILENAME,
    COL_COLLATE_KEY,
    N_COLS,
};

enum
{
    COL_ICON_PIX = 0,
    COL_ICON_NAME,
    COL_ICON_ENABLED,
    N_ICON_COLS,
};

static void
setup_special_icon_list(GladeXML *gxml,
                        XfconfChannel *channel)
{
    GtkWidget *treeview;
    GtkListStore *ls;
    GtkTreeViewColumn *col;
    GtkCellRenderer *render;
    GtkTreeIter iter;
    const struct {
        gchar *name;
        gchar *icon;
        gchar *icon_fallback;
        gboolean state;
    } icons[] = {
        { N_("Home"), "user-home", "gnome-fs-desktop", TRUE },
        { N_("Filesystem"), "drive-harddisk", "gnome-dev-harddisk", TRUE },
        { N_("Trash"), "user-trash", "gnome-fs-trash-empty", TRUE },
        { N_("Removable Devices"), "drive-removable-media", "gnome-dev-removable", TRUE },
        { NULL, NULL, NULL, FALSE },
    };
    int i, w;
    GtkIconTheme *itheme = gtk_icon_theme_get_default();

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &w, NULL);

    ls = gtk_list_store_new(N_ICON_COLS, GDK_TYPE_PIXBUF, G_TYPE_STRING,
                            G_TYPE_BOOLEAN);
    for(i = 0; icons[i].name; ++i) {
        GdkPixbuf *pix = NULL;

        if(gtk_icon_theme_has_icon(itheme, icons[i].icon))
            pix = xfce_themed_icon_load(icons[i].icon, w);
        else
            pix = xfce_themed_icon_load(icons[i].icon_fallback, w);

        gtk_list_store_append(ls, &iter);
        gtk_list_store_set(ls, &iter,
                           COL_ICON_NAME, icons[i].name,
                           COL_ICON_PIX, pix,
                           COL_ICON_ENABLED, icons[i].state,
                           -1);
        if(pix)
            g_object_unref(G_OBJECT(pix));
    }

    treeview = glade_xml_get_widget(gxml, "treeview_default_icons");
    col = gtk_tree_view_column_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);

    render = gtk_cell_renderer_toggle_new();
    gtk_tree_view_column_pack_start(col, render, FALSE);
    gtk_tree_view_column_add_attribute(col, render, "active", COL_ICON_ENABLED);

    render = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, render, FALSE);
    gtk_tree_view_column_add_attribute(col, render, "pixbuf", COL_ICON_PIX);

    render = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, render, TRUE);
    gtk_tree_view_column_add_attribute(col, render, "text", COL_ICON_NAME);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(ls));
    g_object_unref(G_OBJECT(ls));
}

static gint
image_list_sort(GtkTreeModel *model,
                GtkTreeIter *a,
                GtkTreeIter *b,
                gpointer user_data)
{
    gchar *key_a = NULL, *key_b = NULL;
    gint ret;

    gtk_tree_model_get(model, a, COL_COLLATE_KEY, &key_a, -1);
    gtk_tree_model_get(model, b, COL_COLLATE_KEY, &key_b, -1);

    if(G_UNLIKELY(!key_a && !key_b))
        ret = 0;
    else if(G_UNLIKELY(!key_a))
        ret = -1;
    else if(G_UNLIKELY(!key_b))
        ret = 1;
    else
        ret = strcmp(key_a, key_b);

    g_free(key_a);
    g_free(key_b);

    return ret;
}

static GtkTreeIter *
xfdesktop_image_list_add_dir(GtkListStore *ls,
                             const char *path,
                             const char *cur_image_file)
{
    GDir *dir;
    gboolean needs_slash = TRUE;
    const gchar *file;
    GtkTreeIter iter, *iter_ret = NULL;
    gchar buf[PATH_MAX];

    dir = g_dir_open(path, 0, 0);
    if(!dir)
        return NULL;

    if(path[strlen(path)-1] == '/')
        needs_slash = FALSE;

    while((file = g_dir_read_name(dir))) {
        g_snprintf(buf, sizeof(buf), needs_slash ? "%s/%s" : "%s%s",
                   path, file);

        /* FIXME: this is probably too slow */
        if(xfdesktop_check_image_file(buf)) {
            gchar *name, *key = NULL;

            name = g_filename_to_utf8(file, strlen(file), NULL, NULL, NULL);
            if(name) {
                gchar *lower = g_utf8_strdown(name, -1);
                key = g_utf8_collate_key(lower, -1);
                g_free(lower);
            }

            /* FIXME: set image thumbnail */
            gtk_list_store_append(ls, &iter);
            gtk_list_store_set(ls, &iter,
                               COL_NAME, name,
                               COL_FILENAME, buf,
                               COL_COLLATE_KEY, key,
                               -1);

            if(cur_image_file && !strcmp(buf, cur_image_file))
                iter_ret = gtk_tree_iter_copy(&iter);

            g_free(name);
            g_free(key);
        }
    }

    g_dir_close(dir);

    return iter_ret;
}

static void
cb_image_selection_changed(GtkTreeSelection *sel,
                           gpointer user_data)
{
    GtkWidget *treeview = user_data;
    XfconfChannel *channel = g_object_get_data(G_OBJECT(treeview),
                                               "xfconf-channel");
    guint32 scmo_mask = (guint32)GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(treeview),
                                                                    "screen-monitor-mask"));
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    gchar *filename = NULL;
    gint screen = scmo_mask >> 16, monitor = scmo_mask & 0xffff;
    gchar buf[1024];

    TRACE("entering");

    if(!gtk_tree_selection_get_selected(sel, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, COL_FILENAME, &filename, -1);

    DBG("got %s, applying to screen %d monitor %d", filename, screen, monitor);

    g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/image-path",
               screen, monitor);
    xfconf_channel_set_string(channel, buf, filename);
    g_free(filename);
}

static void
xfdesktop_settings_dialog_populate_image_list(GladeXML *gxml,
                                              XfconfChannel *channel,
                                              gint screen,
                                              gint monitor)
{
    gchar buf[PATH_MAX], *image_file;
    GtkWidget *treeview;
    GtkListStore *ls;
    GtkTreeIter iter, *image_file_iter = NULL;
    gboolean do_sort = TRUE, connect_changed_signal = FALSE;
    GtkTreeSelection *sel;

    treeview = glade_xml_get_widget(gxml, "treeview_imagelist");
    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    ls = gtk_list_store_new(N_COLS, GDK_TYPE_PIXBUF, G_TYPE_STRING,
                            G_TYPE_STRING, G_TYPE_STRING);

    g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/image-path",
               screen, monitor);
    image_file = xfconf_channel_get_string(channel, buf, NULL);

    if(image_file && is_backdrop_list(image_file)) {
        gchar **images;

        do_sort = FALSE;

        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(glade_xml_get_widget(gxml,
                                                                            "radio_imagelist")),
                                     TRUE);
        gtk_widget_show(glade_xml_get_widget(gxml, "btn_image_remove"));

        images = get_list_from_file(image_file);
        if(images) {
            gint i;

            for(i = 0; images[i]; ++i) {
                gchar *name;

                /* FIXME: add image thumbnail */

                name = g_strrstr(images[i], G_DIR_SEPARATOR_S);
                if(name)
                    name++;
                else
                    name = images[i];
                name = g_filename_to_utf8(name, strlen(name), NULL, NULL, NULL);

                gtk_list_store_append(ls, &iter);
                gtk_list_store_set(ls, &iter,
                                   COL_NAME, name,
                                   COL_FILENAME, images[i],
                                   -1);

                g_free(name);
            }

            g_strfreev(images);
        }
    } else {
        GtkTreeIter *tmp;
        gchar *d;

        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(glade_xml_get_widget(gxml,
                                                                            "radio_singleimage")),
                                     TRUE);
        gtk_widget_hide(glade_xml_get_widget(gxml, "btn_image_remove"));

        tmp = xfdesktop_image_list_add_dir(ls,
                                           DATADIR "/xfce4/backdrops",
                                           image_file);
        if(tmp)
            image_file_iter = tmp;

        g_snprintf(buf, sizeof(buf), "%s/.local/share/xfce4/backdrops",
                   xfce_get_homedir());
        tmp = xfdesktop_image_list_add_dir(ls, buf, image_file);
        if(tmp)
            image_file_iter = tmp;

        d = xfce_resource_save_location(XFCE_RESOURCE_CONFIG,
                                        "xfce4/desktop/backdrops/", FALSE);
        if(d) {
            tmp = xfdesktop_image_list_add_dir(ls, d, image_file);
            if(tmp)
                image_file_iter = tmp;
            g_free(d);
        }

        d = xfce_resource_save_location(XFCE_RESOURCE_CONFIG, "xfce4/desktop/",
                                        FALSE);
        if(d) {
            tmp = xfdesktop_image_list_add_dir(ls, d, image_file);
            if(tmp)
                image_file_iter = tmp;
            g_free(d);
        }

        if(image_file && !image_file_iter) {
            gchar *name, *key = NULL;

            /* FIXME: set image thumbnail */

            name = g_strrstr(image_file, G_DIR_SEPARATOR_S);
            if(name)
                name++;
            else
                name = image_file;
            name = g_filename_to_utf8(name, strlen(name), NULL, NULL, NULL);

            if(name) {
                gchar *lower = g_utf8_strdown(name, -1);
                key = g_utf8_collate_key(lower, -1);
                g_free(lower);
            }

            gtk_list_store_append(ls, &iter);
            gtk_list_store_set(ls, &iter,
                               COL_NAME, name,
                               COL_FILENAME, image_file,
                               COL_COLLATE_KEY, key,
                               -1);
            image_file_iter = gtk_tree_iter_copy(&iter);

            g_free(name);
            g_free(key);
        }

        connect_changed_signal = TRUE;
    }

    if(do_sort) {
        gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(ls), COL_NAME,
                                        image_list_sort, NULL, NULL);
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(ls), COL_NAME,
                                             GTK_SORT_ASCENDING);
    }

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(ls));
    if(image_file_iter) {
        gtk_tree_selection_select_iter(sel, image_file_iter);
        gtk_tree_iter_free(image_file_iter);
    }
    g_object_unref(G_OBJECT(ls));

    if(connect_changed_signal) {
        g_signal_connect(G_OBJECT(sel), "changed",
                         G_CALLBACK(cb_image_selection_changed), treeview);
    }

    g_free(image_file);
}

static void
cb_xfdesktop_chk_custom_font_size_toggled(GtkCheckButton *button,
                                          gpointer user_data)
{
    GtkWidget *spin_button = GTK_WIDGET(user_data);
    gtk_widget_set_sensitive(spin_button,
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}

static void
cb_xfdesktop_combo_color_changed(GtkComboBox *combo,
                                 gpointer user_data)
{
    GtkWidget *color_button = GTK_WIDGET(user_data);

    if(gtk_combo_box_get_active(combo) > 1)
        gtk_widget_set_sensitive(color_button, TRUE);
    else
        gtk_widget_set_sensitive(color_button, FALSE);
}

static GtkWidget *
xfdesktop_settings_dialog_new(XfconfChannel *channel)
{
    gint i, j, nmonitors, nscreens;
    GladeXML *main_gxml;
    GtkWidget *dialog, *appearance_container, *chk_custom_font_size,
              *spin_font_size, *color_style_widget;

    main_gxml = glade_xml_new_from_buffer(xfdesktop_settings_glade,
                                          xfdesktop_settings_glade_length,
                                          "prefs_dialog", NULL);

    dialog = glade_xml_get_widget(main_gxml, "prefs_dialog");
    appearance_container = glade_xml_get_widget(main_gxml,
                                                "notebook_screens");
    gtk_widget_destroy(glade_xml_get_widget(main_gxml,
                                            "alignment_settings"));

    chk_custom_font_size = glade_xml_get_widget(main_gxml,
                                                "chk_custom_font_size");
    spin_font_size = glade_xml_get_widget(main_gxml, "spin_font_size");

    g_signal_connect(G_OBJECT(chk_custom_font_size), "toggled",
                     G_CALLBACK(cb_xfdesktop_chk_custom_font_size_toggled),
                     spin_font_size);

    nscreens = gdk_display_get_n_screens(gdk_display_get_default());

    for(i = 0; i < nscreens; ++i) {
        GdkDisplay *gdpy = gdk_display_get_default();
        nmonitors = gdk_screen_get_n_monitors(gdk_display_get_screen(gdpy, i));

        if(nscreens > 1 || nmonitors > 1)
            gtk_notebook_set_show_tabs(GTK_NOTEBOOK(appearance_container), TRUE);

        for(j = 0; j < nmonitors; ++j) {
            gchar buf[1024];
            GladeXML *appearance_gxml;
            GtkWidget *appearance_settings, *appearance_label,
                      *colorbtn_second, *treeview;
            GtkCellRenderer *render;
            GtkTreeViewColumn *col;

            if(nscreens > 1 && nmonitors > 1) {
                g_snprintf(buf, sizeof(buf), _("Screen %d, Monitor %d"),
                           nscreens, nmonitors);
            } else {
                if(nscreens > 1)
                    g_snprintf(buf, sizeof(buf), _("Screen %d"), nscreens);
                else
                    g_snprintf(buf, sizeof(buf), _("Monitor %d"), nmonitors);
            }

            appearance_gxml = glade_xml_new_from_buffer(xfdesktop_settings_glade,
                                                        xfdesktop_settings_glade_length,
                                                        "alignment_settings", NULL);
            appearance_settings = glade_xml_get_widget(appearance_gxml,
                                                       "alignment_settings");
            appearance_label = gtk_label_new_with_mnemonic(buf);
            gtk_widget_show(appearance_label);

            gtk_notebook_append_page(GTK_NOTEBOOK(appearance_container),
                                     appearance_settings, appearance_label);

            /* Connect xfconf bindings */
            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/brightness",
                       i, j);
            xfconf_g_property_bind(channel, buf, G_TYPE_INT,
                                   G_OBJECT(gtk_range_get_adjustment(GTK_RANGE(glade_xml_get_widget(appearance_gxml,
                                                                                                    "slider_brightness")))),
                                   "value");

            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/saturation",
                       i, j);
            xfconf_g_property_bind(channel, buf, G_TYPE_DOUBLE,
                                   G_OBJECT(gtk_range_get_adjustment(GTK_RANGE(glade_xml_get_widget(appearance_gxml,
                                                                                                    "slider_saturation")))),
                                   "value");

            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/image-style",
                       i, j);
            xfconf_g_property_bind(channel, buf, G_TYPE_INT,
                                   G_OBJECT(glade_xml_get_widget(appearance_gxml,
                                                                 "combo_style")),
                                   "active");

            color_style_widget = glade_xml_get_widget(appearance_gxml,
                                                      "combo_colors");
            g_snprintf(buf, sizeof(buf), PER_SCREEN_PROP_FORMAT "/color-style",
                       i, j);
            xfconf_g_property_bind(channel, buf, G_TYPE_INT,
                                   G_OBJECT(color_style_widget), "active");

            colorbtn_second = glade_xml_get_widget(appearance_gxml,
                                                   "colorbtn_second");
            g_signal_connect(G_OBJECT(color_style_widget), "changed",
                             G_CALLBACK(cb_xfdesktop_combo_color_changed),
                             colorbtn_second);

            treeview = glade_xml_get_widget(appearance_gxml,
                                            "treeview_imagelist");
            render = gtk_cell_renderer_pixbuf_new();
            col = gtk_tree_view_column_new_with_attributes("thumbnail", render,
                                                           "pixbuf", COL_PIX, NULL);
            gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
            render = gtk_cell_renderer_text_new();
            col = gtk_tree_view_column_new_with_attributes("name", render,
                                                           "text", COL_NAME, NULL);
            gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);

            xfdesktop_settings_dialog_populate_image_list(appearance_gxml,
                                                          channel, i, j);

            g_object_set_data(G_OBJECT(treeview), "xfconf-channel", channel);
            g_object_set_data(G_OBJECT(treeview), "screen-monitor-mask",
                              GUINT_TO_POINTER((i << 16) | (j)));
        }
    }

    xfconf_g_property_bind(channel, SHOW_DESKTOP_MENU_PROP, G_TYPE_BOOLEAN,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "chk_show_desktop_menu")),
                           "active");
    xfconf_g_property_bind(channel, WINLIST_SHOW_WINDOWS_MENU_PROP,
                           G_TYPE_BOOLEAN,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "chk_show_winlist_menu")),
                           "active");
    xfconf_g_property_bind(channel, WINLIST_SHOW_STICKY_WIN_ONCE_PROP,
                           G_TYPE_BOOLEAN,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "chk_show_winlist_sticky_once")),
                           "active");
    xfconf_g_property_bind(channel, WINLIST_SHOW_WS_NAMES_PROP, G_TYPE_BOOLEAN,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "chk_show_winlist_ws_names")),
                           "active");
    xfconf_g_property_bind(channel, WINLIST_SHOW_WS_SUBMENUS_PROP,
                           G_TYPE_BOOLEAN,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "chk_show_winlist_ws_submenus")),
                           "active");
    xfconf_g_property_bind(channel, DESKTOP_ICONS_STYLE_PROP, G_TYPE_INT,
                           G_OBJECT(glade_xml_get_widget(main_gxml,
                                                         "combo_icons")),
                           "active");
    xfconf_g_property_bind(channel, DESKTOP_ICONS_ICON_SIZE_PROP, G_TYPE_UINT,
                           G_OBJECT(gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(glade_xml_get_widget(main_gxml,
                                                                                                        "spin_icon_size")))),
                           "value");
    xfconf_g_property_bind(channel, DESKTOP_ICONS_FONT_SIZE_PROP, G_TYPE_UINT,
                           G_OBJECT(gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin_font_size))),
                           "value");
    xfconf_g_property_bind(channel, DESKTOP_ICONS_CUSTOM_FONT_SIZE_PROP,
                           G_TYPE_BOOLEAN, G_OBJECT(chk_custom_font_size),
                           "active");

    setup_special_icon_list(main_gxml, channel);
    
    return dialog;
}

int
main(int argc, char **argv)
{
    XfconfChannel *channel;
    GtkWidget *dialog;
    GError *error = NULL;

    gtk_init(&argc, &argv);

    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    if(!xfconf_init(&error)) {
        xfce_message_dialog(NULL, _("Desktop Settings"),
                            GTK_STOCK_DIALOG_ERROR,
                            _("Unable to contact settings server"),
                            error->message,
                            GTK_STOCK_QUIT, GTK_RESPONSE_ACCEPT,
                            NULL);
        g_error_free(error);
        return 1;
    }

    channel = xfconf_channel_new(XFDESKTOP_CHANNEL);
    dialog = xfdesktop_settings_dialog_new(channel);

    while(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_HELP) {
        /* show help... */
    }
    
    xfconf_shutdown();

    return 0;
}