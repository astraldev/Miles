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
#include "nautilus-mac-drag.h"

#import <AppKit/AppKit.h>

/*
 * GTK hands other apps all the dragged files as one address, which they do not find.
 */
void
nautilus_mac_drag_set_files (GSList *locations)
{
    @autoreleasepool
    {
        NSMutableArray *urls = [NSMutableArray array];
        NSPasteboard *pasteboard;

        for (GSList *l = locations; l != NULL; l = l->next)
        {
            g_autofree char *path = g_file_get_path (l->data);

            if (path != NULL)
            {
                [urls addObject: [NSURL fileURLWithFileSystemRepresentation: path
                                                                 isDirectory: NO
                                                               relativeToURL: nil]];
            }
        }

        if ([urls count] == 0)
        {
            return;
        }

        pasteboard = [NSPasteboard pasteboardWithName: NSPasteboardNameDrag];
        [pasteboard clearContents];
        [pasteboard writeObjects: urls];
    }
}
