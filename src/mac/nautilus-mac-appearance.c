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

#include <CoreFoundation/CoreFoundation.h>

/* macOS accent colour ("AppleAccentColor") to Yaru variant. install-yaru.sh must list each one. */
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

/* Multicolour, the default, has no value stored. macOS then uses blue. */
#define MULTICOLOUR_ICON_THEME "Yaru-blue"

/* Returns: the Yaru icon theme that matches the accent colour of macOS. */
const char *
nautilus_mac_get_accent_icon_theme (void)
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

static NautilusMacAppearanceFunc accent_changed_func = NULL;

static void
apply_accent (gpointer user_data)
{
    /* CFPreferences keeps what it has read. Make it read the setting again. */
    CFPreferencesSynchronize (kCFPreferencesAnyApplication,
                              kCFPreferencesCurrentUser,
                              kCFPreferencesAnyHost);
    CFPreferencesAppSynchronize (kCFPreferencesCurrentApplication);

    g_debug ("Accent colour of macOS: %s", nautilus_mac_get_accent_icon_theme ());
    accent_changed_func ();
}

static void
accent_changed_cb (CFNotificationCenterRef  center,
                   void                    *observer,
                   CFNotificationName       name,
                   const void              *object,
                   CFDictionaryRef          user_info)
{
    g_debug ("macOS says its colours changed");

    /* The notification can come before the new setting can be read: look again later. */
    g_idle_add_once (apply_accent, NULL);
    g_timeout_add_once (300, apply_accent, NULL);
    g_timeout_add_once (1500, apply_accent, NULL);
    g_main_context_wakeup (NULL);
}

/* Calls @func in the main loop when the accent colour changes. Call once, from the main thread. */
void
nautilus_mac_watch_accent_colour (NautilusMacAppearanceFunc func)
{
    g_return_if_fail (func != NULL);
    g_return_if_fail (accent_changed_func == NULL);

    accent_changed_func = func;

    /* System Settings tells every app with these notifications. */
    CFNotificationCenterAddObserver (CFNotificationCenterGetDistributedCenter (),
                                     &accent_changed_func,
                                     accent_changed_cb,
                                     CFSTR ("AppleColorPreferencesChangedNotification"),
                                     NULL,
                                     CFNotificationSuspensionBehaviorDeliverImmediately);
    CFNotificationCenterAddObserver (CFNotificationCenterGetDistributedCenter (),
                                     &accent_changed_func,
                                     accent_changed_cb,
                                     CFSTR ("AppleAquaColorVariantChanged"),
                                     NULL,
                                     CFNotificationSuspensionBehaviorDeliverImmediately);
}
