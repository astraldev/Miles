/* SPDX-License-Identifier: LGPL-2.0-or-later
 * Copyright (C) 2026 Ekure Edem
 *
 * The trash of MacOS, for gvfs's trash backend. install-gvfs.sh copies this into daemon/trashlib. */

#include <string.h>
#include <sys/attr.h>
#include <unistd.h>

#define DS_STORE_MAX_SIZE (8 * 1024 * 1024)

static gboolean
trash_mac_skips (GFile *file)
{
  char *name = g_file_get_basename (file);
  gboolean skips = strcmp (name, ".DS_Store") == 0 || g_str_has_prefix (name, ".gvfs-expunged-");

  g_free (name);

  return skips;
}

static gboolean
trash_mac_delete (TrashItem  *item,
                  GError    **error)
{
  char *name = g_strdup_printf ("../.gvfs-expunged-%08x", g_random_int ());
  GFile *expunged = g_file_resolve_relative_path (trash_item_get_file (item), name);
  GFile *moved = g_file_get_child (expunged, "0");
  gboolean success;

  g_file_make_directory (expunged, NULL, NULL);
  success = trash_item_restore (item, moved, G_FILE_COPY_NOFOLLOW_SYMLINKS, error);
  if (success)
    trash_expunge (expunged);
  else
    g_file_delete (expunged, NULL, NULL);

  g_object_unref (moved);
  g_object_unref (expunged);
  g_free (name);

  return success;
}

typedef struct
{
  const guint8 *data;
  gsize length;
  gsize block_table;
  guint32 n_blocks;
  guint32 nodes_left;
  GHashTable *put_backs;
} DsStore;

typedef struct
{
  char *location;
  char *name;
} PutBack;

static void
put_back_free (PutBack *put_back)
{
  g_free (put_back->location);
  g_free (put_back->name);
  g_free (put_back);
}

/* The .DS_Store of a drive is not to be trusted: restoring must stay on that drive. */
static gboolean
put_back_is_safe (const PutBack *put_back)
{
  char **folders = g_strsplit (put_back->location, "/", -1);
  gboolean is_safe = !g_strv_contains ((const char * const *) folders, "..");

  g_strfreev (folders);

  return is_safe && (put_back->name == NULL ||
                     (strchr (put_back->name, '/') == NULL && strcmp (put_back->name, "..") != 0));
}

static gboolean
ds_store_read_uint32 (const DsStore *store,
                      gsize          offset,
                      guint32       *value)
{
  if (offset > store->length || store->length - offset < 4)
    return FALSE;

  memcpy (value, store->data + offset, 4);
  *value = GUINT32_FROM_BE (*value);

  return TRUE;
}

static char *
ds_store_read_string (const DsStore *store,
                      gsize          offset,
                      guint32        n_chars)
{
  char *utf8, *normalized;

  if (offset > store->length || (store->length - offset) / 2 < n_chars)
    return NULL;

  utf8 = g_convert ((const char *) store->data + offset, (gssize) n_chars * 2,
                    "UTF-8", "UTF-16BE", NULL, NULL, NULL);
  if (utf8 == NULL)
    return NULL;

  normalized = g_utf8_normalize (utf8, -1, G_NORMALIZE_NFC);
  g_free (utf8);

  return normalized;
}

static gboolean
ds_store_get_block (const DsStore *store,
                    guint32        number,
                    gsize         *offset,
                    gsize         *size)
{
  guint32 address;

  if (number >= store->n_blocks ||
      !ds_store_read_uint32 (store, store->block_table + 4 * (gsize) number, &address))
    return FALSE;

  *offset = (gsize) (address & ~0x1fu) + 4;
  *size = (gsize) 1 << (address & 0x1f);

  return *offset <= store->length && store->length - *offset >= *size;
}

static gboolean
ds_store_read_record (DsStore *store,
                      gsize   *offset,
                      gsize    end)
{
  guint32 n_chars, length;
  char type[4], id[4];
  char *name, *value = NULL;
  gsize position = *offset;

  if (!ds_store_read_uint32 (store, position, &n_chars))
    return FALSE;
  position += 4;

  name = ds_store_read_string (store, position, n_chars);
  if (name == NULL)
    return FALSE;
  position += (gsize) n_chars * 2;

  if (position > end || end - position < 8)
    {
      g_free (name);
      return FALSE;
    }
  memcpy (id, store->data + position, 4);
  memcpy (type, store->data + position + 4, 4);
  position += 8;

  if (memcmp (type, "long", 4) == 0 || memcmp (type, "shor", 4) == 0 || memcmp (type, "type", 4) == 0)
    length = 4;
  else if (memcmp (type, "bool", 4) == 0)
    length = 1;
  else if (memcmp (type, "comp", 4) == 0 || memcmp (type, "dutc", 4) == 0)
    length = 8;
  else if (memcmp (type, "blob", 4) == 0 && ds_store_read_uint32 (store, position, &length))
    position += 4;
  else if (memcmp (type, "ustr", 4) == 0 && ds_store_read_uint32 (store, position, &length) &&
           length <= G_MAXUINT32 / 2)
    {
      position += 4;
      value = ds_store_read_string (store, position, length);
      length *= 2;
    }
  else
    {
      g_free (name);
      return FALSE;
    }

  if (position > end || end - position < length)
    {
      g_free (name);
      g_free (value);
      return FALSE;
    }

  if (value != NULL && (memcmp (id, "ptbL", 4) == 0 || memcmp (id, "ptbN", 4) == 0))
    {
      PutBack *put_back = g_hash_table_lookup (store->put_backs, name);

      if (put_back == NULL)
        {
          put_back = g_new0 (PutBack, 1);
          g_hash_table_insert (store->put_backs, g_steal_pointer (&name), put_back);
        }

      if (id[3] == 'L')
        {
          g_free (put_back->location);
          put_back->location = g_steal_pointer (&value);
        }
      else
        {
          g_free (put_back->name);
          put_back->name = g_steal_pointer (&value);
        }
    }

  g_free (name);
  g_free (value);
  *offset = position + length;

  return TRUE;
}

static void
ds_store_read_node (DsStore *store,
                    guint32  number,
                    guint    depth)
{
  gsize offset, size, end;
  guint32 last_child, n_records;

  if (depth > 16 || store->nodes_left == 0 ||
      !ds_store_get_block (store, number, &offset, &size) || size < 8)
    return;

  store->nodes_left--;

  end = offset + size;
  ds_store_read_uint32 (store, offset, &last_child);
  ds_store_read_uint32 (store, offset + 4, &n_records);
  offset += 8;

  for (guint32 i = 0; i < n_records; i++)
    {
      if (last_child != 0)
        {
          guint32 child;

          if (end - offset < 4 || !ds_store_read_uint32 (store, offset, &child))
            return;
          offset += 4;
          ds_store_read_node (store, child, depth + 1);
        }

      if (!ds_store_read_record (store, &offset, end))
        return;
    }

  if (last_child != 0)
    ds_store_read_node (store, last_child, depth + 1);
}

static GHashTable *
ds_store_read_put_backs (const guint8 *data,
                         gsize         length)
{
  DsStore store = { data, length, 0, 0, 0, NULL };
  guint32 info_offset, n_names;
  gsize offset;

  store.put_backs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, (GDestroyNotify) put_back_free);

  if (length < 20 || memcmp (data, "\0\0\0\1Bud1", 8) != 0 ||
      !ds_store_read_uint32 (&store, 8, &info_offset))
    return store.put_backs;

  offset = (gsize) info_offset + 4;
  if (!ds_store_read_uint32 (&store, offset, &store.n_blocks) || store.n_blocks > length / 4)
    return store.put_backs;

  store.nodes_left = store.n_blocks;
  store.block_table = offset + 8;
  offset = store.block_table + 4 * (((gsize) store.n_blocks + 255) / 256 * 256);

  if (!ds_store_read_uint32 (&store, offset, &n_names))
    return store.put_backs;
  offset += 4;

  for (guint32 i = 0; i < n_names; i++)
    {
      guint8 name_length;
      guint32 header_block, root_node;
      gsize header_offset, header_size;

      if (offset >= length)
        break;
      name_length = data[offset];
      if (length - offset < (gsize) name_length + 5)
        break;

      ds_store_read_uint32 (&store, offset + 1 + name_length, &header_block);

      if (name_length == 4 && memcmp (data + offset + 1, "DSDB", 4) == 0 &&
          ds_store_get_block (&store, header_block, &header_offset, &header_size) &&
          ds_store_read_uint32 (&store, header_offset, &root_node))
        {
          ds_store_read_node (&store, root_node, 0);
          break;
        }

      offset += (gsize) name_length + 5;
    }

  return store.put_backs;
}

static GMutex put_back_lock;
static char *put_back_cache_path;
static time_t put_back_cache_mtime;
static goffset put_back_cache_size;
static GHashTable *put_back_cache;

static void
trash_item_get_put_back (GFile  *path,
                         GFile  *topdir,
                         GFile **original,
                         char  **date)
{
  GFile *trash_dir = g_file_get_parent (path);
  char *trash_dir_path = g_file_get_path (trash_dir);
  char *store_path = g_build_filename (trash_dir_path, ".DS_Store", NULL);
  char *item_path = g_file_get_path (path);
  char *basename = g_file_get_basename (path);
  char *name = g_utf8_normalize (basename, -1, G_NORMALIZE_NFC);
  struct attrlist attributes = { .bitmapcount = ATTR_BIT_MAP_COUNT, .commonattr = ATTR_CMN_ADDEDTIME };
  struct
  {
    guint32 length;
    struct timespec added;
  } __attribute__ ((aligned (4), packed)) buffer = { 0 };
  GStatBuf store_info;
  PutBack *put_back;

  *original = NULL;
  *date = NULL;

  g_mutex_lock (&put_back_lock);

  if (g_stat (store_path, &store_info) != 0 || !S_ISREG (store_info.st_mode) ||
      store_info.st_size > DS_STORE_MAX_SIZE)
    {
      g_clear_pointer (&put_back_cache, g_hash_table_unref);
      g_clear_pointer (&put_back_cache_path, g_free);
    }
  else if (put_back_cache == NULL ||
           g_strcmp0 (put_back_cache_path, store_path) != 0 ||
           put_back_cache_mtime != store_info.st_mtime ||
           put_back_cache_size != store_info.st_size)
    {
      char *contents;
      gsize length;

      g_clear_pointer (&put_back_cache, g_hash_table_unref);
      g_clear_pointer (&put_back_cache_path, g_free);

      if (g_file_get_contents (store_path, &contents, &length, NULL))
        {
          put_back_cache = ds_store_read_put_backs ((const guint8 *) contents, length);
          put_back_cache_path = g_strdup (store_path);
          put_back_cache_mtime = store_info.st_mtime;
          put_back_cache_size = store_info.st_size;
          g_free (contents);
        }
    }

  put_back = put_back_cache != NULL && name != NULL ? g_hash_table_lookup (put_back_cache, name) : NULL;
  if (put_back != NULL && put_back->location != NULL && put_back_is_safe (put_back))
    {
      char *volume = g_file_equal (trash_dir, topdir) || !g_str_has_suffix (trash_dir_path, "/.Trash")
                     ? g_file_get_path (topdir)
                     : g_strdup ("/");

      *original = g_file_new_build_filename (volume,
                                             put_back->location,
                                             put_back->name != NULL ? put_back->name : basename,
                                             NULL);
      g_free (volume);
    }

  g_mutex_unlock (&put_back_lock);

  if (item_path != NULL &&
      getattrlist (item_path, &attributes, &buffer, sizeof (buffer), FSOPT_NOFOLLOW) == 0 &&
      buffer.length >= sizeof (buffer))
    {
      GDateTime *added = g_date_time_new_from_unix_local (buffer.added.tv_sec);

      if (added != NULL)
        {
          *date = g_date_time_format (added, "%Y-%m-%dT%H:%M:%S");
          g_date_time_unref (added);
        }
    }

  g_free (name);
  g_free (basename);
  g_free (item_path);
  g_free (store_path);
  g_free (trash_dir_path);
  g_object_unref (trash_dir);
}
