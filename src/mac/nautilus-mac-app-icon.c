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

#include <config.h>
#include "nautilus-mac-app-icon.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

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

static CFURLRef
copy_icon_url (const char *path)
{
    CFURLRef app_url;
    CFDictionaryRef info;
    CFTypeRef icon_name;
    char name[PATH_MAX];
    g_autofree char *icon_path = NULL;

    app_url = CFURLCreateFromFileSystemRepresentation (NULL, (const UInt8 *) path,
                                                       strlen (path), true);
    if (app_url == NULL)
    {
        return NULL;
    }

    /* A CFBundle would cache the Info.plist. */
    info = CFBundleCopyInfoDictionaryInDirectory (app_url);
    CFRelease (app_url);
    if (info == NULL)
    {
        return NULL;
    }

    icon_name = CFDictionaryGetValue (info, CFSTR ("CFBundleIconFile"));
    if (icon_name != NULL && CFGetTypeID (icon_name) == CFStringGetTypeID () &&
        CFStringGetFileSystemRepresentation (icon_name, name, sizeof (name)))
    {
        icon_path = g_build_filename (path, "Contents", "Resources", name, NULL);

        /* The name may lack its extension. */
        if (!g_file_test (icon_path, G_FILE_TEST_IS_REGULAR))
        {
            g_autofree char *without_extension = g_steal_pointer (&icon_path);

            icon_path = g_strconcat (without_extension, ".icns", NULL);
        }
    }

    CFRelease (info);

    if (icon_path == NULL)
    {
        return NULL;
    }

    return CFURLCreateFromFileSystemRepresentation (NULL, (const UInt8 *) icon_path,
                                                    strlen (icon_path), false);
}

static CFDataRef
copy_custom_icon_data (const char *path)
{
    g_autofree char *fork_path = g_build_filename (path, "Icon\r", "..namedfork", "rsrc", NULL);
    g_autofree char *fork = NULL;
    gsize fork_length;
    guint32 offset, icon_length;

    if (!g_file_get_contents (fork_path, &fork, &fork_length, NULL) || fork_length < 4)
    {
        return NULL;
    }

    /* Offset of the first resource: a length, then the .icns data. */
    memcpy (&offset, fork, 4);
    offset = GUINT32_FROM_BE (offset);
    if ((guint64) offset + 8 > fork_length)
    {
        return NULL;
    }

    memcpy (&icon_length, fork + offset, 4);
    icon_length = GUINT32_FROM_BE (icon_length);
    if (icon_length < 8 ||
        (guint64) offset + 4 + icon_length > fork_length ||
        memcmp (fork + offset + 4, "icns", 4) != 0)
    {
        return NULL;
    }

    return CFDataCreate (NULL, (const UInt8 *) fork + offset + 4, icon_length);
}

static long
get_image_width (CGImageSourceRef source,
                 size_t           index)
{
    CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex (source, index, NULL);
    CFTypeRef number;
    long width = 0;

    if (properties == NULL)
    {
        return 0;
    }

    number = CFDictionaryGetValue (properties, kCGImagePropertyPixelWidth);
    if (number != NULL && CFGetTypeID (number) == CFNumberGetTypeID ())
    {
        CFNumberGetValue (number, kCFNumberLongType, &width);
    }

    CFRelease (properties);

    return width;
}

static CGImageRef
create_image_for_size (CGImageSourceRef source,
                       int              size)
{
    size_t n_images = CGImageSourceGetCount (source);
    size_t best = 0;
    long best_width = 0;
    CFNumberRef max_size;
    CFDictionaryRef options;
    CGImageRef image;

    for (size_t i = 0; i < n_images; i++)
    {
        long width = get_image_width (source, i);
        gboolean is_better;

        if (best_width >= size)
        {
            is_better = (width >= size && width < best_width);
        }
        else
        {
            is_better = (width > best_width);
        }

        if (is_better)
        {
            best = i;
            best_width = width;
        }
    }

    if (best_width == 0)
    {
        return NULL;
    }

    max_size = CFNumberCreate (NULL, kCFNumberIntType, &size);
    options = CFDictionaryCreate (NULL,
                                  (const void *[]) { kCGImageSourceCreateThumbnailFromImageAlways,
                                                     kCGImageSourceThumbnailMaxPixelSize },
                                  (const void *[]) { kCFBooleanTrue, max_size },
                                  2,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    image = CGImageSourceCreateThumbnailAtIndex (source, best, options);

    CFRelease (options);
    CFRelease (max_size);

    return image;
}

static GBytes *
load_png (const char *path,
          int         size)
{
    CFDataRef custom_icon = copy_custom_icon_data (path);
    CFURLRef icon_url = NULL;
    CGImageSourceRef source = NULL;
    CGImageRef image = NULL;
    CFMutableDataRef data = NULL;
    CGImageDestinationRef destination = NULL;
    GBytes *bytes = NULL;

    if (custom_icon != NULL)
    {
        source = CGImageSourceCreateWithData (custom_icon, NULL);
    }
    if (source != NULL)
    {
        image = create_image_for_size (source, size);
    }
    if (image == NULL)
    {
        g_clear_pointer (&source, CFRelease);

        icon_url = copy_icon_url (path);
        if (icon_url != NULL)
        {
            source = CGImageSourceCreateWithURL (icon_url, NULL);
        }
        if (source != NULL)
        {
            image = create_image_for_size (source, size);
        }
    }
    if (image != NULL)
    {
        data = CFDataCreateMutable (NULL, 0);
        destination = CGImageDestinationCreateWithData (data, CFSTR ("public.png"), 1, NULL);
    }
    if (destination != NULL)
    {
        CGImageDestinationAddImage (destination, image, NULL);
        if (CGImageDestinationFinalize (destination))
        {
            bytes = g_bytes_new_with_free_func (CFDataGetBytePtr (data),
                                                CFDataGetLength (data),
                                                (GDestroyNotify) CFRelease,
                                                (gpointer) CFRetain (data));
        }
    }

    g_clear_pointer (&destination, CFRelease);
    g_clear_pointer (&data, CFRelease);
    g_clear_pointer (&image, CGImageRelease);
    g_clear_pointer (&source, CFRelease);
    g_clear_pointer (&icon_url, CFRelease);
    g_clear_pointer (&custom_icon, CFRelease);

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

    bytes = load_png (self->path, size);
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
