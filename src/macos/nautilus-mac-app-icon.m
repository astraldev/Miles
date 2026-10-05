/*
 * Copyright (C) 2026 Ekure Edem
 *
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

#include <config.h>
#include "nautilus-mac-app-icon.h"

#import <AppKit/AppKit.h>

#define DEFAULT_SIZE 256

struct _NautilusMacAppIcon
{
    GObject parent_instance;

    char *path;
};

static void nautilus_mac_app_icon_icon_iface_init (GIconIface *iface);
static void nautilus_mac_app_icon_loadable_icon_iface_init (GLoadableIconIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (NautilusMacAppIcon, nautilus_mac_app_icon, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_ICON,
                                                      nautilus_mac_app_icon_icon_iface_init)
                               G_IMPLEMENT_INTERFACE (G_TYPE_LOADABLE_ICON,
                                                      nautilus_mac_app_icon_loadable_icon_iface_init))

static GBytes *
load_png (const char *path,
          int         size)
{
    GBytes *bytes = NULL;

    @autoreleasepool
    {
        NSString *file = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:path
                                                                                     length:strlen (path)];
        /* The icon Finder shows. Many apps keep it where only MacOS can read it. */
        NSImage *image = [[NSWorkspace sharedWorkspace] iconForFile:file];
        NSBitmapImageRep *bitmap = [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                            pixelsWide:size
                                                                            pixelsHigh:size
                                                                         bitsPerSample:8
                                                                       samplesPerPixel:4
                                                                              hasAlpha:YES
                                                                              isPlanar:NO
                                                                        colorSpaceName:NSDeviceRGBColorSpace
                                                                           bytesPerRow:0
                                                                          bitsPerPixel:0] autorelease];
        NSData *png;

        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap]];
        [image drawInRect:NSMakeRect (0, 0, size, size)
                 fromRect:NSZeroRect
                operation:NSCompositingOperationCopy
                 fraction:1.0];
        [NSGraphicsContext restoreGraphicsState];

        png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (png != nil)
        {
            bytes = g_bytes_new (png.bytes, png.length);
        }
    }

    return bytes;
}

static guint
nautilus_mac_app_icon_hash (GIcon *icon)
{
    return g_str_hash (NAUTILUS_MAC_APP_ICON (icon)->path);
}

static gboolean
nautilus_mac_app_icon_equal (GIcon *icon1,
                             GIcon *icon2)
{
    return g_str_equal (NAUTILUS_MAC_APP_ICON (icon1)->path,
                        NAUTILUS_MAC_APP_ICON (icon2)->path);
}

static GInputStream *
nautilus_mac_app_icon_load (GLoadableIcon  *icon,
                            int             size,
                            char          **type,
                            GCancellable   *cancellable,
                            GError        **error)
{
    NautilusMacAppIcon *self = NAUTILUS_MAC_APP_ICON (icon);
    g_autoptr (GBytes) bytes = NULL;

    if (g_cancellable_set_error_if_cancelled (cancellable, error))
    {
        return NULL;
    }

    bytes = load_png (self->path, size > 0 ? size : DEFAULT_SIZE);
    if (bytes == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "No icon found for the app %s", self->path);

        return NULL;
    }

    if (type != NULL)
    {
        *type = g_strdup ("image/png");
    }

    return g_memory_input_stream_new_from_bytes (bytes);
}

static void
nautilus_mac_app_icon_finalize (GObject *object)
{
    NautilusMacAppIcon *self = NAUTILUS_MAC_APP_ICON (object);

    g_free (self->path);

    G_OBJECT_CLASS (nautilus_mac_app_icon_parent_class)->finalize (object);
}

static void
nautilus_mac_app_icon_icon_iface_init (GIconIface *iface)
{
    iface->hash = nautilus_mac_app_icon_hash;
    iface->equal = nautilus_mac_app_icon_equal;
}

static void
nautilus_mac_app_icon_loadable_icon_iface_init (GLoadableIconIface *iface)
{
    iface->load = nautilus_mac_app_icon_load;
}

static void
nautilus_mac_app_icon_class_init (NautilusMacAppIconClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = nautilus_mac_app_icon_finalize;
}

static void
nautilus_mac_app_icon_init (NautilusMacAppIcon *self)
{
}

GIcon *
nautilus_mac_app_icon_new (GFile *location)
{
    g_autofree char *path = NULL;
    NautilusMacAppIcon *self;

    g_return_val_if_fail (G_IS_FILE (location), NULL);

    path = g_file_get_path (location);
    if (path == NULL || !g_str_has_suffix (path, ".app"))
    {
        return NULL;
    }

    self = g_object_new (NAUTILUS_TYPE_MAC_APP_ICON, NULL);
    self->path = g_steal_pointer (&path);

    return G_ICON (self);
}
