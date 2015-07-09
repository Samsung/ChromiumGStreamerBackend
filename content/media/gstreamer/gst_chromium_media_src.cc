// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/media/gstreamer/gst_chromium_media_src.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/pbutils/missing-plugins.h>

#include <algorithm>

// This element has been inspired from WebKitMediaSource
// TODO: use multiappsrc element which is not upstream
// at the moment, see https://bugzilla.gnome.org/show_bug.cgi?id=725187

typedef struct {
  const gchar* gst;
  const gchar* mse;
} CodecInfo;

// TODO: make this generic, maybe improve gst_pb_utils.
static const CodecInfo codecs_map[] = {
    {"MPEG-4 AAC", "mp4a"},
    {"On2 VP9", "vp9"},
};

typedef struct _Source Source;
struct _Source {
  GstElement* src;
  GstPad* pad;
  // Just for identification
  std::string id;
  std::string media_type;
  std::string codec;
  bool initialized;
};

struct _ChromiumMediaSrcPrivate {
  GList* sources;
  gchar* location;
  GstClockTime duration;
  ;
  bool asyncStart;
  bool noMorePads;
};

enum { Prop0, PropLocation };

static GstStaticPadTemplate srcTemplate =
    GST_STATIC_PAD_TEMPLATE("src_%u",
                            GST_PAD_SRC,
                            GST_PAD_SOMETIMES,
                            GST_STATIC_CAPS_ANY);

#define CHROMIUM_MEDIA_SRC_GET_PRIVATE(obj)                    \
  (G_TYPE_INSTANCE_GET_PRIVATE((obj), CHROMIUM_TYPE_MEDIA_SRC, \
                               ChromiumMediaSrcPrivate))

GST_DEBUG_CATEGORY_STATIC(chromium_media_src_debug);
#define GST_CAT_DEFAULT chromium_media_src_debug

static void chromiumMediaSrcUriHandlerInit(gpointer gIface, gpointer ifaceData);
static void chromiumMediaSrcFinalize(GObject*);
static void chromiumMediaSrcSetProperty(GObject*,
                                        guint propertyId,
                                        const GValue*,
                                        GParamSpec*);
static void chromiumMediaSrcGetProperty(GObject*,
                                        guint propertyId,
                                        GValue*,
                                        GParamSpec*);
static GstStateChangeReturn chromiumMediaSrcChangeState(GstElement*,
                                                        GstStateChange);
static gboolean chromiumMediaSrcQueryWithParent(GstPad*, GstObject*, GstQuery*);

#define chromium_media_src_parent_class parent_class

#define CHROMIUM_MEDIA_SRC_CATEGORY_INIT                                   \
  GST_DEBUG_CATEGORY_INIT(chromium_media_src_debug, "chromiummediasrc", 0, \
                          "media src element");
G_DEFINE_TYPE_WITH_CODE(ChromiumMediaSrc,
                        chromium_media_src,
                        GST_TYPE_BIN,
                        G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER,
                                              chromiumMediaSrcUriHandlerInit);
                        CHROMIUM_MEDIA_SRC_CATEGORY_INIT);

static void chromium_media_src_class_init(ChromiumMediaSrcClass* klass) {
  GObjectClass* oklass = G_OBJECT_CLASS(klass);
  GstElementClass* eklass = GST_ELEMENT_CLASS(klass);

  oklass->finalize = chromiumMediaSrcFinalize;
  oklass->set_property = chromiumMediaSrcSetProperty;
  oklass->get_property = chromiumMediaSrcGetProperty;

  gst_element_class_add_pad_template(eklass,
                                     gst_static_pad_template_get(&srcTemplate));

  gst_element_class_set_static_metadata(
      eklass, "Chromium Media source element", "Source", "Handles Blob uris",
      "Stephane Jadaud <sjadaud@sii.fr>, Sebastian Dröge "
      "<sebastian@centricular.com>");

  /* Allows setting the uri using the 'location' property, which is used
   * for example by gst_element_make_from_uri() */
  g_object_class_install_property(
      oklass, PropLocation,
      g_param_spec_string(
          "location", "location", "Location to read from", 0,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  eklass->change_state = chromiumMediaSrcChangeState;

  g_type_class_add_private(klass, sizeof(ChromiumMediaSrcPrivate));
}

static void chromium_media_src_init(ChromiumMediaSrc* src) {
  src->priv = CHROMIUM_MEDIA_SRC_GET_PRIVATE(src);

  src->priv->sources = NULL;
  src->priv->location = NULL;
}

static void destroy_source(gpointer data, gpointer user_data) {
  // TODO: dynamically remove source
  // ChromiumMediaSrc* media_source = CHROMIUM_MEDIA_SRC(user_data);
  Source* source = (Source*)data;
  if (GST_IS_APP_SRC(source->src)) {
    /*gst_app_src_end_of_stream(GST_APP_SRC(source->src));
    gst_pad_set_active(source->pad, FALSE);
    gst_remove_add_pad(GST_ELEMENT(media_source), source->pad);
    gst_object_unref(source->pad);
    gst_element_set_state(GST_ELEMENT(source->src), GST_STATE_NULL);
    gst_bin_remove(GST_BIN(media_source), source->src);*/
    gst_object_unref(source->pad);
    source->pad = NULL;
    gst_object_unref(source->src);
    source->src = NULL;
  }
  delete source;
}

static void chromiumMediaSrcFinalize(GObject* object) {
  ChromiumMediaSrc* src = CHROMIUM_MEDIA_SRC(object);
  ChromiumMediaSrcPrivate* priv = src->priv;

  if (priv->sources) {
    g_list_foreach(priv->sources, (GFunc)destroy_source, src);
    g_list_free(priv->sources);
    priv->sources = NULL;
  }

  g_free(priv->location);

  GST_CALL_PARENT(G_OBJECT_CLASS, finalize, (object));
}

static void chromiumMediaSrcSetProperty(GObject* object,
                                        guint propId,
                                        const GValue* value,
                                        GParamSpec* pspec) {
  ChromiumMediaSrc* src = CHROMIUM_MEDIA_SRC(object);

  switch (propId) {
    case PropLocation:
      gst_uri_handler_set_uri(reinterpret_cast<GstURIHandler*>(src),
                              g_value_get_string(value), 0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
      break;
  }
}

static void chromiumMediaSrcGetProperty(GObject* object,
                                        guint propId,
                                        GValue* value,
                                        GParamSpec* pspec) {
  ChromiumMediaSrc* src = CHROMIUM_MEDIA_SRC(object);
  ChromiumMediaSrcPrivate* priv = src->priv;

  GST_OBJECT_LOCK(src);
  switch (propId) {
    case PropLocation:
      g_value_set_string(value, priv->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
      break;
  }
  GST_OBJECT_UNLOCK(src);
}

static void chromiumMediaSrcDoAsyncStart(ChromiumMediaSrc* src) {
  ChromiumMediaSrcPrivate* priv = src->priv;
  priv->asyncStart = true;
  GST_BIN_CLASS(parent_class)
      ->handle_message(GST_BIN(src),
                       gst_message_new_async_start(GST_OBJECT(src)));
}

static void chromiumMediaSrcDoAsyncDone(ChromiumMediaSrc* src) {
  ChromiumMediaSrcPrivate* priv = src->priv;
  if (priv->asyncStart) {
    GST_BIN_CLASS(parent_class)
        ->handle_message(
            GST_BIN(src),
            gst_message_new_async_done(GST_OBJECT(src), GST_CLOCK_TIME_NONE));
    priv->asyncStart = false;
  }
}

static GstStateChangeReturn chromiumMediaSrcChangeState(
    GstElement* element,
    GstStateChange transition) {
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  ChromiumMediaSrc* src = CHROMIUM_MEDIA_SRC(element);
  ChromiumMediaSrcPrivate* priv = src->priv;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      priv->noMorePads = false;
      chromiumMediaSrcDoAsyncStart(src);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
  if (G_UNLIKELY(ret == GST_STATE_CHANGE_FAILURE)) {
    GST_DEBUG_OBJECT(src, "State change failed");
    chromiumMediaSrcDoAsyncDone(src);
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ret = GST_STATE_CHANGE_ASYNC;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      chromiumMediaSrcDoAsyncDone(src);
      priv->noMorePads = false;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean chromiumMediaSrcQueryWithParent(GstPad* pad,
                                                GstObject* parent,
                                                GstQuery* query) {
  ChromiumMediaSrc* src = CHROMIUM_MEDIA_SRC(GST_ELEMENT(parent));
  gboolean result = FALSE;

  switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_DURATION: {
      GstFormat format;
      gst_query_parse_duration(query, &format, NULL);

      GST_DEBUG_OBJECT(src, "duration query in format %s",
                       gst_format_get_name(format));
      GST_OBJECT_LOCK(src);
      if ((format == GST_FORMAT_TIME) && (src->priv->duration > 0)) {
        gst_query_set_duration(query, format, src->priv->duration);
        result = TRUE;
      }
      GST_OBJECT_UNLOCK(src);
      break;
    }
    case GST_QUERY_URI: {
      GST_OBJECT_LOCK(src);
      gst_query_set_uri(query, src->priv->location);
      GST_OBJECT_UNLOCK(src);
      result = TRUE;
      break;
    }
    default: {
      GstPad* target = gst_ghost_pad_get_target(GST_GHOST_PAD_CAST(pad));
      // Forward the query to the proxy target pad.
      if (target) {
        result = gst_pad_query(target, query);
        gst_object_unref(target);
      }
      break;
    }
  }

  return result;
}

// uri handler interface
static GstURIType chromiumMediaSrcUriGetType(GType) {
  return GST_URI_SRC;
}

const gchar* const* chromiumMediaSrcGetProtocols(GType) {
  static const char* protocols[] = {"mediasourceblob", 0};
  return protocols;
}

static gchar* chromiumMediaSrcGetUri(GstURIHandler* handler) {
  ChromiumMediaSrc* src = CHROMIUM_MEDIA_SRC(handler);
  gchar* ret;

  GST_OBJECT_LOCK(src);
  ret = g_strdup(src->priv->location);
  GST_OBJECT_UNLOCK(src);
  return ret;
}

static gboolean chromiumMediaSrcSetUri(GstURIHandler* handler,
                                       const gchar* uri,
                                       GError**) {
  ChromiumMediaSrc* src = CHROMIUM_MEDIA_SRC(handler);
  ChromiumMediaSrcPrivate* priv = src->priv;
  GError* error = NULL;

  if (GST_STATE(src) >= GST_STATE_PAUSED) {
    GST_ERROR_OBJECT(src, "URI can only be set in states < PAUSED");
    return FALSE;
  }

  GST_OBJECT_LOCK(src);
  g_free(priv->location);
  priv->location = 0;
  if (!uri) {
    GST_OBJECT_UNLOCK(src);
    return TRUE;
  }

  GURL url(uri);
  if (!url.SchemeIs("mediasourceblob")) {
    g_set_error(&error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
                "Invalid URI '%s'", uri);
    return FALSE;
  }

  priv->location = g_strdup(url.spec().c_str());
  GST_OBJECT_UNLOCK(src);
  return TRUE;
}
static void chromiumMediaSrcUriHandlerInit(gpointer gIface, gpointer) {
  GstURIHandlerInterface* iface = (GstURIHandlerInterface*)gIface;

  iface->get_type = chromiumMediaSrcUriGetType;
  iface->get_protocols = chromiumMediaSrcGetProtocols;
  iface->get_uri = chromiumMediaSrcGetUri;
  iface->set_uri = chromiumMediaSrcSetUri;
}

blink::WebMediaSource::AddStatus chromiumMediaSrcAddSourceBufferId(
    ChromiumMediaSrc* src,
    const std::string& source_id,
    const std::string& media_type,
    const std::vector<std::string>& codecs) {
  ChromiumMediaSrcPrivate* priv = src->priv;

  if (priv->noMorePads) {
    GST_ERROR_OBJECT(
        src, "Adding new source buffers after first data not supported yet");
    return blink::WebMediaSource::AddStatusNotSupported;
  }

  GST_DEBUG_OBJECT(src, "State %d", static_cast<int>(GST_STATE(src)));

  GST_OBJECT_LOCK(src);

  Source* source = new Source;
  guint numberOfSources = g_list_length(priv->sources);
  gchar* src_name = g_strdup_printf("src%u", numberOfSources);
  source->src = gst_element_factory_make("appsrc", src_name);
  g_free(src_name);

  source->id = source_id;
  source->media_type = media_type;
  if (codecs.size() > 0)
    source->codec = codecs[0];
  source->initialized = false;

  gchar* pad_name = g_strdup_printf("src_%u", numberOfSources);
  priv->sources = g_list_prepend(priv->sources, source);

  GST_OBJECT_UNLOCK(src);

  gst_bin_add(GST_BIN(src), GST_ELEMENT(gst_object_ref(source->src)));
  GstPad* pad = gst_element_get_static_pad(source->src, "src");
  GstPad* ghostPad = gst_ghost_pad_new_from_template(
      pad_name, pad, gst_static_pad_template_get(&srcTemplate));
  g_free(pad_name);
  gst_pad_set_query_function(ghostPad, chromiumMediaSrcQueryWithParent);
  gst_pad_set_active(ghostPad, TRUE);
  gst_element_add_pad(GST_ELEMENT(src), GST_PAD(gst_object_ref(ghostPad)));

  source->pad = ghostPad;

  gst_element_sync_state_with_parent(source->src);

  return blink::WebMediaSource::AddStatusOk;
}

void chromiumMediaSrcSetDuration(ChromiumMediaSrc* src,
                                 const base::TimeDelta& duration) {
  ChromiumMediaSrcPrivate* priv = src->priv;

  GstClockTime gst_duration = duration.InMicroseconds() * 1000;
  if (!GST_CLOCK_TIME_IS_VALID(gst_duration))
    return;

  GST_DEBUG_OBJECT(src, "Received duration: %" GST_TIME_FORMAT,
                   GST_TIME_ARGS(gst_duration));

  GST_OBJECT_LOCK(src);
  priv->duration = gst_duration;
  GST_OBJECT_UNLOCK(src);
  gst_element_post_message(GST_ELEMENT(src),
                           gst_message_new_duration_changed(GST_OBJECT(src)));

  GST_OBJECT_UNLOCK(src);
}

bool chromiumMediaSrcAppendData(ChromiumMediaSrc* src,
                                const std::string& source_id,
                                const std::vector<unsigned char>& data,
                                const std::vector<base::TimeDelta>& times,
                                base::TimeDelta& timestamp_offset) {
  ChromiumMediaSrcPrivate* priv = src->priv;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer* buffer;
  Source* source = 0;

  if (!priv->noMorePads) {
    priv->noMorePads = true;
    gst_element_no_more_pads(GST_ELEMENT(src));
    chromiumMediaSrcDoAsyncDone(src);
  }

  GST_OBJECT_LOCK(src);

  for (GList* iter = priv->sources; iter; iter = iter->next) {
    Source* tmp = static_cast<Source*>(iter->data);
    if (tmp->id == source_id) {
      source = tmp;
      break;
    }
  }

  if (!source || !source->src) {
    GST_OBJECT_UNLOCK(src);
    return false;
  }

  buffer = gst_buffer_new_and_alloc(data.size());
  gst_buffer_fill(buffer, 0, data.data(), data.size());

  ret = gst_app_src_push_buffer(GST_APP_SRC(source->src), buffer);
  GST_DEBUG_OBJECT(src, "push buffer %d\n", static_cast<int>(ret));

  // TODO: use times which contains windowStart, windowEnd and timestamp offset.
  // Also update timestamp_offset.

  if (ret == GST_FLOW_OK) {
    GST_OBJECT_UNLOCK(src);
    return true;
  }

  GST_OBJECT_UNLOCK(src);

  return false;
}

void chromiumMediaSrcAbort(ChromiumMediaSrc* src,
                           const std::string& source_id) {
  ChromiumMediaSrcPrivate* priv = src->priv;
  Source* source = 0;

  GST_OBJECT_LOCK(src);

  if (source_id.empty()) {
    GST_WARNING_OBJECT(src, "source id is empty");
    GST_OBJECT_UNLOCK(src);
    return;
  }

  for (GList* iter = priv->sources; iter; iter = iter->next) {
    Source* tmp = static_cast<Source*>(iter->data);
    if (tmp->id == source_id) {
      source = tmp;
      break;
    }
  }

  if (!source || !source->src) {
    GST_WARNING_OBJECT(src, "cannot find source %s", source_id.c_str());
    GST_OBJECT_UNLOCK(src);
    return;
  }

  GST_OBJECT_UNLOCK(src);

  GST_DEBUG_OBJECT(src, "TODO: abort source: %s", source_id.c_str());

  // TODO: after abort has been called the next append buffer will be
  // the first playback position.
  // Maybe a flush seek to 0 will do.
  NOTIMPLEMENTED();
}

void chromiumMediaSrcMarkEndOfStream(ChromiumMediaSrc* src) {
  ChromiumMediaSrcPrivate* priv = src->priv;

  GST_DEBUG_OBJECT(src, "Have EOS");

  if (!priv->noMorePads) {
    priv->noMorePads = true;
    gst_element_no_more_pads(GST_ELEMENT(src));
    chromiumMediaSrcDoAsyncDone(src);
  }

  for (GList* iter = priv->sources; iter; iter = iter->next) {
    Source* source = static_cast<Source*>(iter->data);
    if (source->src)
      gst_app_src_end_of_stream(GST_APP_SRC(source->src));
  }
}

void chromiumMediaSrcRemoveSourceBufferId(ChromiumMediaSrc* src,
                                          const std::string& source_id) {
  GST_OBJECT_LOCK(src);

  ChromiumMediaSrcPrivate* priv = src->priv;
  Source* source = 0;

  for (GList* iter = priv->sources; iter; iter = iter->next) {
    Source* tmp = static_cast<Source*>(iter->data);
    if (tmp->id == source_id) {
      source = tmp;
      break;
    }
  }

  DCHECK(source && source->src);

  priv->sources = g_list_remove(priv->sources, (gconstpointer)source);

  destroy_source(source, src);

  GST_OBJECT_UNLOCK(src);
}

static std::string gstCodecToMSECodec(const std::string& codec) {
  if (codec.empty())
    return "";

  for (size_t i = 0; i < G_N_ELEMENTS(codecs_map); ++i) {
    if (strcmp(codec.c_str(), codecs_map[i].gst) == 0) {
      return codecs_map[i].mse;
    }
  }
  return "";
}

std::string chromiumMediaSrcIsMatchingSourceId(ChromiumMediaSrc* src,
                                               const std::string& media_kind,
                                               const std::string& gst_codec) {
  GST_OBJECT_LOCK(src);

  ChromiumMediaSrcPrivate* priv = src->priv;

  std::string mse_codec = gstCodecToMSECodec(gst_codec);
  std::string lower_gst_codec;
  std::transform(gst_codec.begin(), gst_codec.end(), lower_gst_codec.begin(),
                 ::tolower);

  for (GList* iter = priv->sources; iter; iter = iter->next) {
    Source* source = static_cast<Source*>(iter->data);
    if (!source->initialized &&
        source->media_type.find(media_kind) != std::string::npos &&
        (source->codec.find(mse_codec) != std::string::npos ||
         source->codec.find(lower_gst_codec) != std::string::npos ||
         lower_gst_codec.find(source->codec) != std::string::npos)) {
      source->initialized = true;
      GST_OBJECT_UNLOCK(src);
      return source->id;
    }
  }

  GST_OBJECT_UNLOCK(src);

  return "";
}

void chromiumMediaSrcUnmarkEndOfStream(ChromiumMediaSrc* src) {}

void chromiumMediaSrcSetSequenceMode(ChromiumMediaSrc* src,
                                     const std::string& source_id,
                                     bool sequence_mod) {
  NOTIMPLEMENTED();
}

void chromiumMediaSrcSetGroupStartTimestampIfInSequenceMode(
    ChromiumMediaSrc* src,
    const std::string& source_id,
    const base::TimeDelta& timestamp_offset) {
  NOTIMPLEMENTED();
}

void chromiumMediaSrcRemoveSegment(ChromiumMediaSrc* src,
                                   const std::string& source_id,
                                   const base::TimeDelta& start,
                                   const base::TimeDelta& end) {
  NOTIMPLEMENTED();
}
