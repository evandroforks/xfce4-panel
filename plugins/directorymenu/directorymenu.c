/*
 * Copyright (C) 2010 Nick Schermer <nick@xfce.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>
#include <exo/exo.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4panel/libxfce4panel.h>
#include <common/panel-xfconf.h>
#include <common/panel-builder.h>
#include <common/panel-private.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gdesktopappinfo.h>
#endif

#include "directorymenu.h"
#include "directorymenu-dialog_ui.h"

#define DEFAULT_ICON_NAME "folder"


struct _DirectoryMenuPluginClass
{
  XfcePanelPluginClass __parent__;
};

struct _DirectoryMenuPlugin
{
  XfcePanelPlugin __parent__;

  GtkWidget       *button;
  GtkWidget       *icon;

  GFile           *base_directory;
  gchar           *icon_name;
  gchar           *file_pattern;
  guint            hidden_files : 1;

  GSList          *patterns;

  /* temp item we store here when the
   * properties dialog is opened */
  GtkWidget       *dialog_icon;
};

enum
{
  PROP_0,
  PROP_BASE_DIRECTORY,
  PROP_ICON_NAME,
  PROP_FILE_PATTERN,
  PROP_HIDDEN_FILES
};



static void      directory_menu_plugin_get_property         (GObject             *object,
                                                             guint                prop_id,
                                                             GValue              *value,
                                                             GParamSpec          *pspec);
static void      directory_menu_plugin_set_property         (GObject             *object,
                                                             guint                prop_id,
                                                             const GValue        *value,
                                                             GParamSpec          *pspec);
static void      directory_menu_plugin_construct            (XfcePanelPlugin     *panel_plugin);
static void      directory_menu_plugin_free_file_patterns   (DirectoryMenuPlugin *plugin);
static void      directory_menu_plugin_free_data            (XfcePanelPlugin     *panel_plugin);
static void      directory_menu_plugin_configure_plugin     (XfcePanelPlugin     *panel_plugin);
static gboolean  directory_menu_plugin_remote_event         (XfcePanelPlugin     *panel_plugin,
                                                             const gchar         *name,
                                                             const GValue        *value);
static void      directory_menu_plugin_menu                 (GtkWidget           *button,
                                                             DirectoryMenuPlugin *plugin);



/* define the plugin */
XFCE_PANEL_DEFINE_PLUGIN (DirectoryMenuPlugin, directory_menu_plugin)



static GQuark menu_file = 0;



static void
directory_menu_plugin_class_init (DirectoryMenuPluginClass *klass)
{
  XfcePanelPluginClass *plugin_class;
  GObjectClass         *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = directory_menu_plugin_get_property;
  gobject_class->set_property = directory_menu_plugin_set_property;

  plugin_class = XFCE_PANEL_PLUGIN_CLASS (klass);
  plugin_class->construct = directory_menu_plugin_construct;
  plugin_class->free_data = directory_menu_plugin_free_data;
  plugin_class->configure_plugin = directory_menu_plugin_configure_plugin;
  plugin_class->remote_event = directory_menu_plugin_remote_event;

  g_object_class_install_property (gobject_class,
                                   PROP_BASE_DIRECTORY,
                                   g_param_spec_string ("base-directory",
                                                        NULL, NULL,
                                                        NULL,
                                                        EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_ICON_NAME,
                                   g_param_spec_string ("icon-name",
                                                        NULL, NULL,
                                                        NULL,
                                                        EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_FILE_PATTERN,
                                   g_param_spec_string ("file-pattern",
                                                        NULL, NULL,
                                                        "",
                                                        EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_HIDDEN_FILES,
                                   g_param_spec_boolean ("hidden-files",
                                                         NULL, NULL,
                                                         FALSE,
                                                         EXO_PARAM_READWRITE));

  menu_file = g_quark_from_static_string ("dir-menu-file");
}



static void
directory_menu_plugin_init (DirectoryMenuPlugin *plugin)
{
  plugin->button = xfce_panel_create_toggle_button ();
  xfce_panel_plugin_add_action_widget (XFCE_PANEL_PLUGIN (plugin), plugin->button);
  gtk_container_add (GTK_CONTAINER (plugin), plugin->button);
  gtk_button_set_relief (GTK_BUTTON (plugin->button), GTK_RELIEF_NONE);
  g_signal_connect (G_OBJECT (plugin->button), "toggled",
      G_CALLBACK (directory_menu_plugin_menu), plugin);

  plugin->icon = xfce_panel_image_new_from_source (DEFAULT_ICON_NAME);
  gtk_container_add (GTK_CONTAINER (plugin->button), plugin->icon);
  gtk_widget_show (plugin->icon);
}



static void
directory_menu_plugin_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  DirectoryMenuPlugin *plugin = XFCE_DIRECTORY_MENU_PLUGIN (object);
  gchar               *str;

  switch (prop_id)
    {
    case PROP_BASE_DIRECTORY:
      if (g_file_is_native (plugin->base_directory))
        str = g_file_get_path (plugin->base_directory);
      else
        str = g_file_get_uri (plugin->base_directory);
      g_value_take_string (value, str);
      break;

    case PROP_ICON_NAME:
      g_value_set_string (value, plugin->icon_name);
      break;

    case PROP_FILE_PATTERN:
      g_value_set_string (value, exo_str_is_empty (plugin->file_pattern) ?
          "" : plugin->file_pattern);
      break;

    case PROP_HIDDEN_FILES:
      g_value_set_boolean (value, plugin->hidden_files);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
directory_menu_plugin_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  DirectoryMenuPlugin  *plugin = XFCE_DIRECTORY_MENU_PLUGIN (object);
  gchar                *display_name;
  gchar               **array;
  guint                 i;
  GFile                *base_directory;
  const gchar          *path;

  switch (prop_id)
    {
    case PROP_BASE_DIRECTORY:
      path = g_value_get_string (value);
      if (exo_str_is_empty (path))
        path = g_get_home_dir ();

      if (plugin->base_directory != NULL)
        g_object_unref (G_OBJECT (plugin->base_directory));
      plugin->base_directory = g_file_new_for_commandline_arg (path);

      base_directory = g_file_new_for_commandline_arg (g_value_get_string (value));

      display_name = g_file_get_parse_name (plugin->base_directory);
      gtk_widget_set_tooltip_text (plugin->button, display_name);
      g_free (display_name);
      break;

    case PROP_ICON_NAME:
      g_free (plugin->icon_name);
      plugin->icon_name = g_value_dup_string (value);
      xfce_panel_image_set_from_source (XFCE_PANEL_IMAGE (plugin->icon),
          exo_str_is_empty (plugin->icon_name) ? DEFAULT_ICON_NAME : plugin->icon_name);
      break;

    case PROP_FILE_PATTERN:
      g_free (plugin->file_pattern);
      plugin->file_pattern = g_value_dup_string (value);

      directory_menu_plugin_free_file_patterns (plugin);

      array = g_strsplit (plugin->file_pattern, ";", -1);
      if (G_LIKELY (array != NULL))
        {
          for (i = 0; array[i] != NULL; i++)
            if (!exo_str_is_empty (array[i]))
                plugin->patterns = g_slist_prepend (plugin->patterns,
                    g_pattern_spec_new (array[i]));

          g_strfreev (array);
        }
      break;

    case PROP_HIDDEN_FILES:
      plugin->hidden_files = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
directory_menu_plugin_construct (XfcePanelPlugin *panel_plugin)
{
  DirectoryMenuPlugin *plugin = XFCE_DIRECTORY_MENU_PLUGIN (panel_plugin);
  const PanelProperty  properties[] =
  {
    { "base-directory", G_TYPE_STRING },
    { "icon-name", G_TYPE_STRING },
    { "file-pattern", G_TYPE_STRING },
    { "hidden-files", G_TYPE_BOOLEAN },
    { NULL }
  };

  xfce_panel_plugin_menu_show_configure (XFCE_PANEL_PLUGIN (plugin));

  /* bind all properties */
  panel_properties_bind (NULL, G_OBJECT (plugin),
                         xfce_panel_plugin_get_property_base (panel_plugin),
                         properties, FALSE);

  if (plugin->base_directory == NULL)
    g_object_set (G_OBJECT (plugin), "base-directory", g_get_home_dir (), NULL);

  gtk_widget_show (plugin->button);
}



static void
directory_menu_plugin_free_file_patterns (DirectoryMenuPlugin *plugin)
{
  GSList *li;

  panel_return_if_fail (XFCE_IS_DIRECTORY_MENU_PLUGIN (plugin));

  for (li = plugin->patterns; li != NULL; li = li->next)
    g_pattern_spec_free (li->data);

  g_slist_free (plugin->patterns);
  plugin->patterns = NULL;
}



static void
directory_menu_plugin_free_data (XfcePanelPlugin *panel_plugin)
{
  DirectoryMenuPlugin *plugin = XFCE_DIRECTORY_MENU_PLUGIN (panel_plugin);

  g_object_unref (G_OBJECT (plugin->base_directory));
  g_free (plugin->icon_name);
  g_free (plugin->file_pattern);

  directory_menu_plugin_free_file_patterns (plugin);
}



static void
directory_menu_plugin_configure_plugin_file_set (GtkFileChooserButton *button,
                                                 DirectoryMenuPlugin  *plugin)
{
  gchar *uri;

  panel_return_if_fail (GTK_IS_FILE_CHOOSER_BUTTON (button));
  panel_return_if_fail (XFCE_IS_DIRECTORY_MENU_PLUGIN (plugin));

  uri = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (button));
  g_object_set (G_OBJECT (plugin), "base-directory", uri, NULL);
  g_free (uri);
}



static void
directory_menu_plugin_configure_plugin_icon_chooser (GtkWidget           *button,
                                                     DirectoryMenuPlugin *plugin)
{
  GtkWidget *chooser;
  gchar     *icon;

  panel_return_if_fail (XFCE_IS_DIRECTORY_MENU_PLUGIN (plugin));
  panel_return_if_fail (XFCE_IS_PANEL_IMAGE (plugin->dialog_icon));

  chooser = exo_icon_chooser_dialog_new (_("Select An Icon"),
                                         GTK_WINDOW (gtk_widget_get_toplevel (button)),
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                         NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (chooser), GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_alternative_button_order (GTK_DIALOG (chooser),
                                           GTK_RESPONSE_ACCEPT,
                                           GTK_RESPONSE_CANCEL, -1);

  if (!exo_str_is_empty (plugin->icon_name))
  exo_icon_chooser_dialog_set_icon (EXO_ICON_CHOOSER_DIALOG (chooser), plugin->icon_name);

  if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT)
    {
      icon = exo_icon_chooser_dialog_get_icon (EXO_ICON_CHOOSER_DIALOG (chooser));
      g_object_set (G_OBJECT (plugin), "icon-name", icon, NULL);
      xfce_panel_image_set_from_source (XFCE_PANEL_IMAGE (plugin->dialog_icon), icon);
      g_free (icon);
    }

  gtk_widget_destroy (chooser);
}



static void
directory_menu_plugin_configure_plugin (XfcePanelPlugin *panel_plugin)
{
  DirectoryMenuPlugin *plugin = XFCE_DIRECTORY_MENU_PLUGIN (panel_plugin);
  GtkBuilder          *builder;
  GObject             *dialog, *object;
  const gchar         *icon_name = plugin->icon_name;

  if (exo_str_is_empty (icon_name))
    icon_name = DEFAULT_ICON_NAME;

  /* setup the dialog */
  PANEL_BUILDER_LINK_4UI
  builder = panel_builder_new (panel_plugin, directorymenu_dialog_ui,
                               directorymenu_dialog_ui_length, &dialog);
  if (G_UNLIKELY (builder == NULL))
    return;

  object = gtk_builder_get_object (builder, "base-directory");
  panel_return_if_fail (GTK_IS_FILE_CHOOSER_BUTTON (object));
  if (!gtk_file_chooser_set_current_folder_file (GTK_FILE_CHOOSER (object),
                                                 plugin->base_directory, NULL))
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (object), g_get_home_dir ());
  g_signal_connect (G_OBJECT (object), "current-folder-changed",
     G_CALLBACK (directory_menu_plugin_configure_plugin_file_set), plugin);

  object = gtk_builder_get_object (builder, "icon-button");
  panel_return_if_fail (GTK_IS_BUTTON (object));
  g_signal_connect (G_OBJECT (object), "clicked",
     G_CALLBACK (directory_menu_plugin_configure_plugin_icon_chooser), plugin);

  plugin->dialog_icon = xfce_panel_image_new_from_source (icon_name);
  g_object_set (G_OBJECT (plugin->dialog_icon), "size", 48, NULL);
  gtk_container_add (GTK_CONTAINER (object), plugin->dialog_icon);
  g_object_add_weak_pointer (G_OBJECT (plugin->dialog_icon), (gpointer) &plugin->dialog_icon);
  gtk_widget_show (plugin->dialog_icon);

  object = gtk_builder_get_object (builder, "file-pattern");
  panel_return_if_fail (GTK_IS_ENTRY (object));
  exo_mutual_binding_new (G_OBJECT (plugin), "file-pattern",
                          G_OBJECT (object), "text");

  object = gtk_builder_get_object (builder, "hidden-files");
  panel_return_if_fail (GTK_IS_CHECK_BUTTON (object));
  exo_mutual_binding_new (G_OBJECT (plugin), "hidden-files",
                          G_OBJECT (object), "active");

  gtk_widget_show (GTK_WIDGET (dialog));
}



static gboolean
directory_menu_plugin_remote_event (XfcePanelPlugin *panel_plugin,
                                    const gchar     *name,
                                    const GValue    *value)
{
  DirectoryMenuPlugin *plugin = XFCE_DIRECTORY_MENU_PLUGIN (panel_plugin);

  panel_return_val_if_fail (value == NULL || G_IS_VALUE (value), FALSE);

  if (strcmp (name, "popup") == 0
      && GTK_WIDGET_VISIBLE (panel_plugin)
      && !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (plugin->button)))
    {
      /* show the menu */
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), TRUE);

      /* don't popup another menu */
      return TRUE;
    }

  return FALSE;
}



static void
directory_menu_plugin_selection_done (GtkWidget *menu,
                                      GtkWidget *button)
{
  panel_return_if_fail (GTK_IS_TOGGLE_BUTTON (button));
  panel_return_if_fail (GTK_IS_MENU (menu));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);

  /* delay destruction so we can handle the activate event first */
  exo_gtk_object_destroy_later (GTK_OBJECT (menu));
}



static gint
directory_menu_plugin_menu_sort (gconstpointer a,
                                 gconstpointer b)
{
  GFileType type_a = g_file_info_get_file_type (G_FILE_INFO (a));
  GFileType type_b = g_file_info_get_file_type (G_FILE_INFO (b));
  gboolean  hidden_a, hidden_b;

  if (type_a != type_b)
    {
      /* sort directories before files */
      if (type_a == G_FILE_TYPE_DIRECTORY)
        return -1;
      else if (type_b == G_FILE_TYPE_DIRECTORY)
        return 1;
    }

  hidden_a = g_file_info_get_is_hidden (G_FILE_INFO (a));
  hidden_b = g_file_info_get_is_hidden (G_FILE_INFO (b));

  /* sort hidden files above 'normal' files */
  if (hidden_a != hidden_b)
    return hidden_a ? -1 : 1;

  return g_utf8_collate (g_file_info_get_display_name (G_FILE_INFO (a)),
                         g_file_info_get_display_name (G_FILE_INFO (b)));
}



#ifdef HAVE_GIO_UNIX
static void
directory_menu_plugin_menu_launch_desktop_file (GtkWidget *mi,
                                                GAppInfo  *info)
{
  GdkAppLaunchContext *context;
  GIcon               *icon;
  GError              *error = NULL;

  panel_return_if_fail (G_IS_APP_INFO (info));
  panel_return_if_fail (GTK_IS_WIDGET (mi));

  context = gdk_app_launch_context_new ();
  gdk_app_launch_context_set_screen (context, gtk_widget_get_screen (mi));
  gdk_app_launch_context_set_timestamp (context, gtk_get_current_event_time ());
  icon = g_app_info_get_icon (info);
  if (G_LIKELY (icon != NULL))
    gdk_app_launch_context_set_icon (context, icon);

  if (!g_app_info_launch (info, NULL, G_APP_LAUNCH_CONTEXT (context), &error))
    {
      xfce_dialog_show_error (NULL, error, _("Failed to launch application \"%s\""),
                              g_app_info_get_executable (info));
      g_error_free (error);
    }

  g_object_unref (G_OBJECT (context));
}
#endif



static void
directory_menu_plugin_menu_launch (GtkWidget *mi,
                                   GFile     *file)
{
  GAppInfo            *appinfo;
  GError              *error = NULL;
  gchar               *display_name;
  GList                fake_list = { NULL, };
  GdkAppLaunchContext *context;
  GFileInfo           *info;
  const gchar         *message;
  gboolean             result;

  panel_return_if_fail (G_IS_FILE (file));
  panel_return_if_fail (GTK_IS_WIDGET (mi));

  info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                            G_FILE_QUERY_INFO_NONE, NULL, &error);
  if (G_UNLIKELY (info == NULL))
    {
      message = _("Failed to query content type for \"%s\"");
      goto err;
    }

  appinfo = g_app_info_get_default_for_type (g_file_info_get_content_type (info),
                                             !g_file_is_native (file));
  g_object_unref (G_OBJECT (info));
  if (G_LIKELY (appinfo == NULL))
    {
      message = _("No default application found for \"%s\"");
      goto err;
    }

  fake_list.data = file;

  context = gdk_app_launch_context_new ();
  gdk_app_launch_context_set_screen (context, gtk_widget_get_screen (mi));
  gdk_app_launch_context_set_timestamp (context, gtk_get_current_event_time ());

  result = g_app_info_launch (appinfo, &fake_list, G_APP_LAUNCH_CONTEXT (context), &error);
  g_object_unref (G_OBJECT (context));
  g_object_unref (G_OBJECT (appinfo));
  if (G_UNLIKELY (!result))
    {
      message = _("Failed to launch default application for \"%s\"");
      goto err;
    }

  return;

err:
  display_name = g_file_get_parse_name (file);
  xfce_dialog_show_error (NULL, error, message, display_name);
  g_free (display_name);
  g_error_free (error);
}



static void
directory_menu_plugin_menu_open (GtkWidget   *mi,
                                 GFile       *dir,
                                 const gchar *category)
{
  GError *error = NULL;
  gchar  *path;

  path = g_file_get_path (dir);
  if (!exo_execute_preferred_application_on_screen (category, NULL, path, NULL,
                                                    gtk_widget_get_screen (mi),
                                                    &error))
    {
      xfce_dialog_show_error (NULL, error,
          _("Failed to open preferred application category \"%s\""), category);
      g_error_free (error);
    }
  g_free (path);
}



static void
directory_menu_plugin_menu_open_terminal (GtkWidget *mi,
                                          GFile     *dir)
{
  panel_return_if_fail (GTK_IS_WIDGET (mi));
  panel_return_if_fail (G_IS_FILE (dir));

  directory_menu_plugin_menu_open (mi, dir, "TerminalEmulator");
}



static void
directory_menu_plugin_menu_open_folder (GtkWidget *mi,
                                        GFile     *dir)
{
  panel_return_if_fail (GTK_IS_WIDGET (mi));
  panel_return_if_fail (G_IS_FILE (dir));

  directory_menu_plugin_menu_open (mi, dir, "FileManager");
}



static void
directory_menu_plugin_menu_unload (GtkWidget *menu)
{
  /* delay destruction so we can handle the activate event first */
  gtk_container_foreach (GTK_CONTAINER (menu),
     (GtkCallback) exo_gtk_object_destroy_later, NULL);
}



static void
directory_menu_plugin_menu_load (GtkWidget           *menu,
                                 DirectoryMenuPlugin *plugin)
{
  GFileEnumerator *iter;
  GFileInfo       *info;
  GtkWidget       *mi;
  const gchar     *display_name;
  GSList          *li, *infos = NULL;
  GIcon           *icon;
  GtkWidget       *image;
  GtkWidget       *submenu;
  GFile           *file;
  GFile           *dir;
  gboolean         visible;
  GFileType        file_type;
#ifdef HAVE_GIO_UNIX
  GDesktopAppInfo *desktopinfo;
  gchar           *path;
  const gchar     *description;
#endif

  panel_return_if_fail (XFCE_IS_DIRECTORY_MENU_PLUGIN (plugin));
  panel_return_if_fail (GTK_IS_MENU (menu));

  dir = g_object_get_qdata (G_OBJECT (menu), menu_file);
  panel_return_if_fail (G_IS_FILE (dir));
  if (G_UNLIKELY (dir == NULL))
    return;

  mi = gtk_image_menu_item_new_with_label (_("Open Folder"));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  g_signal_connect_data (G_OBJECT (mi), "activate",
      G_CALLBACK (directory_menu_plugin_menu_open_folder),
      g_object_ref (dir), (GClosureNotify) g_object_unref, 0);
  gtk_widget_show (mi);

  image = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), image);
  gtk_widget_show (image);

  mi = gtk_image_menu_item_new_with_label (_("Open in Terminal"));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  g_signal_connect_data (G_OBJECT (mi), "activate",
      G_CALLBACK (directory_menu_plugin_menu_open_terminal),
      g_object_ref (dir), (GClosureNotify) g_object_unref, 0);
  gtk_widget_show (mi);

  image = gtk_image_new_from_icon_name ("terminal", GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), image);
  gtk_widget_show (image);

  iter = g_file_enumerate_children (dir, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME
                                    "," G_FILE_ATTRIBUTE_STANDARD_NAME
                                    "," G_FILE_ATTRIBUTE_STANDARD_TYPE
                                    "," G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN
                                    "," G_FILE_ATTRIBUTE_STANDARD_ICON,
                                    G_FILE_QUERY_INFO_NONE, NULL, NULL);
  if (G_LIKELY (iter != NULL))
    {
      for (;;)
        {
          info = g_file_enumerator_next_file (iter, NULL, NULL);
          if (G_UNLIKELY (info == NULL))
            break;

          /* skip hidden files if disabled by the user */
          if (!plugin->hidden_files
              && g_file_info_get_is_hidden (info))
            {
              g_object_unref (G_OBJECT (info));
              continue;
            }

          /* if the file is not a directory, check the file patterns */
          if (g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY)
            {
              if (plugin->patterns == NULL)
                {
                  g_object_unref (G_OBJECT (info));
                  continue;
                }

              visible = FALSE;
              display_name = g_file_info_get_display_name (info);
              if (G_LIKELY (display_name != NULL))
                for (li = plugin->patterns; !visible && li != NULL; li = li->next)
                   if (g_pattern_match_string (li->data, display_name))
                     visible = TRUE;

              if (!visible)
                {
                  g_object_unref (G_OBJECT (info));
                  continue;
                }
            }

          infos = g_slist_insert_sorted (infos, info, directory_menu_plugin_menu_sort);
        }

      g_object_unref (G_OBJECT (iter));

      if (G_LIKELY (infos != NULL))
        {
          mi = gtk_separator_menu_item_new ();
          gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
          gtk_widget_show (mi);
        }

      for (li = infos; li != NULL; li = li->next)
        {
          info = G_FILE_INFO (li->data);
          file_type = g_file_info_get_file_type (info);

          display_name = g_file_info_get_display_name (info);
          if (G_UNLIKELY (display_name == NULL))
            {
              g_object_unref (G_OBJECT (info));
              continue;
            }

          file = g_file_get_child (dir, g_file_info_get_name (info));
          icon = NULL;

#ifdef HAVE_GIO_UNIX
          /* for native desktop files we make an exception and try
           * to load them like a normal menu */
          desktopinfo = NULL;
          if (G_UNLIKELY (file_type != G_FILE_TYPE_DIRECTORY
              && g_file_is_native (file)
              && g_str_has_suffix (display_name, ".desktop")))
            {
              path = g_file_get_path (file);
              desktopinfo = g_desktop_app_info_new_from_filename (path);
              g_free (path);

              if (G_LIKELY (desktopinfo != NULL))
                {
                  display_name = g_app_info_get_name (G_APP_INFO (desktopinfo));
                  icon = g_app_info_get_icon (G_APP_INFO (desktopinfo));
                }
            }
#endif

          mi = gtk_image_menu_item_new_with_label (display_name);
          gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
          gtk_widget_show (mi);

          if (G_LIKELY (icon == NULL))
            icon = g_file_info_get_icon (info);
          if (G_LIKELY (icon != NULL))
            {
              image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_MENU);
              gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), image);
              gtk_widget_show (image);
            }

          /* set a submenu for directories */
          if (G_LIKELY (file_type == G_FILE_TYPE_DIRECTORY))
            {
              submenu = gtk_menu_new ();
              gtk_menu_item_set_submenu (GTK_MENU_ITEM (mi), submenu);
              g_object_set_qdata_full (G_OBJECT (submenu), menu_file, file, g_object_unref);

              g_signal_connect (G_OBJECT (submenu), "show",
                  G_CALLBACK (directory_menu_plugin_menu_load), plugin);
              g_signal_connect_after (G_OBJECT (submenu), "hide",
                  G_CALLBACK (directory_menu_plugin_menu_unload), NULL);
            }
#ifdef HAVE_GIO_UNIX
          else if (G_UNLIKELY (desktopinfo != NULL))
            {
              description = g_app_info_get_description (G_APP_INFO (desktopinfo));
              if (!exo_str_is_empty (description))
                gtk_widget_set_tooltip_text (mi, description);

              g_signal_connect_data (G_OBJECT (mi), "activate",
                  G_CALLBACK (directory_menu_plugin_menu_launch_desktop_file),
                  desktopinfo, (GClosureNotify) g_object_unref, 0);

              g_object_unref (G_OBJECT (file));
            }
#endif
          else
            {
              g_signal_connect_data (G_OBJECT (mi), "activate",
                  G_CALLBACK (directory_menu_plugin_menu_launch), file,
                  (GClosureNotify) g_object_unref, 0);
            }

          g_object_unref (G_OBJECT (info));
        }

      g_slist_free (infos);
    }
}



static void
directory_menu_plugin_menu (GtkWidget           *button,
                            DirectoryMenuPlugin *plugin)
{
  GtkWidget *menu;

  panel_return_if_fail (XFCE_IS_DIRECTORY_MENU_PLUGIN (plugin));
  panel_return_if_fail (plugin->button == button);

  if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    return;

  menu = gtk_menu_new ();
  g_signal_connect (G_OBJECT (menu), "deactivate",
      G_CALLBACK (directory_menu_plugin_selection_done), button);

  g_object_set_qdata_full (G_OBJECT (menu), menu_file,
                           g_object_ref (plugin->base_directory),
                           g_object_unref);
  directory_menu_plugin_menu_load (menu, plugin);

  gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
                  xfce_panel_plugin_position_menu, plugin,
                  1, gtk_get_current_event_time ());
}
