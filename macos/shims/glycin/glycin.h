/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ekure Edem
 *
 * Stand-in for the part of glycin that Nautilus uses, on gdk-pixbuf. Real glycin needs Rust to build. */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define GLY_TYPE_LOADER (gly_loader_get_type ())
G_DECLARE_FINAL_TYPE (GlyLoader, gly_loader, GLY, LOADER, GObject)
#define GLY_TYPE_IMAGE (gly_image_get_type ())
G_DECLARE_FINAL_TYPE (GlyImage, gly_image, GLY, IMAGE, GObject)
#define GLY_TYPE_FRAME_REQUEST (gly_frame_request_get_type ())
G_DECLARE_FINAL_TYPE (GlyFrameRequest, gly_frame_request, GLY, FRAME_REQUEST, GObject)
#define GLY_TYPE_FRAME (gly_frame_get_type ())
G_DECLARE_FINAL_TYPE (GlyFrame, gly_frame, GLY, FRAME, GObject)

GlyLoader *gly_loader_new (GFile *file);
GlyLoader *gly_loader_new_for_stream (GInputStream *stream);
GStrv gly_loader_get_mime_types (void);
GlyImage *gly_loader_load (GlyLoader  *loader,
                           GError    **error);
void gly_loader_load_async (GlyLoader           *loader,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data);
GlyImage *gly_loader_load_finish (GlyLoader     *loader,
                                  GAsyncResult  *result,
                                  GError       **error);

const char *gly_image_get_mime_type (GlyImage *image);
guint32 gly_image_get_width (GlyImage *image);
guint32 gly_image_get_height (GlyImage *image);
GlyFrame *gly_image_get_specific_frame (GlyImage         *image,
                                        GlyFrameRequest  *frame_request,
                                        GError          **error);

GlyFrameRequest *gly_frame_request_new (void);
void gly_frame_request_set_scale (GlyFrameRequest *frame_request,
                                  guint32          width,
                                  guint32          height);

guint32 gly_frame_get_width (GlyFrame *frame);
guint32 gly_frame_get_height (GlyFrame *frame);

G_END_DECLS
