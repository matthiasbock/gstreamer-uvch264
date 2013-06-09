/* GStreamer Split File Source
 * Copyright (C) 2011 Collabora Ltd. <tim.muller@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-splitfilesrc
 * @see_also: #GstFileSrc, #GstMultiFileSrc
 *
 * Reads data from multiple files, presenting those files as one continuous
 * file to downstream elements. This is useful for reading a large file that
 * had to be split into multiple parts due to filesystem file size limitations,
 * for example.
 *
 * The files to select are chosen via the location property, which supports
 * (and expects) shell-style wildcards (but only for the filename, not for
 * directories). The results will be sorted.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch splitfilesrc location="/path/to/part-*.mpg" ! decodebin ! ...
 * ]| Plays the different parts as if they were one single MPEG file.
 * |[
 * gst-launch playbin2 uri="splitfile://path/to/foo.avi.*"
 * ]| Plays the different parts as if they were one single AVI file.
 * </refsect2>
 *
 * Since: 0.10.31
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstsplitfilesrc.h"
#include "patternspec.h"

#include <string.h>

#ifdef G_OS_WIN32
#define DEFAULT_PATTERN_MATCH_MODE MATCH_MODE_UTF8
#else
#define DEFAULT_PATTERN_MATCH_MODE MATCH_MODE_AUTO
#endif

enum
{
  PROP_LOCATION = 1
};

#define DEFAULT_LOCATION NULL

static void gst_split_file_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static void gst_split_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_split_file_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_split_file_src_finalize (GObject * obj);

static gboolean gst_split_file_src_start (GstBaseSrc * basesrc);
static gboolean gst_split_file_src_stop (GstBaseSrc * basesrc);
static gboolean gst_split_file_src_can_seek (GstBaseSrc * basesrc);
static gboolean gst_split_file_src_check_get_range (GstBaseSrc * basesrc);
static gboolean gst_split_file_src_get_size (GstBaseSrc * basesrc, guint64 * s);
static gboolean gst_split_file_src_unlock (GstBaseSrc * basesrc);
static GstFlowReturn gst_split_file_src_create (GstBaseSrc * basesrc,
    guint64 offset, guint size, GstBuffer ** buffer);

static GstStaticPadTemplate gst_split_file_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (splitfilesrc_debug);
#define GST_CAT_DEFAULT splitfilesrc_debug



static void
_do_init (GType type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_split_file_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (splitfilesrc_debug, "splitfilesrc", 0,
      "Split File src");
}

GST_BOILERPLATE_FULL (GstSplitFileSrc, gst_split_file_src, GstBaseSrc,
    GST_TYPE_BASE_SRC, _do_init);

static void
gst_split_file_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (splitfilesrc_debug, "splitfilesrc", 0,
      "splitfilesrc element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_split_file_src_pad_template));

  gst_element_class_set_details_simple (gstelement_class, "Split-File Source",
      "Source/File",
      "Read a sequentially named set of files as if it was one large file",
      "Tim-Philipp Müller <tim.muller@collabora.co.uk>");
}

#ifdef G_OS_WIN32
#define WIN32_BLURB " Location string must be in UTF-8 encoding (on Windows)."
#else
#define WIN32_BLURB             /* nothing */
#endif

static void
gst_split_file_src_class_init (GstSplitFileSrcClass * klass)
{
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_split_file_src_set_property;
  gobject_class->get_property = gst_split_file_src_get_property;
  gobject_class->finalize = gst_split_file_src_finalize;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Wildcard pattern to match file names of the input files. If "
          "the location is an absolute path or contains directory components, "
          "only the base file name part will be considered for pattern "
          "matching. The results will be sorted." WIN32_BLURB,
          DEFAULT_LOCATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_split_file_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_split_file_src_stop);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_split_file_src_create);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_split_file_src_get_size);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_split_file_src_unlock);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_split_file_src_can_seek);
  gstbasesrc_class->check_get_range =
      GST_DEBUG_FUNCPTR (gst_split_file_src_check_get_range);
}

static void
gst_split_file_src_init (GstSplitFileSrc * splitfilesrc,
    GstSplitFileSrcClass * g_class)
{
}

static void
gst_split_file_src_finalize (GObject * obj)
{
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (obj);

  g_free (src->location);
  src->location = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static gboolean
gst_split_file_src_can_seek (GstBaseSrc * basesrc)
{
  return TRUE;
}

static gboolean
gst_split_file_src_check_get_range (GstBaseSrc * basesrc)
{
  return TRUE;
}

static gboolean
gst_split_file_src_unlock (GstBaseSrc * basesrc)
{
  /* This is not actually that useful, since all normal file
   * operations are fully blocking anyway */
#if 0
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (basesrc);

  GST_DEBUG_OBJECT (src, "cancelling pending I/O operation if there is one");
  /* g_cancellable_cancel (src->cancellable); */
  GST_DEBUG_OBJECT (src, "done");
#endif

  return TRUE;
}

static gboolean
gst_split_file_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (basesrc);

  *size = src->parts[src->num_parts - 1].stop + 1;
  return TRUE;
}

static void
gst_split_file_src_set_location (GstSplitFileSrc * src, const char *location)
{
  GST_OBJECT_LOCK (src);
  g_free (src->location);

  if (location != NULL && g_str_has_prefix (location, "splitfile://"))
    src->location = gst_uri_get_location (location);
  else
    src->location = g_strdup (location);
#ifdef G_OS_WIN32
  if (!g_utf8_validate (src->location, -1, NULL)) {
    g_warning ("splitfilesrc 'location' property must be in UTF-8 "
        "encoding on Windows");
  }
#endif
  GST_OBJECT_UNLOCK (src);
}

static void
gst_split_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_split_file_src_set_location (src, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_split_file_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      GST_OBJECT_LOCK (src);
      g_value_set_string (value, src->location);
      GST_OBJECT_UNLOCK (src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static int
gst_split_file_src_array_sortfunc (gchar ** a, gchar ** b)
{
  return strcmp (*a, *b);
}

static gchar **
gst_split_file_src_find_files (GstSplitFileSrc * src, const gchar * dirname,
    const gchar * basename, GError ** err)
{
  PatternSpec *pspec;
  GPtrArray *files;
  const gchar *name;
  GDir *dir;

  if (dirname == NULL || basename == NULL)
    goto invalid_location;

  GST_INFO_OBJECT (src, "checking in directory '%s' for pattern '%s'",
      dirname, basename);

  dir = g_dir_open (dirname, 0, err);
  if (dir == NULL)
    return NULL;

  if (DEFAULT_PATTERN_MATCH_MODE == MATCH_MODE_UTF8 &&
      !g_utf8_validate (basename, -1, NULL)) {
    goto not_utf8;
  }

  /* mode will be AUTO on linux/unix and UTF8 on win32 */
  pspec = pattern_spec_new (basename, DEFAULT_PATTERN_MATCH_MODE);

  files = g_ptr_array_new ();

  while ((name = g_dir_read_name (dir))) {
    GST_TRACE_OBJECT (src, "check: %s", name);
    if (pattern_match_string (pspec, name)) {
      GST_DEBUG_OBJECT (src, "match: %s", name);
      g_ptr_array_add (files, g_build_filename (dirname, name, NULL));
    }
  }

  if (files->len == 0)
    goto no_matches;

  g_ptr_array_sort (files, (GCompareFunc) gst_split_file_src_array_sortfunc);
  g_ptr_array_add (files, NULL);

  pattern_spec_free (pspec);
  g_dir_close (dir);

  return (gchar **) g_ptr_array_free (files, FALSE);

/* ERRORS */
invalid_location:
  {
    g_set_error_literal (err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "No filename specified.");
    return NULL;
  }
not_utf8:
  {
    g_dir_close (dir);
    g_set_error_literal (err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
        "Filename pattern must be UTF-8 on Windows.");
    return NULL;
  }
no_matches:
  {
    pattern_spec_free (pspec);
    g_dir_close (dir);
    g_set_error_literal (err, G_FILE_ERROR, G_FILE_ERROR_NOENT,
        "Found no files matching the pattern.");
    return NULL;
  }
}

static gboolean
gst_split_file_src_start (GstBaseSrc * basesrc)
{
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (basesrc);
  GCancellable *cancel;
  gboolean ret = FALSE;
  guint64 offset;
  GError *err = NULL;
  gchar *basename = NULL;
  gchar *dirname = NULL;
  gchar **files;
  guint i;

  GST_OBJECT_LOCK (src);
  if (src->location != NULL && src->location[0] != '\0') {
    basename = g_path_get_basename (src->location);
    dirname = g_path_get_dirname (src->location);
  }
  GST_OBJECT_UNLOCK (src);

  files = gst_split_file_src_find_files (src, dirname, basename, &err);

  if (files == NULL || *files == NULL)
    goto no_files;

  src->num_parts = g_strv_length (files);
  src->parts = g_new0 (GstFilePart, src->num_parts);

  cancel = src->cancellable;

  offset = 0;
  for (i = 0; i < src->num_parts; ++i) {
    GFileInputStream *stream;
    GFileInfo *info;
    goffset size;
    GFile *file;

    file = g_file_new_for_path (files[i]);
    stream = g_file_read (file, cancel, &err);
    g_object_unref (file);

    if (err != NULL)
      goto open_read_error;

    info = g_file_input_stream_query_info (stream, "standard::*", NULL, &err);
    if (err != NULL) {
      g_object_unref (stream);
      goto query_info_error;
    }

    size = g_file_info_get_size (info);
    g_object_unref (info);

    src->parts[i].stream = stream;
    src->parts[i].path = g_strdup (files[i]);
    src->parts[i].start = offset;
    src->parts[i].stop = offset + size - 1;

    GST_DEBUG ("[%010" G_GUINT64_FORMAT "-%010" G_GUINT64_FORMAT "] %s",
        src->parts[i].start, src->parts[i].stop, src->parts[i].path);

    offset += size;
  }

  GST_INFO ("Successfully opened %u file parts for reading", src->num_parts);

  src->cur_part = 0;

  src->cancellable = g_cancellable_new ();

  ret = TRUE;

done:
  if (err != NULL)
    g_error_free (err);
  g_strfreev (files);
  g_free (basename);
  g_free (dirname);
  return ret;

/* ERRORS */
no_files:
  {
    if (err->code == G_IO_ERROR_CANCELLED)
      goto cancelled;

    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, ("%s", err->message),
        ("Failed to find files in '%s' for pattern '%s'",
            GST_STR_NULL (dirname), GST_STR_NULL (basename)));
    goto done;
  }
open_read_error:
  {
    if (err->code == G_IO_ERROR_CANCELLED)
      goto cancelled;

    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, ("%s", err->message),
        ("Failed to open file '%s' for reading", files[i]));
    goto done;
  }
query_info_error:
  {
    if (err->code == G_IO_ERROR_CANCELLED)
      goto cancelled;

    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, ("%s", err->message),
        ("Failed to query info for file '%s'", files[i]));
    goto done;
  }
cancelled:
  {
    GST_DEBUG_OBJECT (src, "I/O operation cancelled from another thread");
    goto done;
  }
}

static gboolean
gst_split_file_src_stop (GstBaseSrc * basesrc)
{
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (basesrc);
  guint i;

  for (i = 0; i < src->num_parts; ++i) {
    if (src->parts[i].stream != NULL)
      g_object_unref (src->parts[i].stream);
    g_free (src->parts[i].path);
  }
  g_free (src->parts);
  src->parts = NULL;
  src->num_parts = 0;

  g_object_unref (src->cancellable);
  src->cancellable = NULL;

  return TRUE;
}

static gboolean
gst_split_file_src_find_part_for_offset (GstSplitFileSrc * src, guint64 offset,
    guint * part_number)
{
  GstFilePart *part;
  guint i;

  /* TODO: could use gst_util_array_binary_search() here */
  part = src->parts;
  for (i = 0; i < src->num_parts; ++i) {
    if (offset >= part->start && offset <= part->stop) {
      *part_number = i;
      return TRUE;
    }
    ++part;
  }

  return FALSE;
}

static GstFlowReturn
gst_split_file_src_create (GstBaseSrc * basesrc, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (basesrc);
  GstFilePart cur_part;
  GInputStream *stream;
  GCancellable *cancel;
  GSeekable *seekable;
  GstBuffer *buf;
  GError *err = NULL;
  guint64 read_offset;
  guint8 *data;
  guint to_read;

  cur_part = src->parts[src->cur_part];
  if (offset < cur_part.start || offset > cur_part.stop) {
    if (!gst_split_file_src_find_part_for_offset (src, offset, &src->cur_part))
      return GST_FLOW_UNEXPECTED;
    cur_part = src->parts[src->cur_part];
  }

  GST_LOG_OBJECT (src, "current part: %u (%" G_GUINT64_FORMAT " - "
      "%" G_GUINT64_FORMAT ", %s)", src->cur_part, cur_part.start,
      cur_part.stop, cur_part.path);

  buf = gst_buffer_new_and_alloc (size);

  GST_BUFFER_OFFSET (buf) = offset;

  data = GST_BUFFER_DATA (buf);

  cancel = src->cancellable;

  while (size > 0) {
    guint64 bytes_to_end_of_part;
    gsize read = 0;

    /* we want the offset into the file part */
    read_offset = offset - cur_part.start;

    GST_LOG ("Reading part %03u from offset %" G_GUINT64_FORMAT " (%s)",
        src->cur_part, read_offset, cur_part.path);

    /* FIXME: only seek when needed (hopefully gio is smart) */
    seekable = G_SEEKABLE (cur_part.stream);
    if (!g_seekable_seek (seekable, read_offset, G_SEEK_SET, cancel, &err))
      goto seek_failed;

    GST_LOG_OBJECT (src, "now: %" G_GUINT64_FORMAT, g_seekable_tell (seekable));

    bytes_to_end_of_part = (cur_part.stop - cur_part.start) + 1 - read_offset;
    to_read = MIN (size, bytes_to_end_of_part);

    GST_LOG_OBJECT (src, "reading %u bytes from part %u (bytes to end of "
        "part: %u)", to_read, src->cur_part, (guint) bytes_to_end_of_part);

    stream = G_INPUT_STREAM (cur_part.stream);

    /* NB: we won't try to read beyond EOF */
    if (!g_input_stream_read_all (stream, data, to_read, &read, cancel, &err))
      goto read_failed;

    GST_LOG_OBJECT (src, "read %u bytes", (guint) read);

    data += read;
    size -= read;
    offset += read;

    /* are we done? */
    if (size == 0)
      break;

    GST_LOG_OBJECT (src, "%u bytes left to read for this chunk", size);

    /* corner case, this should never really happen (assuming basesrc clips
     * requests beyond the file size) */
    if (read < to_read) {
      if (src->cur_part == src->num_parts - 1) {
        /* last file part, stop reading and truncate buffer */
        GST_BUFFER_SIZE (buf) = offset - GST_BUFFER_OFFSET (buf);
        break;
      } else {
        goto file_part_changed;
      }
    }

    ++src->cur_part;
    cur_part = src->parts[src->cur_part];
  }

  GST_BUFFER_OFFSET_END (buf) = offset;

  *buffer = buf;
  GST_LOG_OBJECT (src, "read %u bytes into buf %p", GST_BUFFER_SIZE (buf), buf);
  return GST_FLOW_OK;

/* ERRORS */
seek_failed:
  {
    if (err->code == G_IO_ERROR_CANCELLED)
      goto cancelled;

    GST_ELEMENT_ERROR (src, RESOURCE, SEEK, (NULL),
        ("Seek to %" G_GUINT64_FORMAT " in %s failed", read_offset,
            cur_part.path));
    g_error_free (err);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
read_failed:
  {
    if (err->code == G_IO_ERROR_CANCELLED)
      goto cancelled;

    GST_ELEMENT_ERROR (src, RESOURCE, READ, ("%s", err->message),
        ("Read from %" G_GUINT64_FORMAT " in %s failed", read_offset,
            cur_part.path));
    g_error_free (err);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
file_part_changed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        ("Read error while reading file part %s", cur_part.path),
        ("Short read in file part, file may have been modified since start"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
cancelled:
  {
    GST_DEBUG_OBJECT (src, "I/O operation cancelled from another thread");
    g_error_free (err);
    gst_buffer_unref (buf);
    return GST_FLOW_WRONG_STATE;
  }
}

static guint
gst_split_file_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_split_file_src_uri_get_protocols (void)
{
  static const gchar *protocols[] = { "splitfile", NULL };

  return (gchar **) protocols;
}

static const gchar *
gst_split_file_src_uri_get_uri (GstURIHandler * handler)
{
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (handler);
  gchar *ret;

  GST_OBJECT_LOCK (src);
  if (src->location != NULL)
    ret = g_strdup_printf ("splitfile://%s", src->location);
  else
    ret = NULL;
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static gboolean
gst_split_file_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstSplitFileSrc *src = GST_SPLIT_FILE_SRC (handler);

  gst_split_file_src_set_location (src, uri);

  return TRUE;
}

static void
gst_split_file_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_split_file_src_uri_get_type;
  iface->get_protocols = gst_split_file_src_uri_get_protocols;
  iface->get_uri = gst_split_file_src_uri_get_uri;
  iface->set_uri = gst_split_file_src_uri_set_uri;
}
