/*
 * Nautilus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nautilus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; see the file COPYING.  If not,
 * see <http://www.gnu.org/licenses/>.
 *
 */

#define G_LOG_DOMAIN "nautilus-mac"

#include <config.h>
#include "nautilus-mac-appearance.h"

#include "nautilus-directory-notify.h"
#include "nautilus-directory-private.h"
#include "nautilus-file.h"
#include "nautilus-icon-info.h"

#include <adwaita.h>
#include <CoreFoundation/CoreFoundation.h>

/* Where macos/scripts/install-yaru.sh puts the icon theme bundled with the app. */
#define BUNDLED_ICONS_DIR NAUTILUS_DATADIR "/icons"

#define FONT_SIZE 13
#define APP_ICON_STAGGER_MS 20

/* "AppleAccentColor" to Yaru variant. install-yaru.sh must list each one. */
static const struct
{
    int accent;
    const char *icon_theme;
} accent_icon_themes[] =
{
    { -1, "Yaru-bark" },    /* Graphite */
    { 0, "Yaru-red" },      /* Red */
    { 1, "Yaru" },          /* Orange, which is Yaru's own colour */
    { 2, "Yaru-yellow" },   /* Yellow */
    { 3, "Yaru-viridian" }, /* Green */
    { 4, "Yaru-blue" },     /* Blue */
    { 5, "Yaru-purple" },   /* Purple */
    { 6, "Yaru-magenta" },  /* Pink */
};

/* Multicolour, the default, has no value stored. MacOS then uses blue. */
#define MULTICOLOUR_ICON_THEME "Yaru-blue"

static const char *
get_accent_icon_theme (void)
{
    const char *icon_theme = MULTICOLOUR_ICON_THEME;
    CFPropertyListRef value;
    int accent;

    /* Looks in the app's own preferences, then in the global ones. */
    value = CFPreferencesCopyAppValue (CFSTR ("AppleAccentColor"),
                                       kCFPreferencesCurrentApplication);
    if (value == NULL)
    {
        return icon_theme;
    }

    if (CFGetTypeID (value) == CFNumberGetTypeID () &&
        CFNumberGetValue (value, kCFNumberIntType, &accent))
    {
        for (guint i = 0; i < G_N_ELEMENTS (accent_icon_themes); i++)
        {
            if (accent_icon_themes[i].accent == accent)
            {
                icon_theme = accent_icon_themes[i].icon_theme;
                break;
            }
        }
    }

    CFRelease (value);

    return icon_theme;
}

static void
update_icon_theme (void)
{
    gboolean dark = adw_style_manager_get_dark (adw_style_manager_get_default ());
    g_autofree char *name = g_strconcat (get_accent_icon_theme (), dark ? "-dark" : "", NULL);
    g_autofree char *index = g_build_filename (BUNDLED_ICONS_DIR, name, "index.theme", NULL);
    g_autofree char *current_name = NULL;

    g_object_get (gtk_settings_get_default (), "gtk-icon-theme-name", &current_name, NULL);
    if (g_strcmp0 (name, current_name) == 0)
    {
        return;
    }

    if (!g_file_test (index, G_FILE_TEST_EXISTS))
    {
        /* The theme is installed with the app: without it this install is broken. */
        g_error ("The icon theme %s is missing from %s", name, BUNDLED_ICONS_DIR);
    }

    g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", name, NULL);
}

static void
apply_accent (gpointer user_data)
{
    /* CFPreferences keeps what it has read. Make it read the setting again. */
    CFPreferencesSynchronize (kCFPreferencesAnyApplication,
                              kCFPreferencesCurrentUser,
                              kCFPreferencesAnyHost);
    CFPreferencesAppSynchronize (kCFPreferencesCurrentApplication);

    g_debug ("Accent colour of MacOS: %s", get_accent_icon_theme ());
    update_icon_theme ();
}

static void
accent_changed_cb (CFNotificationCenterRef  center,
                   void                    *observer,
                   CFNotificationName       name,
                   const void              *object,
                   CFDictionaryRef          user_info)
{
    g_debug ("MacOS says its colours changed");

    /* The notification can come before the new setting can be read: look again later. */
    g_idle_add_once (apply_accent, NULL);
    g_timeout_add_once (300, apply_accent, NULL);
    g_timeout_add_once (1500, apply_accent, NULL);
    g_main_context_wakeup (NULL);
}

static void
watch_icon_theme (void)
{
    const CFStringRef notifications[] =
    {
        CFSTR ("AppleColorPreferencesChangedNotification"),
        CFSTR ("AppleAquaColorVariantChanged"),
    };
    GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
    g_auto (GStrv) search_path = gtk_icon_theme_get_search_path (icon_theme);
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_auto (GStrv) bundled_first = NULL;

    /* Part of the app: searched first, so a theme of the same name elsewhere cannot replace it. */
    g_strv_builder_add (builder, BUNDLED_ICONS_DIR);
    if (search_path != NULL)
    {
        g_strv_builder_addv (builder, (const char **) search_path);
    }
    bundled_first = g_strv_builder_end (builder);
    gtk_icon_theme_set_search_path (icon_theme, (const char * const *) bundled_first);

    update_icon_theme ();

    g_signal_connect (adw_style_manager_get_default (), "notify::dark",
                      G_CALLBACK (update_icon_theme), NULL);
    for (guint i = 0; i < G_N_ELEMENTS (notifications); i++)
    {
        CFNotificationCenterAddObserver (CFNotificationCenterGetDistributedCenter (),
                                         accent_icon_themes,
                                         accent_changed_cb,
                                         notifications[i],
                                         NULL,
                                         CFNotificationSuspensionBehaviorDeliverImmediately);
    }
}

static char *
get_icon_style (void)
{
    g_autoptr (GPtrArray) settings = g_ptr_array_new_with_free_func (g_free);
    CFArrayRef keys;

    CFPreferencesSynchronize (kCFPreferencesAnyApplication,
                              kCFPreferencesCurrentUser,
                              kCFPreferencesAnyHost);
    keys = CFPreferencesCopyKeyList (kCFPreferencesAnyApplication,
                                     kCFPreferencesCurrentUser,
                                     kCFPreferencesAnyHost);

    for (CFIndex i = 0; keys != NULL && i < CFArrayGetCount (keys); i++)
    {
        CFStringRef key = CFArrayGetValueAtIndex (keys, i);
        CFPropertyListRef value;
        CFStringRef setting;
        CFIndex size;
        char *text;

        if (!CFStringHasPrefix (key, CFSTR ("AppleIconAppearance")))
        {
            continue;
        }

        value = CFPreferencesCopyValue (key,
                                        kCFPreferencesAnyApplication,
                                        kCFPreferencesCurrentUser,
                                        kCFPreferencesAnyHost);
        if (value == NULL)
        {
            continue;
        }

        setting = CFStringCreateWithFormat (NULL, NULL, CFSTR ("%@=%@"), key, value);
        size = CFStringGetMaximumSizeForEncoding (CFStringGetLength (setting),
                                                  kCFStringEncodingUTF8) + 1;
        text = g_malloc (size);
        if (CFStringGetCString (setting, text, size, kCFStringEncodingUTF8))
        {
            g_ptr_array_add (settings, text);
        }
        else
        {
            g_free (text);
        }

        CFRelease (setting);
        CFRelease (value);
    }

    g_clear_pointer (&keys, CFRelease);

    g_ptr_array_sort_values (settings, (GCompareFunc) strcmp);
    g_ptr_array_add (settings, NULL);

    return g_strjoinv (" ", (char **) settings->pdata);
}

static gboolean
icon_style_changed (void)
{
    static char *last_style = NULL;
    g_autofree char *style = get_icon_style ();
    gboolean changed = last_style != NULL && !g_str_equal (style, last_style);

    if (changed)
    {
        g_debug ("Icon style of MacOS: %s", style);
    }

    g_set_str (&last_style, style);

    return changed;
}

static guint app_icons_source_id = 0;

static gboolean
refresh_next_app_icon (gpointer user_data)
{
    GQueue *apps = user_data;
    g_autoptr (NautilusFile) app = g_queue_pop_head (apps);

    if (app == NULL)
    {
        app_icons_source_id = 0;

        return G_SOURCE_REMOVE;
    }

    nautilus_file_changed (app);

    return G_SOURCE_CONTINUE;
}

static void
free_app_queue (gpointer user_data)
{
    g_queue_free_full (user_data, g_object_unref);
}

static int
compare_apps (gconstpointer a,
              gconstpointer b)
{
    return nautilus_file_compare_for_sort (NAUTILUS_FILE (a), NAUTILUS_FILE (b),
                                           NAUTILUS_FILE_SORT_BY_DISPLAY_NAME, FALSE, FALSE);
}

/* One after the other, in the order they are shown: all at once is a flash. */
static void
refresh_app_icons (void)
{
    GQueue *apps = g_queue_new ();
    GList *sorted_apps = g_list_sort (nautilus_directory_get_mac_apps (), compare_apps);

    for (GList *l = sorted_apps; l != NULL; l = l->next)
    {
        g_queue_push_tail (apps, l->data);
    }
    g_list_free (sorted_apps);

    g_clear_handle_id (&app_icons_source_id, g_source_remove);
    nautilus_icon_info_clear_caches ();
    app_icons_source_id = g_timeout_add_full (G_PRIORITY_DEFAULT, APP_ICON_STAGGER_MS,
                                              refresh_next_app_icon, apps, free_app_queue);
}

static void
on_window_active_changed (GtkWindow *window)
{
    /* MacOS does not announce a change of the icon style. */
    if (gtk_window_is_active (window) && icon_style_changed ())
    {
        refresh_app_icons ();
    }
}

static void
on_window_added (GtkApplication *application,
                 GtkWindow      *window)
{
    g_signal_connect (window, "notify::is-active", G_CALLBACK (on_window_active_changed), NULL);
}

static void
set_font_size (void)
{
    GtkSettings *settings = gtk_settings_get_default ();
    g_autofree char *font_name = NULL;
    g_autofree char *sized_font_name = NULL;
    PangoFontDescription *description;

    g_object_get (settings, "gtk-font-name", &font_name, NULL);

    description = pango_font_description_from_string (font_name);
    pango_font_description_set_size (description, FONT_SIZE * PANGO_SCALE);
    sized_font_name = pango_font_description_to_string (description);
    pango_font_description_free (description);

    g_object_set (settings, "gtk-font-name", sized_font_name, NULL);
}

void
nautilus_mac_appearance_init (GtkApplication *application)
{
    watch_icon_theme ();
    set_font_size ();

    icon_style_changed ();
    g_signal_connect (application, "window-added", G_CALLBACK (on_window_added), NULL);
}
