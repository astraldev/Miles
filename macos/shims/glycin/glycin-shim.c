/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ekure Edem */

#include "glycin.h"
#include "glycin-gtk4.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

struct _GlyLoader
{
    GObject parent;
    GFile *file;
    GInputStream *stream;
};

struct _GlyImage
{
    GObject parent;
    GBytes *bytes;
    char *mime_type;
    guint32 width;
    guint32 height;
};

struct _GlyFrameRequest
{
    GObject parent;
    guint32 width;
    guint32 height;
};

struct _GlyFrame
{
    GObject parent;
    GdkPixbuf *pixbuf;
};

G_DEFINE_FINAL_TYPE (GlyLoader, gly_loader, G_TYPE_OBJECT)
G_DEFINE_FINAL_TYPE (GlyImage, gly_image, G_TYPE_OBJECT)
G_DEFINE_FINAL_TYPE (GlyFrameRequest, gly_frame_request, G_TYPE_OBJECT)
G_DEFINE_FINAL_TYPE (GlyFrame, gly_frame, G_TYPE_OBJECT)

enum { PROP_0, PROP_FILE, N_PROPS };
static GParamSpec *loader_props[N_PROPS];

static void
gly_loader_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    GlyLoader *self = GLY_LOADER (object);

    switch (prop_id)
    {
        case PROP_FILE:
        {
            g_value_set_object (value, self->file);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
gly_loader_finalize (GObject *object)
{
    GlyLoader *self = GLY_LOADER (object);

    g_clear_object (&self->file);
    g_clear_object (&self->stream);
    G_OBJECT_CLASS (gly_loader_parent_class)->finalize (object);
}

static void
gly_loader_class_init (GlyLoaderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = gly_loader_get_property;
    object_class->finalize = gly_loader_finalize;
    loader_props[PROP_FILE] = g_param_spec_object ("file", NULL, NULL, G_TYPE_FILE,
                                                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_properties (object_class, N_PROPS, loader_props);
}

static void
gly_loader_init (GlyLoader *self)
{
}

GlyLoader *
gly_loader_new (GFile *file)
{
    GlyLoader *self = g_object_new (GLY_TYPE_LOADER, NULL);

    self->file = g_object_ref (file);
    return self;
}

GlyLoader *
gly_loader_new_for_stream (GInputStream *stream)
{
    GlyLoader *self = g_object_new (GLY_TYPE_LOADER, NULL);

    self->stream = g_object_ref (stream);
    return self;
}

GStrv
gly_loader_get_mime_types (void)
{
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_autoptr (GSList) formats = gdk_pixbuf_get_formats ();

    for (GSList *l = formats; l != NULL; l = l->next)
    {
        g_auto (GStrv) mime_types = gdk_pixbuf_format_get_mime_types (l->data);

        g_strv_builder_addv (builder, (const char **) mime_types);
    }

    return g_strv_builder_end (builder);
}

static GlyImage *
load_image (GlyLoader     *self,
            GCancellable  *cancellable,
            GError       **error)
{
    g_autoptr (GInputStream) stream = NULL;
    g_autoptr (GOutputStream) memory = g_memory_output_stream_new_resizable ();
    g_autoptr (GdkPixbufLoader) pixbuf_loader = NULL;
    g_autoptr (GBytes) bytes = NULL;
    GdkPixbufFormat *format;
    GdkPixbuf *pixbuf;
    GlyImage *image;

    if (self->file != NULL)
    {
        stream = G_INPUT_STREAM (g_file_read (self->file, cancellable, error));
    }
    else
    {
        stream = g_object_ref (self->stream);
    }

    if (stream == NULL ||
        g_output_stream_splice (memory, stream,
                                G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                cancellable, error) < 0)
    {
        return NULL;
    }

    bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (memory));
    pixbuf_loader = gdk_pixbuf_loader_new ();

    if (!gdk_pixbuf_loader_write_bytes (pixbuf_loader, bytes, error))
    {
        gdk_pixbuf_loader_close (pixbuf_loader, NULL);
        return NULL;
    }
    if (!gdk_pixbuf_loader_close (pixbuf_loader, error))
    {
        return NULL;
    }

    pixbuf = gdk_pixbuf_loader_get_pixbuf (pixbuf_loader);
    format = gdk_pixbuf_loader_get_format (pixbuf_loader);
    if (pixbuf == NULL || format == NULL)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Unknown image format");
        return NULL;
    }

    g_auto (GStrv) mime_types = gdk_pixbuf_format_get_mime_types (format);

    image = g_object_new (GLY_TYPE_IMAGE, NULL);
    image->bytes = g_steal_pointer (&bytes);
    image->mime_type = g_strdup (mime_types[0]);
    image->width = gdk_pixbuf_get_width (pixbuf);
    image->height = gdk_pixbuf_get_height (pixbuf);

    return image;
}

GlyImage *
gly_loader_load (GlyLoader  *loader,
                 GError    **error)
{
    return load_image (loader, NULL, error);
}

static void
load_thread (GTask        *task,
             gpointer      source_object,
             gpointer      task_data,
             GCancellable *cancellable)
{
    GError *error = NULL;
    GlyImage *image = load_image (source_object, cancellable, &error);

    if (image != NULL)
    {
        g_task_return_pointer (task, image, g_object_unref);
    }
    else
    {
        g_task_return_error (task, error);
    }
}

void
gly_loader_load_async (GlyLoader           *loader,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
    g_autoptr (GTask) task = g_task_new (loader, cancellable, callback, user_data);

    g_task_run_in_thread (task, load_thread);
}

GlyImage *
gly_loader_load_finish (GlyLoader     *loader,
                        GAsyncResult  *result,
                        GError       **error)
{
    return g_task_propagate_pointer (G_TASK (result), error);
}

static void
gly_image_finalize (GObject *object)
{
    GlyImage *self = GLY_IMAGE (object);

    g_clear_pointer (&self->bytes, g_bytes_unref);
    g_clear_pointer (&self->mime_type, g_free);
    G_OBJECT_CLASS (gly_image_parent_class)->finalize (object);
}

static void
gly_image_class_init (GlyImageClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = gly_image_finalize;
}

static void
gly_image_init (GlyImage *self)
{
}

const char *
gly_image_get_mime_type (GlyImage *image)
{
    return image->mime_type;
}

guint32
gly_image_get_width (GlyImage *image)
{
    return image->width;
}

guint32
gly_image_get_height (GlyImage *image)
{
    return image->height;
}

GlyFrame *
gly_image_get_specific_frame (GlyImage         *image,
                              GlyFrameRequest  *frame_request,
                              GError          **error)
{
    g_autoptr (GInputStream) stream = g_memory_input_stream_new_from_bytes (image->bytes);
    int width = frame_request->width > 0 ? (int) frame_request->width : -1;
    int height = frame_request->height > 0 ? (int) frame_request->height : -1;
    g_autoptr (GdkPixbuf) pixbuf = gdk_pixbuf_new_from_stream_at_scale (stream, width, height, TRUE,
                                                                        NULL, error);
    GlyFrame *frame;

    if (pixbuf == NULL)
    {
        return NULL;
    }

    frame = g_object_new (GLY_TYPE_FRAME, NULL);
    /* glycin turns a photo the way its EXIF data says. */
    frame->pixbuf = gdk_pixbuf_apply_embedded_orientation (pixbuf);
    if (frame->pixbuf == NULL)
    {
        frame->pixbuf = g_steal_pointer (&pixbuf);
    }

    return frame;
}

static void
gly_frame_request_class_init (GlyFrameRequestClass *klass)
{
}

static void
gly_frame_request_init (GlyFrameRequest *self)
{
}

GlyFrameRequest *
gly_frame_request_new (void)
{
    return g_object_new (GLY_TYPE_FRAME_REQUEST, NULL);
}

void
gly_frame_request_set_scale (GlyFrameRequest *frame_request,
                             guint32          width,
                             guint32          height)
{
    frame_request->width = width;
    frame_request->height = height;
}

static void
gly_frame_finalize (GObject *object)
{
    GlyFrame *self = GLY_FRAME (object);

    g_clear_object (&self->pixbuf);
    G_OBJECT_CLASS (gly_frame_parent_class)->finalize (object);
}

static void
gly_frame_class_init (GlyFrameClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = gly_frame_finalize;
}

static void
gly_frame_init (GlyFrame *self)
{
}

guint32
gly_frame_get_width (GlyFrame *frame)
{
    return gdk_pixbuf_get_width (frame->pixbuf);
}

guint32
gly_frame_get_height (GlyFrame *frame)
{
    return gdk_pixbuf_get_height (frame->pixbuf);
}

GdkTexture *
gly_gtk_frame_get_texture (GlyFrame *frame)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    return gdk_texture_new_for_pixbuf (frame->pixbuf);
    G_GNUC_END_IGNORE_DEPRECATIONS
}
