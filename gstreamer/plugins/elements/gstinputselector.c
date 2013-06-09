/* GStreamer input selector
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2007 Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2007 Andy Wingo <wingo@pobox.com>
 * Copyright (C) 2008 Nokia Corporation. (contact <stefan.kost@nokia.com>)
 * Copyright (C) 2011 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-input-selector
 * @see_also: #GstOutputSelector
 *
 * Direct one out of N input streams to the output pad.
 *
 * The input pads are from a GstPad subclass and have additional
 * properties, which users may find useful, namely:
 *
 * <itemizedlist>
 * <listitem>
 * "running-time": Running time of stream on pad (#gint64)
 * </listitem>
 * <listitem>
 * "tags": The currently active tags on the pad (#GstTagList, boxed type)
 * </listitem>
 * <listitem>
 * "active": If the pad is currently active (#gboolean)
 * </listitem>
 * <listitem>
 * "always-ok" : Make an inactive pads return #GST_FLOW_OK instead of
 * #GST_FLOW_NOT_LINKED
 * </listitem>
 * </itemizedlist>
 *
 * Since: 0.10.32
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstinputselector.h"

#include "gst/glib-compat-private.h"

#define DEBUG_CACHED_BUFFERS 0

GST_DEBUG_CATEGORY_STATIC (input_selector_debug);
#define GST_CAT_DEFAULT input_selector_debug

#define GST_TYPE_INPUT_SELECTOR_SYNC_MODE (gst_input_selector_sync_mode_get_type())
static GType
gst_input_selector_sync_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue data[] = {
    {GST_INPUT_SELECTOR_SYNC_MODE_ACTIVE_SEGMENT,
          "Sync using the current active segment",
        "active-segment"},
    {GST_INPUT_SELECTOR_SYNC_MODE_CLOCK, "Sync using the clock", "clock"},
    {0, NULL, NULL},
  };

  if (!type) {
    type = g_enum_register_static ("GstInputSelectorSyncMode", data);
  }
  return type;
}

#if GLIB_CHECK_VERSION(2, 26, 0)
#define NOTIFY_MUTEX_LOCK()
#define NOTIFY_MUTEX_UNLOCK()
#else
static GStaticRecMutex notify_mutex = G_STATIC_REC_MUTEX_INIT;
#define NOTIFY_MUTEX_LOCK() g_static_rec_mutex_lock (&notify_mutex)
#define NOTIFY_MUTEX_UNLOCK() g_static_rec_mutex_unlock (&notify_mutex)
#endif

#define GST_INPUT_SELECTOR_GET_LOCK(sel) (((GstInputSelector*)(sel))->lock)
#define GST_INPUT_SELECTOR_GET_COND(sel) (((GstInputSelector*)(sel))->cond)
#define GST_INPUT_SELECTOR_LOCK(sel) (g_mutex_lock (GST_INPUT_SELECTOR_GET_LOCK(sel)))
#define GST_INPUT_SELECTOR_UNLOCK(sel) (g_mutex_unlock (GST_INPUT_SELECTOR_GET_LOCK(sel)))
#define GST_INPUT_SELECTOR_WAIT(sel) (g_cond_wait (GST_INPUT_SELECTOR_GET_COND(sel), \
			GST_INPUT_SELECTOR_GET_LOCK(sel)))
#define GST_INPUT_SELECTOR_BROADCAST(sel) (g_cond_broadcast (GST_INPUT_SELECTOR_GET_COND(sel)))

static GstStaticPadTemplate gst_input_selector_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_input_selector_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_N_PADS,
  PROP_ACTIVE_PAD,
  PROP_SYNC_STREAMS,
  PROP_SYNC_MODE,
  PROP_CACHE_BUFFERS
};

#define DEFAULT_SYNC_STREAMS FALSE
#define DEFAULT_SYNC_MODE GST_INPUT_SELECTOR_SYNC_MODE_ACTIVE_SEGMENT
#define DEFAULT_CACHE_BUFFERS FALSE

#define DEFAULT_PAD_ALWAYS_OK TRUE

enum
{
  PROP_PAD_0,
  PROP_PAD_RUNNING_TIME,
  PROP_PAD_TAGS,
  PROP_PAD_ACTIVE,
  PROP_PAD_ALWAYS_OK
};

enum
{
  /* methods */
  SIGNAL_BLOCK,
  SIGNAL_SWITCH,
  LAST_SIGNAL
};
static guint gst_input_selector_signals[LAST_SIGNAL] = { 0 };

static void gst_input_selector_active_pad_changed (GstInputSelector * sel,
    GParamSpec * pspec, gpointer user_data);
static inline gboolean gst_input_selector_is_active_sinkpad (GstInputSelector *
    sel, GstPad * pad);
static GstPad *gst_input_selector_activate_sinkpad (GstInputSelector * sel,
    GstPad * pad);
static GstPad *gst_input_selector_get_linked_pad (GstInputSelector * sel,
    GstPad * pad, gboolean strict);

#define GST_TYPE_SELECTOR_PAD \
  (gst_selector_pad_get_type())
#define GST_SELECTOR_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SELECTOR_PAD, GstSelectorPad))
#define GST_SELECTOR_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SELECTOR_PAD, GstSelectorPadClass))
#define GST_IS_SELECTOR_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SELECTOR_PAD))
#define GST_IS_SELECTOR_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SELECTOR_PAD))
#define GST_SELECTOR_PAD_CAST(obj) \
  ((GstSelectorPad *)(obj))

typedef struct _GstSelectorPad GstSelectorPad;
typedef struct _GstSelectorPadClass GstSelectorPadClass;
typedef struct _GstSelectorPadCachedBuffer GstSelectorPadCachedBuffer;

struct _GstSelectorPad
{
  GstPad parent;

  gboolean active;              /* when buffer have passed the pad */
  gboolean pushed;              /* when buffer was pushed downstream since activation */
  gboolean eos;                 /* when EOS has been received */
  gboolean eos_sent;            /* when EOS was sent downstream */
  gboolean discont;             /* after switching we create a discont */
  gboolean flushing;            /* set after flush-start and before flush-stop */
  gboolean always_ok;
  gboolean segment_update;
  GstSegment segment;           /* the current segment on the pad */
  GstTagList *tags;             /* last tags received on the pad */

  gboolean sending_cached_buffers;
  GQueue *cached_buffers;

  gboolean segment_pending;
};

struct _GstSelectorPadCachedBuffer
{
  GstBuffer *buffer;
  GstSegment segment;
  gboolean segment_update;
};

struct _GstSelectorPadClass
{
  GstPadClass parent;
};

static void gst_selector_pad_class_init (GstSelectorPadClass * klass);
static void gst_selector_pad_init (GstSelectorPad * pad);
static void gst_selector_pad_finalize (GObject * object);
static void gst_selector_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_selector_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static GstPadClass *selector_pad_parent_class = NULL;

static gint64 gst_selector_pad_get_running_time (GstSelectorPad * pad);
static void gst_selector_pad_reset (GstSelectorPad * pad);
static void gst_selector_pad_cache_buffer (GstSelectorPad * selpad,
    GstBuffer * buffer);
static void gst_selector_pad_free_cached_buffers (GstSelectorPad * selpad);

static gboolean gst_selector_pad_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_selector_pad_getcaps (GstPad * pad);
static gboolean gst_selector_pad_acceptcaps (GstPad * pad, GstCaps * caps);
static GstIterator *gst_selector_pad_iterate_linked_pads (GstPad * pad);
static GstFlowReturn gst_selector_pad_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_selector_pad_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);

static GType
gst_selector_pad_get_type (void)
{
  static volatile gsize selector_pad_type = 0;
  static const GTypeInfo selector_pad_info = {
    sizeof (GstSelectorPadClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_selector_pad_class_init,
    NULL,
    NULL,
    sizeof (GstSelectorPad),
    0,
    (GInstanceInitFunc) gst_selector_pad_init,
  };

  if (g_once_init_enter (&selector_pad_type)) {
    GType tmp = g_type_register_static (GST_TYPE_PAD, "GstSelectorPad",
        &selector_pad_info, 0);
    g_once_init_leave (&selector_pad_type, tmp);
  }

  return (GType) selector_pad_type;
}

static void
gst_selector_pad_class_init (GstSelectorPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  selector_pad_parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_selector_pad_finalize;

  gobject_class->get_property = gst_selector_pad_get_property;
  gobject_class->set_property = gst_selector_pad_set_property;

  g_object_class_install_property (gobject_class, PROP_PAD_RUNNING_TIME,
      g_param_spec_int64 ("running-time", "Running time",
          "Running time of stream on pad", 0, G_MAXINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_TAGS,
      g_param_spec_boxed ("tags", "Tags",
          "The currently active tags on the pad", GST_TYPE_TAG_LIST,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ACTIVE,
      g_param_spec_boolean ("active", "Active",
          "If the pad is currently active", FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /* FIXME: better property name? */
  g_object_class_install_property (gobject_class, PROP_PAD_ALWAYS_OK,
      g_param_spec_boolean ("always-ok", "Always OK",
          "Make an inactive pad return OK instead of NOT_LINKED",
          DEFAULT_PAD_ALWAYS_OK, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_selector_pad_init (GstSelectorPad * pad)
{
  pad->always_ok = DEFAULT_PAD_ALWAYS_OK;
  gst_selector_pad_reset (pad);
}

static void
gst_selector_pad_finalize (GObject * object)
{
  GstSelectorPad *pad;

  pad = GST_SELECTOR_PAD_CAST (object);

  if (pad->tags)
    gst_tag_list_free (pad->tags);
  gst_selector_pad_free_cached_buffers (pad);

  G_OBJECT_CLASS (selector_pad_parent_class)->finalize (object);
}

static void
gst_selector_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSelectorPad *spad = GST_SELECTOR_PAD_CAST (object);

  switch (prop_id) {
    case PROP_PAD_ALWAYS_OK:
      GST_OBJECT_LOCK (object);
      spad->always_ok = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_selector_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSelectorPad *spad = GST_SELECTOR_PAD_CAST (object);

  switch (prop_id) {
    case PROP_PAD_RUNNING_TIME:
      g_value_set_int64 (value, gst_selector_pad_get_running_time (spad));
      break;
    case PROP_PAD_TAGS:
      GST_OBJECT_LOCK (object);
      g_value_set_boxed (value, spad->tags);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_PAD_ACTIVE:
    {
      GstInputSelector *sel;

      sel = GST_INPUT_SELECTOR (gst_pad_get_parent (spad));
      g_value_set_boolean (value, gst_input_selector_is_active_sinkpad (sel,
              GST_PAD_CAST (spad)));
      gst_object_unref (sel);
      break;
    }
    case PROP_PAD_ALWAYS_OK:
      GST_OBJECT_LOCK (object);
      g_value_set_boolean (value, spad->always_ok);
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint64
gst_selector_pad_get_running_time (GstSelectorPad * pad)
{
  gint64 ret = 0;

  GST_OBJECT_LOCK (pad);
  if (pad->active) {
    gint64 last_stop = pad->segment.last_stop;

    if (last_stop >= 0)
      ret = gst_segment_to_running_time (&pad->segment, GST_FORMAT_TIME,
          last_stop);
  }
  GST_OBJECT_UNLOCK (pad);

  GST_DEBUG_OBJECT (pad, "running time: %" GST_TIME_FORMAT
      " segment: %" GST_SEGMENT_FORMAT, GST_TIME_ARGS (ret), &pad->segment);

  return ret;
}

/* must be called with the SELECTOR_LOCK */
static void
gst_selector_pad_reset (GstSelectorPad * pad)
{
  GST_OBJECT_LOCK (pad);
  pad->active = FALSE;
  pad->pushed = FALSE;
  pad->eos = FALSE;
  pad->eos_sent = FALSE;
  pad->segment_pending = FALSE;
  pad->discont = FALSE;
  pad->flushing = FALSE;
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
  pad->segment_update = FALSE;
  pad->sending_cached_buffers = FALSE;
  gst_selector_pad_free_cached_buffers (pad);
  GST_OBJECT_UNLOCK (pad);
}

static GstSelectorPadCachedBuffer *
gst_selector_pad_new_cached_buffer (GstSelectorPad * selpad, GstBuffer * buffer)
{
  GstSelectorPadCachedBuffer *cached_buffer =
      g_slice_new (GstSelectorPadCachedBuffer);
  cached_buffer->buffer = buffer;
  cached_buffer->segment = selpad->segment;
  cached_buffer->segment_update = selpad->segment_update;
  return cached_buffer;
}

static void
gst_selector_pad_free_cached_buffer (GstSelectorPadCachedBuffer * cached_buffer)
{
  gst_buffer_unref (cached_buffer->buffer);
  g_slice_free (GstSelectorPadCachedBuffer, cached_buffer);
}

/* must be called with the SELECTOR_LOCK */
static void
gst_selector_pad_cache_buffer (GstSelectorPad * selpad, GstBuffer * buffer)
{
  if (selpad->segment.format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (selpad, "Buffer %p with segment not in time format, "
        "not caching", buffer);
    return;
  }

  GST_DEBUG_OBJECT (selpad, "Caching buffer %p", buffer);
  if (!selpad->cached_buffers)
    selpad->cached_buffers = g_queue_new ();
  g_queue_push_tail (selpad->cached_buffers,
      gst_selector_pad_new_cached_buffer (selpad, buffer));
}

/* must be called with the SELECTOR_LOCK */
static void
gst_selector_pad_free_cached_buffers (GstSelectorPad * selpad)
{
  GstSelectorPadCachedBuffer *cached_buffer;

  if (!selpad->cached_buffers)
    return;

  GST_DEBUG_OBJECT (selpad, "Freeing cached buffers");
  while ((cached_buffer = g_queue_pop_head (selpad->cached_buffers)))
    gst_selector_pad_free_cached_buffer (cached_buffer);
  g_queue_free (selpad->cached_buffers);
  selpad->cached_buffers = NULL;
}

/* strictly get the linked pad from the sinkpad. If the pad is active we return
 * the srcpad else we return NULL */
static GstIterator *
gst_selector_pad_iterate_linked_pads (GstPad * pad)
{
  GstInputSelector *sel;
  GstPad *otherpad;
  GstIterator *it;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return NULL;

  otherpad = gst_input_selector_get_linked_pad (sel, pad, TRUE);
  it = gst_iterator_new_single (GST_TYPE_PAD, otherpad,
      (GstCopyFunction) gst_object_ref, (GFreeFunc) gst_object_unref);

  if (otherpad)
    gst_object_unref (otherpad);
  gst_object_unref (sel);

  return it;
}

static gboolean
gst_selector_pad_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  gboolean forward;
  gboolean new_tags = FALSE;
  GstInputSelector *sel;
  GstSelectorPad *selpad;
  GstPad *prev_active_sinkpad;
  GstPad *active_sinkpad;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }
  selpad = GST_SELECTOR_PAD_CAST (pad);
  GST_DEBUG_OBJECT (selpad, "received event %" GST_PTR_FORMAT, event);

  GST_INPUT_SELECTOR_LOCK (sel);
  prev_active_sinkpad = sel->active_sinkpad;
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);
  GST_INPUT_SELECTOR_UNLOCK (sel);

  if (prev_active_sinkpad != active_sinkpad && pad == active_sinkpad) {
    NOTIFY_MUTEX_LOCK ();
    g_object_notify (G_OBJECT (sel), "active-pad");
    NOTIFY_MUTEX_UNLOCK ();
  }

  GST_INPUT_SELECTOR_LOCK (sel);
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);

  /* only forward if we are dealing with the active sinkpad */
  forward = (pad == active_sinkpad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      /* Unblock the pad if it's waiting */
      selpad->flushing = TRUE;
      GST_INPUT_SELECTOR_BROADCAST (sel);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_selector_pad_reset (selpad);
      sel->pending_close = FALSE;
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      GST_DEBUG_OBJECT (pad,
          "configured NEWSEGMENT update %d, rate %lf, applied rate %lf, "
          "format %d, "
          "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
          G_GINT64_FORMAT, update, rate, arate, format, start, stop, time);

      gst_segment_set_newsegment_full (&selpad->segment, update,
          rate, arate, format, start, stop, time);
      /* keep the segment update value to be used when resending cached
       * buffers if in sync-mode */
      selpad->segment_update = update;

      /* If we aren't forwarding the event because the pad is not the
       * active_sinkpad, then set the flag on the pad
       * that says a segment needs sending if/when that pad is activated.
       * For all other cases, we send the event immediately, which makes
       * sparse streams and other segment updates work correctly downstream.
       */
      if (!forward)
        selpad->segment_pending = TRUE;
      break;
    }
    case GST_EVENT_TAG:
    {
      GstTagList *tags, *oldtags, *newtags;

      gst_event_parse_tag (event, &tags);

      oldtags = selpad->tags;

      newtags = gst_tag_list_merge (oldtags, tags, GST_TAG_MERGE_REPLACE);
      selpad->tags = newtags;
      if (oldtags)
        gst_tag_list_free (oldtags);
      GST_DEBUG_OBJECT (pad, "received tags %" GST_PTR_FORMAT, newtags);

      new_tags = TRUE;
      break;
    }
    case GST_EVENT_EOS:
      selpad->eos = TRUE;

      if (forward) {
        selpad->eos_sent = TRUE;
      } else {
        GstSelectorPad *active_selpad;

        /* If the active sinkpad is in EOS state but EOS
         * was not sent downstream this means that the pad
         * got EOS before it was set as active pad and that
         * the previously active pad got EOS after it was
         * active
         */
        active_selpad = GST_SELECTOR_PAD (active_sinkpad);
        forward = (active_selpad->eos && !active_selpad->eos_sent);
        active_selpad->eos_sent = TRUE;
      }
      GST_DEBUG_OBJECT (pad, "received EOS");
      break;
    default:
      break;
  }
  GST_INPUT_SELECTOR_UNLOCK (sel);
  if (new_tags)
    g_object_notify (G_OBJECT (selpad), "tags");
  if (forward) {
    GST_DEBUG_OBJECT (pad, "forwarding event");
    res = gst_pad_push_event (sel->srcpad, event);
  } else
    gst_event_unref (event);

  gst_object_unref (sel);

  return res;
}

static GstCaps *
gst_selector_pad_getcaps (GstPad * pad)
{
  GstInputSelector *sel;
  GstCaps *caps;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return gst_caps_new_any ();

  GST_DEBUG_OBJECT (sel, "Getting caps of srcpad peer");
  caps = gst_pad_peer_get_caps_reffed (sel->srcpad);
  if (caps == NULL)
    caps = gst_caps_new_any ();

  gst_object_unref (sel);

  return caps;
}

static gboolean
gst_selector_pad_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstInputSelector *sel;
  gboolean res;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return FALSE;

  GST_DEBUG_OBJECT (sel, "Checking acceptcaps of srcpad peer");
  res = gst_pad_peer_accept_caps (sel->srcpad, caps);
  gst_object_unref (sel);

  return res;
}

static GstFlowReturn
gst_selector_pad_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstInputSelector *sel;
  GstFlowReturn result;
  GstPad *active_sinkpad;
  GstPad *prev_active_sinkpad;
  GstSelectorPad *selpad;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return GST_FLOW_WRONG_STATE;

  selpad = GST_SELECTOR_PAD_CAST (pad);

  GST_LOG_OBJECT (pad, "received alloc");

  GST_INPUT_SELECTOR_LOCK (sel);
  prev_active_sinkpad = sel->active_sinkpad;
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);

  if (pad != active_sinkpad)
    goto not_active;

  GST_INPUT_SELECTOR_UNLOCK (sel);

  if (prev_active_sinkpad != active_sinkpad && pad == active_sinkpad) {
    NOTIFY_MUTEX_LOCK ();
    g_object_notify (G_OBJECT (sel), "active-pad");
    NOTIFY_MUTEX_UNLOCK ();
  }

  result = gst_pad_alloc_buffer (sel->srcpad, offset, size, caps, buf);

done:
  gst_object_unref (sel);

  return result;

  /* ERRORS */
not_active:
  {
    gboolean active_pad_pushed = GST_SELECTOR_PAD_CAST (active_sinkpad)->pushed;

    GST_INPUT_SELECTOR_UNLOCK (sel);

    /* unselected pad, perform fallback alloc or return unlinked when
     * asked */
    GST_OBJECT_LOCK (selpad);
    if (selpad->always_ok || !active_pad_pushed || sel->cache_buffers) {
      GST_DEBUG_OBJECT (pad, "Not selected, performing fallback allocation");
      *buf = NULL;
      result = GST_FLOW_OK;
    } else {
      GST_DEBUG_OBJECT (pad, "Not selected, return NOT_LINKED");
      result = GST_FLOW_NOT_LINKED;
    }
    GST_OBJECT_UNLOCK (selpad);

    goto done;
  }
}

/* must be called with the SELECTOR_LOCK, will block while the pad is blocked 
 * or return TRUE when flushing */
static gboolean
gst_input_selector_wait (GstInputSelector * self, GstSelectorPad * pad)
{
  while (self->blocked && !self->flushing && !pad->flushing) {
    /* we can be unlocked here when we are shutting down (flushing) or when we
     * get unblocked */
    GST_INPUT_SELECTOR_WAIT (self);
  }
  return self->flushing;
}

/* must be called without the SELECTOR_LOCK, will wait until the running time
 * of the active pad is after this pad or return TRUE when flushing */
static gboolean
gst_input_selector_wait_running_time (GstInputSelector * sel,
    GstSelectorPad * selpad, GstBuffer * buf)
{
  GstSegment *seg;

  GST_DEBUG_OBJECT (selpad, "entering wait for buffer %p", buf);

  /* If we have no valid timestamp we can't sync this buffer */
  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    GST_DEBUG_OBJECT (selpad, "leaving wait for buffer with "
        "invalid timestamp");
    return FALSE;
  }

  seg = &selpad->segment;

  /* Wait until
   *   a) this is the active pad
   *   b) the pad or the selector is flushing
   *   c) the selector is not blocked
   *   d) the buffer running time is before the current running time
   *      (either active-seg or clock, depending on sync-mode)
   */

  GST_INPUT_SELECTOR_LOCK (sel);
  while (TRUE) {
    GstPad *active_sinkpad;
    GstSelectorPad *active_selpad;
    GstClock *clock;
    gint64 cur_running_time;
    GstClockTime running_time;

    active_sinkpad =
        gst_input_selector_activate_sinkpad (sel, GST_PAD_CAST (selpad));
    active_selpad = GST_SELECTOR_PAD_CAST (active_sinkpad);

    if (seg->format != GST_FORMAT_TIME) {
      GST_INPUT_SELECTOR_UNLOCK (sel);
      return FALSE;
    }

    running_time = GST_BUFFER_TIMESTAMP (buf);
    /* If possible try to get the running time at the end of the buffer */
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      running_time += GST_BUFFER_DURATION (buf);
    /* Only use the segment to convert to running time if the segment is
     * in TIME format, otherwise do our best to try to sync */
    if (GST_CLOCK_TIME_IS_VALID (seg->stop)) {
      if (running_time > seg->stop) {
        running_time = seg->stop;
      }
    }
    running_time =
        gst_segment_to_running_time (seg, GST_FORMAT_TIME, running_time);
    /* If this is outside the segment don't sync */
    if (running_time == -1) {
      GST_INPUT_SELECTOR_UNLOCK (sel);
      return FALSE;
    }

    cur_running_time = GST_CLOCK_TIME_NONE;
    if (sel->sync_mode == GST_INPUT_SELECTOR_SYNC_MODE_CLOCK) {
      clock = gst_element_get_clock (GST_ELEMENT_CAST (sel));
      if (clock) {
        GstClockTime base_time;

        cur_running_time = gst_clock_get_time (clock);
        base_time = gst_element_get_base_time (GST_ELEMENT_CAST (sel));
        if (base_time <= cur_running_time)
          cur_running_time -= base_time;
        else
          cur_running_time = 0;
      }
    } else {
      GstSegment *active_seg;

      active_seg = &active_selpad->segment;

      /* If the active segment is configured but not to time format
       * we can't do any syncing at all */
      if (active_seg->format != GST_FORMAT_TIME
          && active_seg->format != GST_FORMAT_UNDEFINED) {
        GST_INPUT_SELECTOR_UNLOCK (sel);
        return FALSE;
      }

      /* Get active pad's running time, if no configured segment yet keep at -1 */
      if (active_seg->format == GST_FORMAT_TIME)
        cur_running_time = gst_segment_to_running_time (active_seg,
            GST_FORMAT_TIME, active_seg->last_stop);
    }

    if (selpad != active_selpad && !sel->flushing && !selpad->flushing &&
        (sel->cache_buffers || active_selpad->pushed) &&
        (sel->blocked || cur_running_time == -1
            || running_time >= cur_running_time)) {
      if (!sel->blocked) {
        GST_DEBUG_OBJECT (selpad,
            "Waiting for active streams to advance. %" GST_TIME_FORMAT " >= %"
            GST_TIME_FORMAT, GST_TIME_ARGS (running_time),
            GST_TIME_ARGS (cur_running_time));
      }

      GST_INPUT_SELECTOR_WAIT (sel);
    } else {
      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    }
  }

  /* Return TRUE if the selector or the pad is flushing */
  return (sel->flushing || selpad->flushing);
}

#if DEBUG_CACHED_BUFFERS
static void
gst_input_selector_debug_cached_buffers (GstInputSelector * sel)
{
  GList *walk;

  for (walk = GST_ELEMENT_CAST (sel)->sinkpads; walk; walk = g_list_next (walk)) {
    GstSelectorPad *selpad;
    GString *timestamps;
    gchar *str;
    int i;

    selpad = GST_SELECTOR_PAD_CAST (walk->data);
    if (!selpad->cached_buffers) {
      GST_DEBUG_OBJECT (selpad, "Cached buffers timestamps: <none>");
      continue;
    }

    timestamps = g_string_new ("Cached buffers timestamps:");
    for (i = 0; i < selpad->cached_buffers->length; ++i) {
      GstSelectorPadCachedBuffer *cached_buffer;

      cached_buffer = g_queue_peek_nth (selpad->cached_buffers, i);
      g_string_append_printf (timestamps, " %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (cached_buffer->buffer)));
    }
    str = g_string_free (timestamps, FALSE);
    GST_DEBUG_OBJECT (selpad, str);
    g_free (str);
  }
}
#endif

/* must be called with the SELECTOR_LOCK */
static void
gst_input_selector_cleanup_old_cached_buffers (GstInputSelector * sel,
    GstPad * pad)
{
  GstClock *clock;
  gint64 cur_running_time;
  GList *walk;

  cur_running_time = GST_CLOCK_TIME_NONE;
  if (sel->sync_mode == GST_INPUT_SELECTOR_SYNC_MODE_CLOCK) {
    clock = gst_element_get_clock (GST_ELEMENT_CAST (sel));
    if (clock) {
      GstClockTime base_time;

      cur_running_time = gst_clock_get_time (clock);
      base_time = gst_element_get_base_time (GST_ELEMENT_CAST (sel));
      if (base_time <= cur_running_time)
        cur_running_time -= base_time;
      else
        cur_running_time = 0;
    }
  } else {
    GstPad *active_sinkpad;
    GstSelectorPad *active_selpad;
    GstSegment *active_seg;

    active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);
    active_selpad = GST_SELECTOR_PAD_CAST (active_sinkpad);
    active_seg = &active_selpad->segment;

    /* Get active pad's running time, if no configured segment yet keep at -1 */
    if (active_seg->format == GST_FORMAT_TIME)
      cur_running_time = gst_segment_to_running_time (active_seg,
          GST_FORMAT_TIME, active_seg->last_stop);
  }

  if (!GST_CLOCK_TIME_IS_VALID (cur_running_time))
    return;

  GST_DEBUG_OBJECT (sel, "Cleaning up old cached buffers");
  for (walk = GST_ELEMENT_CAST (sel)->sinkpads; walk; walk = g_list_next (walk)) {
    GstSelectorPad *selpad;
    GstSegment *seg;
    GstSelectorPadCachedBuffer *cached_buffer;
    GSList *maybe_remove;
    guint queue_position;

    selpad = GST_SELECTOR_PAD_CAST (walk->data);
    if (!selpad->cached_buffers)
      continue;

    seg = &selpad->segment;

    maybe_remove = NULL;
    queue_position = 0;
    while ((cached_buffer = g_queue_peek_nth (selpad->cached_buffers,
                queue_position))) {
      GstBuffer *buffer = cached_buffer->buffer;
      GstClockTime running_time;
      GSList *l;

      /* If we have no valid timestamp we can't sync this buffer */
      if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
        maybe_remove = g_slist_append (maybe_remove, cached_buffer);
        queue_position = g_slist_length (maybe_remove);
        continue;
      }

      /* the buffer is still valid if its duration is valid and the
       * timestamp + duration is >= time, or if its duration is invalid
       * and the timestamp is >= time */
      running_time = GST_BUFFER_TIMESTAMP (buffer);
      /* If possible try to get the running time at the end of the buffer */
      if (GST_BUFFER_DURATION_IS_VALID (buffer))
        running_time += GST_BUFFER_DURATION (buffer);
      /* Only use the segment to convert to running time if the segment is
       * in TIME format, otherwise do our best to try to sync */
      if (GST_CLOCK_TIME_IS_VALID (seg->stop)) {
        if (running_time > seg->stop) {
          running_time = seg->stop;
        }
      }
      running_time =
          gst_segment_to_running_time (seg, GST_FORMAT_TIME, running_time);

      GST_DEBUG_OBJECT (selpad,
          "checking if buffer %p running time=%" GST_TIME_FORMAT
          " >= stream time=%" GST_TIME_FORMAT, buffer,
          GST_TIME_ARGS (running_time), GST_TIME_ARGS (cur_running_time));
      if (running_time >= cur_running_time) {
        break;
      }

      GST_DEBUG_OBJECT (selpad, "Removing old cached buffer %p", buffer);
      g_queue_pop_nth (selpad->cached_buffers, queue_position);
      gst_selector_pad_free_cached_buffer (cached_buffer);

      for (l = maybe_remove; l != NULL; l = g_slist_next (l)) {
        /* A buffer after some invalid buffers was removed, it means the invalid buffers
         * are old, lets also remove them */
        cached_buffer = l->data;
        g_queue_remove (selpad->cached_buffers, cached_buffer);
        gst_selector_pad_free_cached_buffer (cached_buffer);
      }

      g_slist_free (maybe_remove);
      maybe_remove = NULL;
      queue_position = 0;
    }

    g_slist_free (maybe_remove);
    maybe_remove = NULL;

    if (g_queue_is_empty (selpad->cached_buffers)) {
      g_queue_free (selpad->cached_buffers);
      selpad->cached_buffers = NULL;
    }
  }

#if DEBUG_CACHED_BUFFERS
  gst_input_selector_debug_cached_buffers (sel);
#endif
}

static GstFlowReturn
gst_selector_pad_chain (GstPad * pad, GstBuffer * buf)
{
  GstInputSelector *sel;
  GstFlowReturn res;
  GstPad *active_sinkpad;
  GstPad *prev_active_sinkpad;
  GstSelectorPad *selpad;
  GstClockTime start_time;
  GstSegment *seg;
  GstEvent *close_event = NULL, *start_event = NULL;
  GstCaps *caps;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  selpad = GST_SELECTOR_PAD_CAST (pad);
  seg = &selpad->segment;

  GST_DEBUG_OBJECT (selpad,
      "entering chain for buf %p with timestamp %" GST_TIME_FORMAT, buf,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  GST_INPUT_SELECTOR_LOCK (sel);
  /* wait or check for flushing */
  if (gst_input_selector_wait (sel, selpad)) {
    GST_INPUT_SELECTOR_UNLOCK (sel);
    goto flushing;
  }

  GST_LOG_OBJECT (pad, "getting active pad");

  prev_active_sinkpad = sel->active_sinkpad;
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);

  /* In sync mode wait until the active pad has advanced
   * after the running time of the current buffer */
  if (sel->sync_streams) {
    /* call chain for each cached buffer if we are not the active pad
     * or if we are the active pad but didn't push anything yet. */
    if (active_sinkpad != pad || !selpad->pushed) {
      /* no need to check for sel->cache_buffers as selpad->cached_buffers
       * will only be valid if cache_buffers is TRUE */
      if (selpad->cached_buffers && !selpad->sending_cached_buffers) {
        GstSelectorPadCachedBuffer *cached_buffer;
        GstSegment saved_segment;
        gboolean saved_segment_update;

        saved_segment = selpad->segment;
        saved_segment_update = selpad->segment_update;

        selpad->sending_cached_buffers = TRUE;
        while (!sel->flushing && !selpad->flushing &&
            (cached_buffer = g_queue_pop_head (selpad->cached_buffers))) {
          GST_DEBUG_OBJECT (pad, "Cached buffers found, "
              "invoking chain for cached buffer %p", cached_buffer->buffer);

          selpad->segment = cached_buffer->segment;
          selpad->segment_update = cached_buffer->segment_update;
          selpad->segment_pending = TRUE;
          GST_INPUT_SELECTOR_UNLOCK (sel);
          gst_selector_pad_chain (pad, cached_buffer->buffer);
          GST_INPUT_SELECTOR_LOCK (sel);

          /* we may have cleaned up the queue in the meantime because of
           * old buffers */
          if (!selpad->cached_buffers) {
            break;
          }
        }
        selpad->sending_cached_buffers = FALSE;

        /* all cached buffers sent, restore segment for current buffer */
        selpad->segment = saved_segment;
        selpad->segment_update = saved_segment_update;
        selpad->segment_pending = TRUE;

        /* Might have changed while calling chain for cached buffers */
        active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);
      }
    }

    if (active_sinkpad != pad) {
      GST_INPUT_SELECTOR_UNLOCK (sel);
      if (gst_input_selector_wait_running_time (sel, selpad, buf))
        goto flushing;
      GST_INPUT_SELECTOR_LOCK (sel);
    }

    /* Might have changed while waiting */
    active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);
  }

  /* update the segment on the srcpad */
  start_time = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (start_time)) {
    GST_LOG_OBJECT (pad, "received start time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start_time));
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      GST_LOG_OBJECT (pad, "received end time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start_time + GST_BUFFER_DURATION (buf)));

    GST_OBJECT_LOCK (pad);
    gst_segment_set_last_stop (seg, seg->format, start_time);
    GST_OBJECT_UNLOCK (pad);
  }

  /* Ignore buffers from pads except the selected one */
  if (pad != active_sinkpad)
    goto ignore;

  /* Tell all non-active pads that we advanced the running time */
  if (sel->sync_streams)
    GST_INPUT_SELECTOR_BROADCAST (sel);

  if (G_UNLIKELY (sel->pending_close)) {
    GstSegment *cseg = &sel->segment;

    GST_DEBUG_OBJECT (sel,
        "pushing close NEWSEGMENT update %d, rate %lf, applied rate %lf, "
        "format %d, "
        "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
        G_GINT64_FORMAT, TRUE, cseg->rate, cseg->applied_rate, cseg->format,
        cseg->start, cseg->stop, cseg->time);

    /* create update segment */
    close_event = gst_event_new_new_segment_full (TRUE, cseg->rate,
        cseg->applied_rate, cseg->format, cseg->start, cseg->stop, cseg->time);

    sel->pending_close = FALSE;
  }
  /* if we have a pending segment, push it out now */
  if (G_UNLIKELY (selpad->segment_pending)) {
    if (G_UNLIKELY (seg->format == GST_FORMAT_UNDEFINED)) {
      GST_ERROR_OBJECT (pad, "buffers arrived before NEWSEGMENT event");

    } else {
      gboolean update = FALSE;

      if (sel->sync_streams && sel->cache_buffers) {
        /* if we didn't push anything on this pad after it became active, send an
         * update FALSE to reset the segments, otherwise use whatever is in the
         * selpad current segment update value */
        update = selpad->pushed ? selpad->segment_update : FALSE;
      }

      GST_DEBUG_OBJECT (pad,
          "pushing pending NEWSEGMENT update %d, rate %lf, applied rate %lf, "
          "format %d, "
          "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
          G_GINT64_FORMAT, update,
          seg->rate, seg->applied_rate, seg->format,
          seg->start, seg->stop, seg->time);

      start_event = gst_event_new_new_segment_full (update, seg->rate,
          seg->applied_rate, seg->format, seg->start, seg->stop, seg->time);
      selpad->segment_pending = FALSE;
    }
  }
  GST_INPUT_SELECTOR_UNLOCK (sel);

  if (prev_active_sinkpad != active_sinkpad && pad == active_sinkpad) {
    NOTIFY_MUTEX_LOCK ();
    g_object_notify (G_OBJECT (sel), "active-pad");
    NOTIFY_MUTEX_UNLOCK ();
  }

  if (close_event)
    gst_pad_push_event (sel->srcpad, close_event);

  if (start_event)
    gst_pad_push_event (sel->srcpad, start_event);

  if (selpad->discont) {
    buf = gst_buffer_make_metadata_writable (buf);

    GST_DEBUG_OBJECT (pad, "Marking discont buffer %p", buf);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    selpad->discont = FALSE;
  }

  /* forward */
  GST_LOG_OBJECT (pad, "Forwarding buffer %p with timestamp %" GST_TIME_FORMAT,
      buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  if ((caps = GST_BUFFER_CAPS (buf))) {
    if (GST_PAD_CAPS (sel->srcpad) != caps)
      gst_pad_set_caps (sel->srcpad, caps);
  }

  res = gst_pad_push (sel->srcpad, gst_buffer_ref (buf));
  GST_LOG_OBJECT (pad, "Buffer %p forwarded result=%d", buf, res);

  GST_INPUT_SELECTOR_LOCK (sel);

  if (sel->sync_streams && sel->cache_buffers) {
    /* Might have changed while pushing */
    active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);
    /* only set pad to pushed if we are still the active pad */
    if (active_sinkpad == pad)
      selpad->pushed = TRUE;

    /* cache buffer as we may need it again if we change pads */
    gst_selector_pad_cache_buffer (selpad, buf);
    gst_input_selector_cleanup_old_cached_buffers (sel, pad);
  } else {
    selpad->pushed = TRUE;
    gst_buffer_unref (buf);
  }
  GST_INPUT_SELECTOR_UNLOCK (sel);

done:
  gst_object_unref (sel);
  return res;

  /* dropped buffers */
ignore:
  {
    gboolean active_pad_pushed = GST_SELECTOR_PAD_CAST (active_sinkpad)->pushed;

    GST_DEBUG_OBJECT (pad, "Pad not active, discard buffer %p", buf);
    /* when we drop a buffer, we're creating a discont on this pad */
    selpad->discont = TRUE;
    GST_INPUT_SELECTOR_UNLOCK (sel);
    gst_buffer_unref (buf);

    /* figure out what to return upstream */
    GST_OBJECT_LOCK (selpad);
    if (selpad->always_ok || !active_pad_pushed)
      res = GST_FLOW_OK;
    else
      res = GST_FLOW_NOT_LINKED;
    GST_OBJECT_UNLOCK (selpad);

    goto done;
  }
flushing:
  {
    GST_DEBUG_OBJECT (pad, "We are flushing, discard buffer %p", buf);
    gst_buffer_unref (buf);
    res = GST_FLOW_WRONG_STATE;
    goto done;
  }
}

static void gst_input_selector_dispose (GObject * object);

static void gst_input_selector_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_input_selector_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_input_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static void gst_input_selector_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_input_selector_change_state (GstElement *
    element, GstStateChange transition);

static GstCaps *gst_input_selector_getcaps (GstPad * pad);
static gboolean gst_input_selector_event (GstPad * pad, GstEvent * event);
static gboolean gst_input_selector_query (GstPad * pad, GstQuery * query);
static gint64 gst_input_selector_block (GstInputSelector * self);
static void gst_input_selector_switch (GstInputSelector * self,
    GstPad * pad, gint64 stop_time, gint64 start_time);

/* FIXME: create these marshallers using glib-genmarshal */
#define g_marshal_value_peek_object(v)   (v)->data[0].v_pointer
#define g_marshal_value_peek_int64(v)    (v)->data[0].v_int64

static void
gst_input_selector_marshal_INT64__VOID (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef gint64 (*GMarshalFunc_INT64__VOID) (gpointer data1, gpointer data2);
  register GMarshalFunc_INT64__VOID callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  gint64 v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 1);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_INT64__VOID) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1, data2);

  g_value_set_int64 (return_value, v_return);
}

static void
gst_input_selector_marshal_VOID__OBJECT_INT64_INT64 (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_INT64_INT64) (gpointer data1,
      gpointer arg_1, gint64 arg_2, gint64 arg_3, gpointer data2);
  register GMarshalFunc_VOID__OBJECT_INT64_INT64 callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 4);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_VOID__OBJECT_INT64_INT64) (marshal_data ? marshal_data :
      cc->callback);

  callback (data1,
      g_marshal_value_peek_object (param_values + 1),
      g_marshal_value_peek_int64 (param_values + 2),
      g_marshal_value_peek_int64 (param_values + 3), data2);
}

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (input_selector_debug, \
        "input-selector", 0, "An input stream selector element");

GST_BOILERPLATE_FULL (GstInputSelector, gst_input_selector, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_input_selector_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Input selector",
      "Generic", "N-to-1 input stream selector",
      "Julien Moutte <julien@moutte.net>, "
      "Jan Schmidt <thaytan@mad.scientist.com>, "
      "Wim Taymans <wim.taymans@gmail.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_input_selector_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_input_selector_src_factory));
}

static void
gst_input_selector_class_init (GstInputSelectorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = gst_input_selector_dispose;

  gobject_class->set_property = gst_input_selector_set_property;
  gobject_class->get_property = gst_input_selector_get_property;

  g_object_class_install_property (gobject_class, PROP_N_PADS,
      g_param_spec_uint ("n-pads", "Number of Pads",
          "The number of sink pads", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ACTIVE_PAD,
      g_param_spec_object ("active-pad", "Active pad",
          "The currently active sink pad", GST_TYPE_PAD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstInputSelector:sync-streams
   *
   * If set to %TRUE all inactive streams will be synced to the
   * running time of the active stream or to the current clock.
   *
   * To make sure no buffers are dropped by input-selector
   * that might be needed when switching the active pad,
   * sync-mode should be set to "clock" and cache-buffers to TRUE.
   *
   * Since: 0.10.36
   */
  g_object_class_install_property (gobject_class, PROP_SYNC_STREAMS,
      g_param_spec_boolean ("sync-streams", "Sync Streams",
          "Synchronize inactive streams to the running time of the active "
          "stream or to the current clock",
          DEFAULT_SYNC_STREAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /**
   * GstInputSelector:sync-mode
   *
   * Select how input-selector will sync buffers when in sync-streams mode.
   *
   * Note that when using the "active-segment" mode, the "active-segment" may
   * be ahead of current clock time when switching the active pad, as the current
   * active pad may have pushed more buffers than what was displayed/consumed,
   * which may cause delays and some missing buffers.
   *
   * Since: 0.10.36
   */
  g_object_class_install_property (gobject_class, PROP_SYNC_MODE,
      g_param_spec_enum ("sync-mode", "Sync mode",
          "Behavior in sync-streams mode", GST_TYPE_INPUT_SELECTOR_SYNC_MODE,
          DEFAULT_SYNC_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /**
   * GstInputSelector:cache-buffers
   *
   * If set to %TRUE and GstInputSelector:sync-streams is also set to %TRUE,
   * the active pad will cache the buffers still considered valid (after current
   * running time, see sync-mode) to avoid missing frames if/when the pad is
   * reactivated.
   *
   * The active pad may push more buffers than what is currently displayed/consumed
   * and when changing pads those buffers will be discarded and the only way to
   * reactivate that pad without loosing the already consumed buffers is to enable cache.
   */
  g_object_class_install_property (gobject_class, PROP_CACHE_BUFFERS,
      g_param_spec_boolean ("cache-buffers", "Cache Buffers",
          "Cache buffers for active-pad",
          DEFAULT_CACHE_BUFFERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /**
   * GstInputSelector::block:
   * @inputselector: the #GstInputSelector
   *
   * Block all sink pads in preparation for a switch. Returns the stop time of
   * the current switch segment, as a running time, or 0 if there is no current
   * active pad or the current active pad never received data.
   */
  gst_input_selector_signals[SIGNAL_BLOCK] =
      g_signal_new ("block", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstInputSelectorClass, block), NULL, NULL,
      gst_input_selector_marshal_INT64__VOID, G_TYPE_INT64, 0);
  /**
   * GstInputSelector::switch:
   * @inputselector: the #GstInputSelector
   * @pad:            the pad to switch to
   * @stop_time:      running time at which to close the previous segment, or -1
   *                  to use the running time of the previously active sink pad
   * @start_time:     running time at which to start the new segment, or -1 to
   *                  use the running time of the newly active sink pad
   *
   * Switch to a new feed. The segment opened by the previously active pad, if
   * any, will be closed, and a new segment opened before data flows again.
   *
   * This signal must be emitted when the element has been blocked via the <link
   * linkend="GstInputSelector-block">block</link> signal.
   *
   * If you have a stream with only one switch element, such as an audio-only
   * stream, a stream switch should be performed by first emitting the block
   * signal, and then emitting the switch signal with -1 for the stop and start
   * time values.
   *
   * The intention of the @stop_time and @start_time arguments is to allow
   * multiple switch elements to switch and maintain stream synchronization.
   * When switching a stream with multiple feeds, you will need as many switch
   * elements as you have feeds. For example, a feed with audio and video will
   * have one switch element between the audio feeds and one for video.
   *
   * A switch over multiple switch elements should be performed as follows:
   * First, emit the <link linkend="GstInputSelector-block">block</link>
   * signal, collecting the returned values. The maximum running time returned
   * by block should then be used as the time at which to close the previous
   * segment.
   *
   * Then, query the running times of the new audio and video pads that you will
   * switch to. Naturally, these pads are on separate switch elements. Take the
   * minimum running time for those streams and use it for the time at which to
   * open the new segment.
   *
   * If @pad is the same as the current active pad, the element will cancel any
   * previous block without adjusting segments.
   *
   * <note><simpara>
   * the signal changed from accepting the pad name to the pad object.
   * </simpara></note>
   *
   * Since: 0.10.7
   */
  gst_input_selector_signals[SIGNAL_SWITCH] =
      g_signal_new ("switch", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstInputSelectorClass, switch_),
      NULL, NULL, gst_input_selector_marshal_VOID__OBJECT_INT64_INT64,
      G_TYPE_NONE, 3, GST_TYPE_PAD, G_TYPE_INT64, G_TYPE_INT64);

  gstelement_class->request_new_pad = gst_input_selector_request_new_pad;
  gstelement_class->release_pad = gst_input_selector_release_pad;
  gstelement_class->change_state = gst_input_selector_change_state;

  klass->block = GST_DEBUG_FUNCPTR (gst_input_selector_block);
  /* note the underscore because switch is a keyword otherwise */
  klass->switch_ = GST_DEBUG_FUNCPTR (gst_input_selector_switch);
}

static void
gst_input_selector_init (GstInputSelector * sel,
    GstInputSelectorClass * g_class)
{
  sel->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_iterate_internal_links_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_iterate_linked_pads));
  gst_pad_set_getcaps_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_input_selector_getcaps));
  gst_pad_set_query_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_input_selector_query));
  gst_pad_set_event_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_input_selector_event));
  gst_element_add_pad (GST_ELEMENT (sel), sel->srcpad);
  /* sinkpad management */
  sel->active_sinkpad = NULL;
  sel->padcount = 0;
  gst_segment_init (&sel->segment, GST_FORMAT_UNDEFINED);
  sel->sync_streams = DEFAULT_SYNC_STREAMS;

  sel->lock = g_mutex_new ();
  sel->cond = g_cond_new ();
  sel->blocked = FALSE;

  /* lets give a change for downstream to do something on
   * active-pad change before we start pushing new buffers */
  g_signal_connect_data (sel, "notify::active-pad",
      (GCallback) gst_input_selector_active_pad_changed, NULL,
      NULL, G_CONNECT_AFTER);
}

static void
gst_input_selector_dispose (GObject * object)
{
  GstInputSelector *sel = GST_INPUT_SELECTOR (object);

  if (sel->active_sinkpad) {
    gst_object_unref (sel->active_sinkpad);
    sel->active_sinkpad = NULL;
  }
  if (sel->lock) {
    g_mutex_free (sel->lock);
    sel->lock = NULL;
  }
  if (sel->cond) {
    g_cond_free (sel->cond);
    sel->cond = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/* Solve the following equation for B.timestamp, and set that as the segment
 * stop:
 * B.running_time = (B.timestamp - NS.start) / NS.abs_rate + NS.accum
 */
static gint64
gst_segment_get_timestamp (GstSegment * segment, gint64 running_time)
{
  if (running_time <= segment->accum)
    return segment->start;
  else
    return (running_time - segment->accum) * segment->abs_rate + segment->start;
}

static void
gst_segment_set_stop (GstSegment * segment, gint64 running_time)
{
  segment->stop = gst_segment_get_timestamp (segment, running_time);
  segment->last_stop = -1;
}

static void
gst_segment_set_start (GstSegment * segment, gint64 running_time)
{
  gint64 new_start, duration;

  new_start = gst_segment_get_timestamp (segment, running_time);

  /* this is the duration we skipped */
  duration = new_start - segment->start;
  /* add the duration to the accumulated segment time */
  segment->accum += duration;
  /* move position in the segment */
  segment->time += duration;
  segment->start += duration;
}

/* this function must be called with the SELECTOR_LOCK. It returns TRUE when the
 * active pad changed. */
static gboolean
gst_input_selector_set_active_pad (GstInputSelector * self,
    GstPad * pad, gint64 stop_time, gint64 start_time)
{
  GstSelectorPad *old, *new;
  GstPad **active_pad_p;

  if (pad == self->active_sinkpad)
    return FALSE;

  old = GST_SELECTOR_PAD_CAST (self->active_sinkpad);
  new = GST_SELECTOR_PAD_CAST (pad);

  GST_DEBUG_OBJECT (self, "setting active pad to %s:%s",
      GST_DEBUG_PAD_NAME (new));

  if (!GST_CLOCK_TIME_IS_VALID (stop_time) && old) {
    /* no stop time given, get the latest running_time on the active pad to 
     * close and open the new segment */
    stop_time = start_time = gst_selector_pad_get_running_time (old);
    GST_DEBUG_OBJECT (self, "using start/stop of %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start_time));
  }

  if (old && old->active && !self->pending_close && stop_time >= 0) {
    /* schedule a last_stop update if one isn't already scheduled, and a
       segment has been pushed before. */
    self->segment = old->segment;

    GST_DEBUG_OBJECT (self, "setting stop_time to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (stop_time));
    gst_segment_set_stop (&self->segment, stop_time);
    self->pending_close = TRUE;
  }
  if (old)
    old->pushed = FALSE;

  if (new && new->active && start_time >= 0) {
    /* schedule a new segment push */
    if (self->sync_streams && self->cache_buffers)
      start_time = gst_selector_pad_get_running_time (new);
    GST_DEBUG_OBJECT (self, "setting start_time to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start_time));
    /* schedule a new segment push */
    gst_segment_set_start (&new->segment, start_time);
    new->segment_pending = TRUE;
  }
  if (new)
    new->pushed = FALSE;

  active_pad_p = &self->active_sinkpad;
  gst_object_replace ((GstObject **) active_pad_p, GST_OBJECT_CAST (pad));

  GST_DEBUG_OBJECT (self, "New active pad is %" GST_PTR_FORMAT,
      self->active_sinkpad);

  return TRUE;
}

static void
gst_input_selector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInputSelector *sel = GST_INPUT_SELECTOR (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:
    {
      GstPad *pad;

      pad = g_value_get_object (value);

      GST_INPUT_SELECTOR_LOCK (sel);

#if DEBUG_CACHED_BUFFERS
      gst_input_selector_debug_cached_buffers (sel);
#endif

      gst_input_selector_set_active_pad (sel, pad,
          GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE);

#if DEBUG_CACHED_BUFFERS
      gst_input_selector_debug_cached_buffers (sel);
#endif

      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    }
    case PROP_SYNC_STREAMS:
      GST_INPUT_SELECTOR_LOCK (sel);
      sel->sync_streams = g_value_get_boolean (value);
      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    case PROP_SYNC_MODE:
      GST_INPUT_SELECTOR_LOCK (sel);
      sel->sync_mode = g_value_get_enum (value);
      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    case PROP_CACHE_BUFFERS:
      GST_INPUT_SELECTOR_LOCK (object);
      sel->cache_buffers = g_value_get_boolean (value);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_input_selector_active_pad_changed (GstInputSelector * sel,
    GParamSpec * pspec, gpointer user_data)
{
  /* Wake up all non-active pads in sync mode, they might be
   * the active pad now */
  if (sel->sync_streams)
    GST_INPUT_SELECTOR_BROADCAST (sel);
}

static void
gst_input_selector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstInputSelector *sel = GST_INPUT_SELECTOR (object);

  switch (prop_id) {
    case PROP_N_PADS:
      GST_INPUT_SELECTOR_LOCK (object);
      g_value_set_uint (value, sel->n_pads);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    case PROP_ACTIVE_PAD:
      GST_INPUT_SELECTOR_LOCK (object);
      g_value_set_object (value, sel->active_sinkpad);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    case PROP_SYNC_STREAMS:
      GST_INPUT_SELECTOR_LOCK (object);
      g_value_set_boolean (value, sel->sync_streams);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    case PROP_SYNC_MODE:
      GST_INPUT_SELECTOR_LOCK (object);
      g_value_set_enum (value, sel->sync_mode);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    case PROP_CACHE_BUFFERS:
      GST_INPUT_SELECTOR_LOCK (object);
      g_value_set_boolean (value, sel->cache_buffers);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_input_selector_get_linked_pad (GstInputSelector * sel, GstPad * pad,
    gboolean strict)
{
  GstPad *otherpad = NULL;

  GST_INPUT_SELECTOR_LOCK (sel);
  if (pad == sel->srcpad)
    otherpad = sel->active_sinkpad;
  else if (pad == sel->active_sinkpad || !strict)
    otherpad = sel->srcpad;
  if (otherpad)
    gst_object_ref (otherpad);
  GST_INPUT_SELECTOR_UNLOCK (sel);

  return otherpad;
}

static gboolean
gst_input_selector_event (GstPad * pad, GstEvent * event)
{
  GstInputSelector *sel;
  gboolean res = FALSE;
  GstPad *otherpad;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }

  otherpad = gst_input_selector_get_linked_pad (sel, pad, TRUE);
  if (otherpad) {
    res = gst_pad_push_event (otherpad, event);

    gst_object_unref (otherpad);
  } else
    gst_event_unref (event);

  gst_object_unref (sel);
  return res;
}

/* query on the srcpad. We override this function because by default it will
 * only forward the query to one random sinkpad */
static gboolean
gst_input_selector_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstInputSelector *sel;
  GstPad *otherpad;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return FALSE;

  otherpad = gst_input_selector_get_linked_pad (sel, pad, TRUE);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GList *walk;
      GstClockTime resmin, resmax;
      gboolean reslive;

      resmin = 0;
      resmax = -1;
      reslive = FALSE;

      /* assume FALSE, we become TRUE if one query succeeds */
      res = FALSE;

      /* perform the query on all sinkpads and combine the results. We take the
       * max of min and the min of max for the result latency. */
      GST_INPUT_SELECTOR_LOCK (sel);
      for (walk = GST_ELEMENT_CAST (sel)->sinkpads; walk;
          walk = g_list_next (walk)) {
        GstPad *sinkpad = GST_PAD_CAST (walk->data);

        if (gst_pad_peer_query (sinkpad, query)) {
          GstClockTime min, max;
          gboolean live;

          /* one query succeeded, we succeed too */
          res = TRUE;

          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG_OBJECT (sinkpad,
              "peer latency min %" GST_TIME_FORMAT ", max %" GST_TIME_FORMAT
              ", live %d", GST_TIME_ARGS (min), GST_TIME_ARGS (max), live);

          if (live) {
            if (min > resmin)
              resmin = min;
            if (resmax == -1)
              resmax = max;
            else if (max < resmax)
              resmax = max;
            if (reslive == FALSE)
              reslive = live;
          }
        }
      }
      GST_INPUT_SELECTOR_UNLOCK (sel);
      if (res) {
        gst_query_set_latency (query, reslive, resmin, resmax);

        GST_DEBUG_OBJECT (sel,
            "total latency min %" GST_TIME_FORMAT ", max %" GST_TIME_FORMAT
            ", live %d", GST_TIME_ARGS (resmin), GST_TIME_ARGS (resmax),
            reslive);
      }

      break;
    }
    default:
      if (otherpad)
        res = gst_pad_peer_query (otherpad, query);
      break;
  }
  if (otherpad)
    gst_object_unref (otherpad);
  gst_object_unref (sel);

  return res;
}

static GstCaps *
gst_input_selector_getcaps (GstPad * pad)
{
  GstPad *otherpad;
  GstInputSelector *sel;
  GstCaps *caps;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return gst_caps_new_any ();

  otherpad = gst_input_selector_get_linked_pad (sel, pad, FALSE);

  if (!otherpad) {
    GST_DEBUG_OBJECT (pad, "Pad not linked, returning ANY");
    caps = gst_caps_new_any ();
  } else {
    GST_DEBUG_OBJECT (pad, "Pad is linked (to %s:%s), returning peer caps",
        GST_DEBUG_PAD_NAME (otherpad));
    /* if the peer has caps, use those. If the pad is not linked, this function
     * returns NULL and we return ANY */
    if (!(caps = gst_pad_peer_get_caps_reffed (otherpad)))
      caps = gst_caps_new_any ();
    gst_object_unref (otherpad);
  }

  gst_object_unref (sel);
  return caps;
}

/* check if the pad is the active sinkpad */
static inline gboolean
gst_input_selector_is_active_sinkpad (GstInputSelector * sel, GstPad * pad)
{
  gboolean res;

  GST_INPUT_SELECTOR_LOCK (sel);
  res = (pad == sel->active_sinkpad);
  GST_INPUT_SELECTOR_UNLOCK (sel);

  return res;
}

/* Get or create the active sinkpad, must be called with SELECTOR_LOCK */
static GstPad *
gst_input_selector_activate_sinkpad (GstInputSelector * sel, GstPad * pad)
{
  GstPad *active_sinkpad;
  GstSelectorPad *selpad;

  selpad = GST_SELECTOR_PAD_CAST (pad);

  selpad->active = TRUE;
  active_sinkpad = sel->active_sinkpad;
  if (active_sinkpad == NULL) {
    /* first pad we get activity on becomes the activated pad by default */
    if (sel->active_sinkpad)
      gst_object_unref (sel->active_sinkpad);
    active_sinkpad = sel->active_sinkpad = gst_object_ref (pad);
    GST_DEBUG_OBJECT (sel, "Activating pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  }

  return active_sinkpad;
}

static GstPad *
gst_input_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused)
{
  GstInputSelector *sel;
  gchar *name = NULL;
  GstPad *sinkpad = NULL;

  g_return_val_if_fail (templ->direction == GST_PAD_SINK, NULL);

  sel = GST_INPUT_SELECTOR (element);

  GST_INPUT_SELECTOR_LOCK (sel);

  GST_LOG_OBJECT (sel, "Creating new pad %d", sel->padcount);
  name = g_strdup_printf ("sink%d", sel->padcount++);
  sinkpad = g_object_new (GST_TYPE_SELECTOR_PAD,
      "name", name, "direction", templ->direction, "template", templ, NULL);
  g_free (name);

  sel->n_pads++;

  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_event));
  gst_pad_set_getcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_getcaps));
  gst_pad_set_acceptcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_acceptcaps));
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_chain));
  gst_pad_set_iterate_internal_links_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_iterate_linked_pads));
  gst_pad_set_bufferalloc_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_bufferalloc));

  gst_pad_set_active (sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (sel), sinkpad);
  GST_INPUT_SELECTOR_UNLOCK (sel);

  return sinkpad;
}

static void
gst_input_selector_release_pad (GstElement * element, GstPad * pad)
{
  GstInputSelector *sel;

  sel = GST_INPUT_SELECTOR (element);
  GST_LOG_OBJECT (sel, "Releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  GST_INPUT_SELECTOR_LOCK (sel);
  /* if the pad was the active pad, makes us select a new one */
  if (sel->active_sinkpad == pad) {
    GST_DEBUG_OBJECT (sel, "Deactivating pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    gst_object_unref (sel->active_sinkpad);
    sel->active_sinkpad = NULL;
  }
  sel->n_pads--;

  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (GST_ELEMENT (sel), pad);
  GST_INPUT_SELECTOR_UNLOCK (sel);
}

static void
gst_input_selector_reset (GstInputSelector * sel)
{
  GList *walk;

  GST_INPUT_SELECTOR_LOCK (sel);
  /* clear active pad */
  if (sel->active_sinkpad) {
    gst_object_unref (sel->active_sinkpad);
    sel->active_sinkpad = NULL;
  }
  /* reset segment */
  gst_segment_init (&sel->segment, GST_FORMAT_UNDEFINED);
  sel->pending_close = FALSE;
  /* reset each of our sinkpads state */
  for (walk = GST_ELEMENT_CAST (sel)->sinkpads; walk; walk = g_list_next (walk)) {
    GstSelectorPad *selpad = GST_SELECTOR_PAD_CAST (walk->data);

    gst_selector_pad_reset (selpad);

    if (selpad->tags) {
      gst_tag_list_free (selpad->tags);
      selpad->tags = NULL;
    }
  }
  GST_INPUT_SELECTOR_UNLOCK (sel);
}

static GstStateChangeReturn
gst_input_selector_change_state (GstElement * element,
    GstStateChange transition)
{
  GstInputSelector *self = GST_INPUT_SELECTOR (element);
  GstStateChangeReturn result;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_INPUT_SELECTOR_LOCK (self);
      self->blocked = FALSE;
      self->flushing = FALSE;
      GST_INPUT_SELECTOR_UNLOCK (self);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* first unlock before we call the parent state change function, which
       * tries to acquire the stream lock when going to ready. */
      GST_INPUT_SELECTOR_LOCK (self);
      self->blocked = FALSE;
      self->flushing = TRUE;
      GST_INPUT_SELECTOR_BROADCAST (self);
      GST_INPUT_SELECTOR_UNLOCK (self);
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_input_selector_reset (self);
      break;
    default:
      break;
  }

  return result;
}

static gint64
gst_input_selector_block (GstInputSelector * self)
{
  gint64 ret = 0;
  GstSelectorPad *spad;

  GST_INPUT_SELECTOR_LOCK (self);

  if (self->blocked)
    GST_WARNING_OBJECT (self, "switch already blocked");

  self->blocked = TRUE;
  spad = GST_SELECTOR_PAD_CAST (self->active_sinkpad);

  if (spad)
    ret = gst_selector_pad_get_running_time (spad);
  else
    GST_DEBUG_OBJECT (self, "no active pad while blocking");

  GST_INPUT_SELECTOR_UNLOCK (self);

  return ret;
}

/* stop_time and start_time are running times */
static void
gst_input_selector_switch (GstInputSelector * self, GstPad * pad,
    gint64 stop_time, gint64 start_time)
{
  gboolean changed;

  g_return_if_fail (self->blocked == TRUE);

  GST_INPUT_SELECTOR_LOCK (self);
  changed =
      gst_input_selector_set_active_pad (self, pad, stop_time, start_time);

  self->blocked = FALSE;
  GST_INPUT_SELECTOR_BROADCAST (self);
  GST_INPUT_SELECTOR_UNLOCK (self);

  if (changed) {
    NOTIFY_MUTEX_LOCK ();
    g_object_notify (G_OBJECT (self), "active-pad");
    NOTIFY_MUTEX_UNLOCK ();
  }
}
