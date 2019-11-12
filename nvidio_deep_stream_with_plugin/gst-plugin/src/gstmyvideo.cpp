#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <glib/gstdio.h>

#include "gstmyvideo.h"
#include <sys/time.h>
#include <nvdsmeta.h>
#include <gstnvdsmeta.h>

#include "string"
/* Here we define our expected video format, somewhat on voluntary basis.
 * In production, we probably should use something that needs not be converted. */

#define VIDEO_SRC_CAPS \
  GST_VIDEO_CAPS_MAKE("{ NV12 }")

#define VIDEO_SINK_CAPS \
  GST_VIDEO_CAPS_MAKE("{ NV12 }")

/**
 * This function contains the image processing.
 */
static GstFlowReturn gst_n_video_transform_frame_ip(GstVideoFilter *filter,
                                                    GstVideoFrame *frame) {
  GstNVideo *me = GST_NVIDEO (filter);
  GstMapInfo info;

  gst_buffer_map(frame->buffer, &info, GST_MAP_WRITE);  // GST_MAP_READ or GST_MAP_READWRITE also possible
  guint64 frame_no = me->count++;
  gint width = frame->info.width;
  gint height = frame->info.height;
  gsize size = gst_buffer_get_size(frame->buffer);
  gsize bytes_per_pixel = size / (width * height);
  guint pixel_stride = GST_VIDEO_FRAME_COMP_PSTRIDE (frame, 0); // Bytes per pixel.
  guint stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0); // Bytes per row

  if (!me->silent) {
    g_print("Plugin: Video frame number %lu, size %d x %d received (%u bytes per pixel, %u per row). "
            "Buffer size %lu (alternatively %u, %u). %lf fps. \n",
            frame_no, width, height, pixel_stride, stride, size, height * stride, width * height * pixel_stride,
            measure_fps(me, frame_no));
    // This prints size 392 x 216 received (1 bytes per pixel, 392 per row). Buffer size 127008 (alternatively 84672, 84672).
    // NV12 contains something like grayscale followed by the two color components.
  }

  // Access NVIDIA metadata:
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(frame->buffer);
  int meta_frame = 0;

  for (NvDsMetaList *l_frame = batch_meta->frame_meta_list; l_frame != nullptr;
       l_frame = l_frame->next) {
    const NvDsFrameMeta & frame_meta = * ((NvDsFrameMeta *) (l_frame->data));
    g_print("    Plugin: Frame %d\n", meta_frame++);
    for (NvDsMetaList * l_obj = frame_meta.obj_meta_list; l_obj != nullptr;
         l_obj = l_obj->next) {
      const NvDsObjectMeta & obj_meta = *((NvDsObjectMeta *) (l_obj->data));
      std::string name = obj_meta.class_id == 2? "Person":"";
      g_print("        Plugin: Rect at %d %d (%d x %d) %s\n",
        obj_meta.rect_params.top, obj_meta.rect_params.left,
        obj_meta.rect_params.width,
        obj_meta.rect_params.height,
        name.c_str());
    }
  }

  guint8 *pixels = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0); // Pointer to pixels

  // Draw the white cross moving right and down over the time.
  int y = frame_no % height;
  int x = frame_no % width;

  // Draw horizontal line with memset
  memset(pixels + y * stride, 0xFF, stride);

  // Draw vertical line
  for (gsize p = x * pixel_stride; p < size; p += stride) {
    pixels[p] = 0xFF;
  }

  gst_buffer_unmap(frame->buffer, &info);
  return GST_FLOW_OK;
}

static double measure_fps(GstNVideo *filter, guint count) {
  static double fps = 0;
  static struct timeval tp;
  if ((count & 0xF) == 0) { // Reset every 16 frames
    filter->probe = 0;
    gettimeofday(&tp, nullptr);
    filter->started = (double) tp.tv_sec + tp.tv_usec / 1000000.0;
  }
  if (filter->probe > 3) { // Start updating after 3 samples
    gettimeofday(&tp, nullptr);
    double ms = (double) tp.tv_sec + tp.tv_usec / 1000000.0;
    fps = filter->probe / (ms - filter->started);
  }
  filter->probe++;
  return fps;
}

/* Initialize the class of the plugin */
static void gst_n_video_class_init(GstNVideoClass *klass) {
  g_print("My Video have initialized its class\n");

  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (klass);
  gobject_class->set_property = gst_n_video_set_property;
  gobject_class->get_property = gst_n_video_get_property;

  g_object_class_install_property(gobject_class, PROP_SILENT,
                                  g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
                                                       false, G_PARAM_READWRITE));

  gst_element_class_set_details_simple(GST_ELEMENT_CLASS (klass),
                                       "NVideo",
                                       "Lab plugin",
                                       "GStreamer Lab plugin",
                                       "audrius<audrius@deep-impact.com>");

  gst_element_class_add_pad_template(GST_ELEMENT_CLASS (klass),
                                     gst_pad_template_new("src", GST_PAD_SRC,
                                                          GST_PAD_ALWAYS,
                                                          gst_caps_from_string(VIDEO_SRC_CAPS)));
  gst_element_class_add_pad_template(GST_ELEMENT_CLASS (klass),
                                     gst_pad_template_new("sink", GST_PAD_SINK,
                                                          GST_PAD_ALWAYS,
                                                          gst_caps_from_string(VIDEO_SINK_CAPS)));

  video_filter_class->transform_frame_ip = gst_n_video_transform_frame_ip;
}

/* Initialize he plugin */
static void gst_n_video_init(GstNVideo *filter) {
  g_print("I have initialized my instance\n");
  filter->silent = false;
  filter->count = filter->probe = 0;
}

/* Some property setter */
static void gst_n_video_set_property(GObject *object, guint prop_id,
                                     const GValue *value, GParamSpec *pspec) {
  GstNVideo *filter = GST_NVIDEO (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean(value);
      g_print("I have set my property to %d\n", filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Some property getter */
static void gst_n_video_get_property(GObject *object, guint prop_id,
                                     GValue *value, GParamSpec *pspec) {
  GstNVideo *filter = GST_NVIDEO (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_print("I have been asked about my property and would say %d\n", filter->silent);
      g_value_set_boolean(value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* This function handles sink events */
static gboolean gst_n_video_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
  g_print("Received %s event", GST_EVENT_TYPE_NAME(event));

  GstNVideo *filter = GST_NVIDEO (parent);
  gboolean ret;

  GST_LOG_OBJECT (filter, "Received %s event: %"
      GST_PTR_FORMAT,
                  GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS: {
      GstCaps *caps;

      gst_event_parse_caps(event, &caps);
      /* do something with the caps */

      /* and forward */
      ret = gst_pad_event_default(pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default(pad, parent, event);
      break;
  }
  return ret;
}

/* Entry point to initialize the plug-in */
static gboolean nvideo_init(GstPlugin *nvideo) {
  GST_DEBUG_CATEGORY_INIT (gst_n_video_debug, DI_PLUGIN_NAME,
                           0, "Template nvideo");
  g_print("N-Video plugin initialized\n");
  return gst_element_register(nvideo, DI_PLUGIN_NAME, GST_RANK_NONE,
                              GST_TYPE_NVIDEO);
}

/*
 * Gstreamer looks for this structure to register myvideo plugin.
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvideo,
    "GStreamer Lab Plugin",
    nvideo_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)