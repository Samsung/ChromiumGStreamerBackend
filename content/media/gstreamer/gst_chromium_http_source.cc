// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/media/gstreamer/gst_chromium_http_source.h"

#include <condition_variable>
#include <mutex>

#include "base/callback.h"
#include "base/bind.h"
#include "base/synchronization/waitable_event.h"
#include "content/child/child_process.h"
#include "content/child/child_thread_impl.h"
#include "content/child/web_url_loader_impl.h"
#include "url/gurl.h"

static scoped_refptr<media::MediaLog> media_log_;
static media::BufferedDataSourceHost* buffered_data_source_host_;
static content::ResourceDispatcher* resource_dispatcher_;
static scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;

#define DEFAULT_BLOCKSIZE 4 * 1024

#define CHROMIUM_HTTP_SRC_GET_PRIVATE(obj)                    \
  (G_TYPE_INSTANCE_GET_PRIVATE((obj), CHROMIUM_TYPE_HTTP_SRC, \
                               ChromiumHttpSrcPrivate))

// Inspired from WTF/wtf/glib/GUniquePtr.h
// TODO: move this in a separate header and handle all types.
template<typename T>
struct GPtrDeleter {
  void operator()(T* ptr) const { g_free(ptr); }
};

template<typename T>
using GUniquePtr = std::unique_ptr<T, GPtrDeleter<T>>;

#define WTF_DEFINE_GPTR_DELETER(typeName, deleterFunc) \
  template<> struct GPtrDeleter<typeName> \
  { \
    void operator() (typeName* ptr) const \
    { \
      if (ptr) \
      deleterFunc(ptr); \
    } \
  };

WTF_DEFINE_GPTR_DELETER(GstStructure, gst_structure_free)

struct _ChromiumHttpSrcPrivate {
  gchar* uri;
  bool keepAlive;
  GUniquePtr<GstStructure> extraHeaders;
  bool compress;

  bool data_source_initialized_;
  scoped_ptr<media::BufferedDataSource> data_source_;
  gint last_read_bytes_;
  base::Closure error_cb_;

  base::WaitableEvent* aborted_;
  base::WaitableEvent* read_complete_;

  gboolean pendingStart;

  // icecast stuff
  gchar* iradioName;
  gchar* iradioGenre;
  gchar* iradioUrl;
  gchar* iradioTitle;

  std::mutex mutex_data_source_;
  std::condition_variable condition_data_source_;
  scoped_refptr<media::MediaLog> media_log_;
  media::BufferedDataSourceHost* buffered_data_source_host_;
  content::ResourceDispatcher* resource_dispatcher_;
  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
};

enum {
  PROP_IRADIO_NAME = 1,
  PROP_IRADIO_GENRE,
  PROP_IRADIO_URL,
  PROP_IRADIO_TITLE,
  PROP_LOCATION,
  PROP_KEEP_ALIVE,
  PROP_EXTRA_HEADERS,
  PROP_COMPRESS
};

static GstStaticPadTemplate srcTemplate =
    GST_STATIC_PAD_TEMPLATE("src",
                            GST_PAD_SRC,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC(chromium_http_src_debug);
#define GST_CAT_DEFAULT chromium_http_src_debug

static void chromiumHttpSrcUriHandlerInit(gpointer gIface, gpointer ifaceData);

static void chromiumHttpSrcDispose(GObject*);
static void chromiumHttpSrcFinalize(GObject*);
static void chromiumHttpSrcSetProperty(GObject*,
                                       guint propertyID,
                                       const GValue*,
                                       GParamSpec*);
static void chromiumHttpSrcGetProperty(GObject*,
                                       guint propertyID,
                                       GValue*,
                                       GParamSpec*);
static gboolean chromiumHttpSrcStart(GstBaseSrc* basesrc);
static gboolean chromiumHttpSrcStop(GstBaseSrc* basesrc);
static gboolean chromiumHttpSrcIsSeekable(GstBaseSrc* src);
static gboolean chromiumHttpSrcGetSize(GstBaseSrc* src, guint64* size);
static GstFlowReturn chromiumHttpSrcFill(GstBaseSrc* src,
                                         guint64 offset,
                                         guint length,
                                         GstBuffer* buf);

static void onResetDataSource(GstBaseSrc* basesrc);

#define chromium_http_src_parent_class parent_class
#define CHROMIUM_HTTP_SRC_CATEGORY_INIT                                  \
  GST_DEBUG_CATEGORY_INIT(chromium_http_src_debug, "chromiumhttpsrc", 0, \
                          "Chromium http source element");
G_DEFINE_TYPE_WITH_CODE(ChromiumHttpSrc,
                        chromium_http_src,
                        GST_TYPE_BASE_SRC,
                        G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER,
                                              chromiumHttpSrcUriHandlerInit);
                        CHROMIUM_HTTP_SRC_CATEGORY_INIT);

void chromiumHttpSrcSetup(
    ChromiumHttpSrc* src,
    scoped_refptr<media::MediaLog> media_log,
    media::BufferedDataSourceHost* buffered_data_source_host,
    content::ResourceDispatcher* resource_dispatcher,
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner) {
  ChromiumHttpSrcPrivate* priv = src->priv;

  static volatile gsize _init = 0;

  // Keep them around for hls: gst_element_make_from_uri.
  if (g_once_init_enter(&_init)) {
    media_log_ = media_log;
    buffered_data_source_host_ = buffered_data_source_host;
    resource_dispatcher_ = resource_dispatcher;
    main_task_runner_ = main_task_runner;
    g_once_init_leave(&_init, 1);
  }

  priv->media_log_ = media_log_;
  priv->buffered_data_source_host_ = buffered_data_source_host_;
  priv->resource_dispatcher_ = resource_dispatcher_;
  priv->main_task_runner_ = main_task_runner_;
}

static void chromium_http_src_class_init(ChromiumHttpSrcClass* klass) {
  GObjectClass* oklass = G_OBJECT_CLASS(klass);
  GstElementClass* eklass = GST_ELEMENT_CLASS(klass);
  GstBaseSrcClass* bklass = GST_BASE_SRC_CLASS(klass);

  oklass->dispose = GST_DEBUG_FUNCPTR(chromiumHttpSrcDispose);
  oklass->finalize = GST_DEBUG_FUNCPTR(chromiumHttpSrcFinalize);
  oklass->set_property = GST_DEBUG_FUNCPTR(chromiumHttpSrcSetProperty);
  oklass->get_property = GST_DEBUG_FUNCPTR(chromiumHttpSrcGetProperty);

  bklass->start = GST_DEBUG_FUNCPTR(chromiumHttpSrcStart);
  bklass->stop = GST_DEBUG_FUNCPTR(chromiumHttpSrcStop);
  bklass->is_seekable = GST_DEBUG_FUNCPTR(chromiumHttpSrcIsSeekable);
  bklass->get_size = GST_DEBUG_FUNCPTR(chromiumHttpSrcGetSize);
  bklass->fill = GST_DEBUG_FUNCPTR(chromiumHttpSrcFill);

  gst_element_class_set_static_metadata(
      eklass, "Chromium http source element", "Source",
      "Handles FILE/HTTP/HTTPS/blob uris",
      "Julien Isorce <julien.isorce@samsung.com>");

  gst_element_class_add_pad_template(eklass,
                                     gst_static_pad_template_get(&srcTemplate));

  // icecast stuff
  g_object_class_install_property(
      oklass, PROP_IRADIO_NAME,
      g_param_spec_string(
          "iradio-name", "iradio-name", "Name of the stream", 0,
          (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      oklass, PROP_IRADIO_GENRE,
      g_param_spec_string(
          "iradio-genre", "iradio-genre", "Genre of the stream", 0,
          (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      oklass, PROP_IRADIO_URL,
      g_param_spec_string(
          "iradio-url", "iradio-url", "Homepage URL for radio stream", 0,
          (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      oklass, PROP_IRADIO_TITLE,
      g_param_spec_string(
          "iradio-title", "iradio-title", "Name of currently playing song", 0,
          (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  /* Allows setting the uri using the 'location' property, which is used
   * for example by gst_element_make_from_uri() */
  g_object_class_install_property(
      oklass, PROP_LOCATION,
      g_param_spec_string(
          "location", "location", "Location to read from", 0,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      oklass, PROP_KEEP_ALIVE,
      g_param_spec_boolean("keep-alive", "keep-alive",
                           "Use HTTP persistent connections", FALSE,
                           static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                    G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      oklass, PROP_EXTRA_HEADERS,
      g_param_spec_boxed("extra-headers", "Extra Headers",
                         "Extra headers to append to the HTTP request",
                         GST_TYPE_STRUCTURE,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      oklass, PROP_COMPRESS,
      g_param_spec_boolean("compress", "Compress",
                           "Allow compressed content encodings", FALSE,
                           static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                    G_PARAM_STATIC_STRINGS)));

  g_type_class_add_private(klass, sizeof(ChromiumHttpSrcPrivate));
}

static void chromium_http_src_init(ChromiumHttpSrc* src) {
  ChromiumHttpSrcPrivate* priv = CHROMIUM_HTTP_SRC_GET_PRIVATE(src);
  src->priv = priv;

  priv->data_source_ = nullptr;
  priv->data_source_initialized_ = false;

  priv->aborted_ = new base::WaitableEvent{true, false};
  priv->read_complete_ = new base::WaitableEvent{false, false};

  priv->media_log_ = media_log_;
  priv->buffered_data_source_host_ = buffered_data_source_host_;
  priv->resource_dispatcher_ = resource_dispatcher_;
  priv->main_task_runner_ = main_task_runner_;

  gst_base_src_set_blocksize(GST_BASE_SRC(src), DEFAULT_BLOCKSIZE);
}

static void chromiumHttpSrcDispose(GObject* object) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(object);
  ChromiumHttpSrcPrivate* priv = src->priv;

  priv->data_source_ = nullptr;
  priv->data_source_initialized_ = false;

  GST_CALL_PARENT(G_OBJECT_CLASS, dispose, (object));
}

static void chromiumHttpSrcFinalize(GObject* object) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(object);
  ChromiumHttpSrcPrivate* priv = src->priv;

  g_free(priv->uri);

  delete priv->read_complete_;
  delete priv->aborted_;

  priv->media_log_ = nullptr;
  priv->buffered_data_source_host_ = nullptr;
  priv->resource_dispatcher_ = nullptr;
  priv->main_task_runner_ = nullptr;

  GST_CALL_PARENT(G_OBJECT_CLASS, finalize, (object));
}

static void chromiumHttpSrcSetProperty(GObject* object,
                                       guint propID,
                                       const GValue* value,
                                       GParamSpec* pspec) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(object);

  switch (propID) {
    case PROP_LOCATION:
      gst_uri_handler_set_uri(reinterpret_cast<GstURIHandler*>(src),
                              g_value_get_string(value), 0);
      break;
    case PROP_KEEP_ALIVE:
      src->priv->keepAlive = g_value_get_boolean(value);
      break;
    case PROP_EXTRA_HEADERS: {
      const GstStructure* s = gst_value_get_structure(value);
      src->priv->extraHeaders.reset(s ? gst_structure_copy(s) : nullptr);
      break;
    }
    case PROP_COMPRESS:
      src->priv->compress = g_value_get_boolean(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propID, pspec);
      break;
  }
}

static void chromiumHttpSrcGetProperty(GObject* object,
                                       guint propID,
                                       GValue* value,
                                       GParamSpec* pspec) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(object);
  ChromiumHttpSrcPrivate* priv = src->priv;

  GST_OBJECT_LOCK(src);
  switch (propID) {
    case PROP_IRADIO_NAME:
      g_value_set_string(value, priv->iradioName);
      break;
    case PROP_IRADIO_GENRE:
      g_value_set_string(value, priv->iradioGenre);
      break;
    case PROP_IRADIO_URL:
      g_value_set_string(value, priv->iradioUrl);
      break;
    case PROP_IRADIO_TITLE:
      g_value_set_string(value, priv->iradioTitle);
      break;
    case PROP_LOCATION:
      g_value_set_string(value, priv->uri);
      break;
    case PROP_KEEP_ALIVE:
      g_value_set_boolean(value, priv->keepAlive);
      break;
    case PROP_EXTRA_HEADERS:
      gst_value_set_structure(value, priv->extraHeaders.get());
      break;
    case PROP_COMPRESS:
      g_value_set_boolean(value, priv->compress);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propID, pspec);
      break;
  }

  GST_OBJECT_UNLOCK(src);
}

static void SignalReadCompleted(GstBaseSrc* basesrc, int size) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(basesrc);
  ChromiumHttpSrcPrivate* priv = src->priv;

  priv->last_read_bytes_ = size;
  priv->read_complete_->Signal();
}

static GstFlowReturn chromiumHttpSrcFill(GstBaseSrc* basesrc,
                                         guint64 offset,
                                         guint length,
                                         GstBuffer* buf) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(basesrc);
  ChromiumHttpSrcPrivate* priv = src->priv;
  guint to_read = 0;
  guint bytes_read = 0;
  GstMapInfo info;
  int ret = 0;
  guint8* data = 0;
  int64 read_position = offset;

  if (priv->aborted_->IsSignaled()) {
    GST_ELEMENT_ERROR(src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return GST_FLOW_ERROR;
  }

  int64 file_size;
  if (priv->data_source_->GetSize(&file_size) && read_position >= file_size) {
    GST_ELEMENT_ERROR(src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return GST_FLOW_ERROR;
  }

  gst_buffer_map(buf, &info, GST_MAP_WRITE);
  data = info.data;

  bytes_read = 0;
  to_read = length;
  while (to_read > 0) {
    GST_LOG_OBJECT(src, "Reading %d bytes at offset 0x%" G_GINT64_MODIFIER "x",
                   to_read, offset + bytes_read);
    errno = 0;
    // Blocking read from data source until either:
    //   1) |last_read_bytes_| is set and |read_complete_| is signalled
    //   2) |aborted_| is signalled
    priv->last_read_bytes_ = 0;

    priv->data_source_->Read(read_position, to_read, data,
                             base::Bind(&SignalReadCompleted, basesrc));

    base::WaitableEvent* events[] = {priv->aborted_, priv->read_complete_};
    size_t index = base::WaitableEvent::WaitMany(events, arraysize(events));

    if (events[index] == priv->aborted_) {
      GST_ELEMENT_ERROR(src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
      gst_buffer_unmap(buf, &info);
      gst_buffer_resize(buf, 0, 0);
      return GST_FLOW_ERROR;
    }
    if (priv->last_read_bytes_ == media::DataSource::kReadError) {
      priv->aborted_->Signal();
      priv->error_cb_.Run();
      gst_buffer_unmap(buf, &info);
      gst_buffer_resize(buf, 0, 0);
      GST_ELEMENT_ERROR(src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
      return GST_FLOW_ERROR;
    }

    ret = priv->last_read_bytes_;
    if (G_UNLIKELY(ret == 0)) {
      // .. but first we should return any remaining data
      if (bytes_read > 0)
        break;
      goto eos;
    }

    to_read -= ret;
    bytes_read += ret;

    read_position += ret;
  }

  priv->last_read_bytes_ = 0;

  gst_buffer_unmap(buf, &info);
  if (bytes_read != length)
    gst_buffer_resize(buf, 0, bytes_read);

  GST_BUFFER_OFFSET(buf) = offset;
  GST_BUFFER_OFFSET_END(buf) = offset + bytes_read;

  return GST_FLOW_OK;

eos : {
  GST_DEBUG("EOS");
  gst_buffer_unmap(buf, &info);
  gst_buffer_resize(buf, 0, 0);
  return GST_FLOW_EOS;
}
}

static gboolean chromiumHttpSrcIsSeekable(GstBaseSrc* basesrc) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(basesrc);
  ChromiumHttpSrcPrivate* priv = src->priv;
  return !priv->data_source_->IsStreaming();
}

static gboolean chromiumHttpSrcGetSize(GstBaseSrc* basesrc, guint64* size) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(basesrc);
  ChromiumHttpSrcPrivate* priv = src->priv;

  int64 file_size;
  if (!priv->data_source_->GetSize(&file_size))
    return FALSE;

  *size = file_size;

  return TRUE;
}

static gboolean chromiumHttpSrcStop(GstBaseSrc* basesrc) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(basesrc);
  ChromiumHttpSrcPrivate* priv = src->priv;

  GST_OBJECT_LOCK(src);

  {
    std::unique_lock<std::mutex> lock(priv->mutex_data_source_);

    if (priv->data_source_)
    // Can be called in any thread.
    priv->data_source_->Stop();

    if (priv->data_source_initialized_) {
      // media::BufferedDataSource has to be released where it has been attached.
      priv->main_task_runner_->PostTask(FROM_HERE,
                                            base::Bind(&onResetDataSource, basesrc));
      priv->condition_data_source_.wait(lock);

      DCHECK(priv->data_source_ == nullptr);
    }

    g_free(priv->iradioName);
    priv->iradioName = 0;

    g_free(priv->iradioGenre);
    priv->iradioGenre = 0;

    g_free(priv->iradioUrl);
    priv->iradioUrl = 0;

    g_free(priv->iradioTitle);
    priv->iradioTitle = 0;
  }

  GST_OBJECT_UNLOCK(src);
  return TRUE;
}

static bool chromiumHttpSrcSetExtraHeader(GQuark fieldId,
                                          const GValue* value,
                                          gpointer userData) {
  GUniquePtr<gchar> fieldContent;

  if (G_VALUE_HOLDS_STRING(value))
    fieldContent.reset(g_value_dup_string(value));
  else {
    GValue dest = G_VALUE_INIT;

    g_value_init(&dest, G_TYPE_STRING);
    if (g_value_transform(value, &dest))
      fieldContent.reset(g_value_dup_string(&dest));
  }

  const gchar* fieldName = g_quark_to_string(fieldId);
  if (!fieldContent.get()) {
    GST_ERROR(
        "extra-headers field '%s' contains no value or can't be converted to a "
        "string",
        fieldName);
    return false;
  }

  GST_DEBUG("Appending extra header: \"%s: %s\"", fieldName,
            fieldContent.get());
  // ResourceRequest* request = static_cast<ResourceRequest*>(userData);
  // request->setHTTPHeaderField(fieldName, fieldContent.get());
  return true;
}

static gboolean chromiumHttpSrcProcessExtraHeaders(GQuark fieldId,
                                                   const GValue* value,
                                                   gpointer userData) {
  if (G_VALUE_TYPE(value) == GST_TYPE_ARRAY) {
    unsigned size = gst_value_array_get_size(value);

    for (unsigned i = 0; i < size; i++) {
      if (!chromiumHttpSrcSetExtraHeader(
              fieldId, gst_value_array_get_value(value, i), userData))
        return FALSE;
    }
    return TRUE;
  }

  if (G_VALUE_TYPE(value) == GST_TYPE_LIST) {
    unsigned size = gst_value_list_get_size(value);

    for (unsigned i = 0; i < size; i++) {
      if (!chromiumHttpSrcSetExtraHeader(
              fieldId, gst_value_list_get_value(value, i), userData))
        return FALSE;
    }
    return TRUE;
  }

  return chromiumHttpSrcSetExtraHeader(fieldId, value, userData);
}

static void onSourceInitialized(GstBaseSrc* basesrc, bool success) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(basesrc);
  ChromiumHttpSrcPrivate* priv = src->priv;

  std::unique_lock<std::mutex> lock(priv->mutex_data_source_);

  GST_DEBUG("Data source notified intialization");

  priv->data_source_initialized_ = success;

  priv->condition_data_source_.notify_one();
}

static void onNotifyDownloading(GstBaseSrc* basesrc, bool is_downloading) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(basesrc);
  ChromiumHttpSrcPrivate* priv = src->priv;

  priv->media_log_->AddEvent(priv->media_log_->CreateBooleanEvent(
      media::MediaLogEvent::NETWORK_ACTIVITY_SET, "is_downloading_data",
      is_downloading));

  GST_DEBUG("Data source downloading: %d", is_downloading);
}

static void onResetDataSource(GstBaseSrc* basesrc) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(basesrc);
  ChromiumHttpSrcPrivate* priv = src->priv;

  if (priv->data_source_initialized_) {
    // The callback has been spwan to released the data source.
    priv->data_source_ = nullptr;
    priv->data_source_initialized_ = false;
    priv->condition_data_source_.notify_one();
    return;
  }

  GST_DEBUG("Preparing data source for uri: %s", priv->uri);

  content::WebURLLoaderImpl* url_loader = new content::WebURLLoaderImpl(
      priv->resource_dispatcher_, priv->main_task_runner_);

  // TODO: allow to set extra headers on WebURLLoaderImpl

  /*URL url = URL(URL(), priv->uri);

  ResourceRequest request(url);
  request.setAllowCookies(true);
  request.setFirstPartyForCookies(url);

  priv->size = 0;

  if (priv->player)
      request.setHTTPReferrer(priv->player->referrer());*/

  // By default, HTTP Accept-Encoding is disabled here as we don't
  // want the received response to be encoded in any way as we need
  // to rely on the proper size of the returned data on
  // didReceiveResponse.
  // If Accept-Encoding is used, the server may send the data in encoded format
  // and
  // request.expectedContentLength() will have the "wrong" size (the size of the
  // compressed data), even though the data received in didReceiveData is
  // uncompressed.
  // This is however useful to enable for adaptive streaming
  // scenarios, when the demuxer needs to download playlists.
  /*if (!priv->compress)
      request.setAcceptEncoding(false);*/

  // Let Apple web servers know we want to access their nice movie trailers.
  /*if (!g_ascii_strcasecmp("movies.apple.com", url.host().utf8().data())
      || !g_ascii_strcasecmp("trailers.apple.com", url.host().utf8().data()))
      request.setHTTPUserAgent("Quicktime/7.6.6");*/

  /*if (priv->requestedOffset) {
      GUniquePtr<gchar> val(g_strdup_printf("bytes=%" G_GUINT64_FORMAT "-",
  priv->requestedOffset));
      request.setHTTPHeaderField(HTTPHeaderName::Range, val.get());
  }
  priv->offset = priv->requestedOffset;*/

  /*if (!priv->keepAlive) {
      GST_DEBUG_OBJECT(src, "Persistent connection support disabled");
      request.setHTTPHeaderField(HTTPHeaderName::Connection, "close");
  }*/

  // We always request Icecast/Shoutcast metadata, just in case ...
  // request.setHTTPHeaderField(HTTPHeaderName::IcyMetadata, "1");*/

  if (priv->extraHeaders)
    gst_structure_foreach(priv->extraHeaders.get(),
                          chromiumHttpSrcProcessExtraHeaders, /*&request*/ 0);

  // TODO: get this from IPC
  blink::WebString referrer = "";
  blink::WebReferrerPolicy referrer_policy = blink::WebReferrerPolicyDefault;
  media::BufferedResourceLoader::CORSMode cors_mode =
      media::BufferedResourceLoader::CORSMode::kUnspecified;

  priv->data_source_.reset(new media::BufferedDataSource(
      GURL(priv->uri),
      static_cast<media::BufferedResourceLoader::CORSMode>(cors_mode),
      priv->main_task_runner_, nullptr, priv->media_log_.get(),
      priv->buffered_data_source_host_,
      base::Bind(&onNotifyDownloading, basesrc)));

  priv->data_source_->SetPreload(media::BufferedDataSource::AUTO);

  GST_DEBUG("Data source prepared for uri: %s", priv->uri);

  priv->data_source_->Initialize(base::Bind(&onSourceInitialized, basesrc),
                                 url_loader, referrer, referrer_policy);

  priv->data_source_->MediaIsPlaying();
}

static gboolean chromiumHttpSrcStart(GstBaseSrc* basesrc) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(basesrc);
  ChromiumHttpSrcPrivate* priv = src->priv;

  GST_OBJECT_LOCK(src);

  if (!priv->uri) {
    GST_ERROR_OBJECT(src, "No URI provided");
    GST_OBJECT_UNLOCK(src);
    return false;
  }

  if (!priv->resource_dispatcher_) {
    GST_WARNING("Resource dispatcher has not been set");
    GST_OBJECT_UNLOCK(src);
    return false;
  }

  GST_DEBUG("Creating data source");

  std::unique_lock<std::mutex> lock(priv->mutex_data_source_);
  priv->main_task_runner_->PostTask(FROM_HERE,
                                    base::Bind(&onResetDataSource, basesrc));
  priv->condition_data_source_.wait(lock);

  if (!priv->data_source_initialized_) {
    GST_ERROR("Failed to initialized data source");
    GST_OBJECT_UNLOCK(src);
    return false;
  }

  GST_DEBUG("Data source is initialized");

  // TODO set data_source_->SetBitrate();

  gst_base_src_set_dynamic_size(basesrc, chromiumHttpSrcIsSeekable(basesrc));

  // priv->data_source_->MediaIsPlaying();

  // TODO call data_source_->MediaIsPaused(); when paused

  GST_OBJECT_UNLOCK(src);
  return true;
}

static bool urlHasSupportedProtocol(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIsBlob();
}

static GstURIType chromiumHttpSrcUriGetType(GType) {
  return GST_URI_SRC;
}

const gchar* const* chromiumHttpSrcGetProtocols(GType) {
  static const char* protocols[] = {"file", "http", "https", "blob", 0};
  return protocols;
}

static gchar* chromiumHttpSrcGetUri(GstURIHandler* handler) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(handler);
  gchar* ret;

  GST_OBJECT_LOCK(src);
  ret = g_strdup(src->priv->uri);

  GST_OBJECT_UNLOCK(src);
  return ret;
}

static gboolean chromiumHttpSrcSetUri(GstURIHandler* handler,
                                      const gchar* uri,
                                      GError** error) {
  ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(handler);
  ChromiumHttpSrcPrivate* priv = src->priv;

  if (GST_STATE(src) >= GST_STATE_PAUSED) {
    GST_ERROR_OBJECT(src, "URI can only be set in states < PAUSED");
    return FALSE;
  }

  GST_OBJECT_LOCK(src);

  g_free(priv->uri);
  priv->uri = 0;

  if (!uri)
    return TRUE;

  GURL url(uri);
  if (!urlHasSupportedProtocol(url)) {
    g_set_error(error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI, "Invalid URI '%s'",
                uri);
    return FALSE;
  }

  priv->uri = g_strdup(url.spec().c_str());

  GST_OBJECT_UNLOCK(src);
  return TRUE;
}

static void chromiumHttpSrcUriHandlerInit(gpointer gIface, gpointer) {
  GstURIHandlerInterface* iface = (GstURIHandlerInterface*)gIface;

  iface->get_protocols = chromiumHttpSrcGetProtocols;
  iface->get_type = chromiumHttpSrcUriGetType;
  iface->get_uri = chromiumHttpSrcGetUri;
  iface->set_uri = chromiumHttpSrcSetUri;
}

// TODO Allow to register a handleResponse callback to data source in order to
// parse blink::WebURLResponse.
/*
void StreamingClient::handleResponseReceived(const ResourceResponse& response)
{
    ChromiumHttpSrc* src = CHROMIUM_HTTP_SRC(m_src);
    ChromiumHttpSrcPrivate* priv = src->priv;

    GST_DEBUG_OBJECT(src, "Received response: %d", response.httpStatusCode());

    if (response.httpStatusCode() >= 400) {
        GST_ELEMENT_ERROR(src, RESOURCE, READ, ("Received %d HTTP error code",
response.httpStatusCode()), (nullptr));
        gst_app_src_end_of_stream(priv->appsrc);
        chromiumHttpSrcStop(src);
        return;
    }

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));

    if (priv->seekSource.isActive()) {
        GST_DEBUG_OBJECT(src, "Seek in progress, ignoring response");
        return;
    }

    if (priv->requestedOffset) {
        // Seeking ... we expect a 206 == PARTIAL_CONTENT
        if (response.httpStatusCode() == 200) {
            // Range request didn't have a ranged response; resetting offset.
            priv->offset = 0;
        } else if (response.httpStatusCode() != 206) {
            // Range request completely failed.
            locker.unlock();
            GST_ELEMENT_ERROR(src, RESOURCE, READ, ("Received unexpected %d HTTP
status code", response.httpStatusCode()), (nullptr));
            gst_app_src_end_of_stream(priv->appsrc);
            chromiumHttpSrcStop(src);
            return;
        }
    }

    long long length = response.expectedContentLength();
    if (length > 0 && priv->requestedOffset && response.httpStatusCode() == 206)
        length += priv->requestedOffset;

    priv->size = length >= 0 ? length : 0;
    priv->seekable = length > 0 && g_ascii_strcasecmp("none",
response.httpHeaderField(HTTPHeaderName::AcceptRanges).utf8().data());

    // Wait until we unlock to send notifications
    g_object_freeze_notify(G_OBJECT(src));

    GstTagList* tags = gst_tag_list_new_empty();
    String value = response.httpHeaderField(HTTPHeaderName::IcyName);
    if (!value.isEmpty()) {
        g_free(priv->iradioName);
        priv->iradioName = g_strdup(value.utf8().data());
        g_object_notify(G_OBJECT(src), "iradio-name");
        gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_ORGANIZATION,
priv->iradioName, NULL);
    }
    value = response.httpHeaderField(HTTPHeaderName::IcyGenre);
    if (!value.isEmpty()) {
        g_free(priv->iradioGenre);
        priv->iradioGenre = g_strdup(value.utf8().data());
        g_object_notify(G_OBJECT(src), "iradio-genre");
        gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_GENRE,
priv->iradioGenre, NULL);
    }
    value = response.httpHeaderField(HTTPHeaderName::IcyURL);
    if (!value.isEmpty()) {
        g_free(priv->iradioUrl);
        priv->iradioUrl = g_strdup(value.utf8().data());
        g_object_notify(G_OBJECT(src), "iradio-url");
        gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_LOCATION,
priv->iradioUrl, NULL);
    }
    value = response.httpHeaderField(HTTPHeaderName::IcyTitle);
    if (!value.isEmpty()) {
        g_free(priv->iradioTitle);
        priv->iradioTitle = g_strdup(value.utf8().data());
        g_object_notify(G_OBJECT(src), "iradio-title");
        gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_TITLE,
priv->iradioTitle, NULL);
    }

    locker.unlock();
    g_object_thaw_notify(G_OBJECT(src));

    // notify size/duration
    if (length > 0) {
        gst_app_src_set_size(priv->appsrc, length);
    } else
        gst_app_src_set_size(priv->appsrc, -1);

    // icecast stuff
    value = response.httpHeaderField(HTTPHeaderName::IcyMetaInt);
    if (!value.isEmpty()) {
        gchar* endptr = 0;
        gint64 icyMetaInt = g_ascii_strtoll(value.utf8().data(), &endptr, 10);

        if (endptr && *endptr == '\0' && icyMetaInt > 0) {
            GRefPtr<GstCaps> caps =
adoptGRef(gst_caps_new_simple("application/x-icy", "metadata-interval",
G_TYPE_INT, (gint) icyMetaInt, NULL));

            gst_app_src_set_caps(priv->appsrc, caps.get());
        }
    } else
        gst_app_src_set_caps(priv->appsrc, 0);

    // notify tags
    if (gst_tag_list_is_empty(tags))
        gst_tag_list_unref(tags);
    else
        gst_pad_push_event(priv->srcpad, gst_event_new_tag(tags));
}
*/
