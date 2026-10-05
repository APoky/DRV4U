#include <gst/gst.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE_PATH "/dev/video1"

static int v4l2_fd = -1;

/*
 * Callback function executed when GStreamer's appsink has a new video frame (sample).
 * This function extracts the raw frame data and writes it to the V4L2 device node.
 */
static GstFlowReturn new_sample_callback(GstElement *sink, gpointer data) {
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo map;
    ssize_t written_bytes;

    // Pull the sample from the sink
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (!sample) {
        return GST_FLOW_ERROR;
    }

    // Get the buffer and map it to access raw data
    buffer = gst_sample_get_buffer(sample);
    gst_buffer_map(buffer, &map, GST_MAP_READ);

    if (v4l2_fd != -1) {
        // Write the raw frame data to the V4L2 device file
        written_bytes = write(v4l2_fd, map.data, map.size);
        if (written_bytes == -1) {
            perror("Failed to write to V4L2 device");
        }
    }

    // Unmap the buffer and free the sample
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

int main(int argc, char *argv[]) {
    GstElement *pipeline, *source, *sink, *capsfilter;
    GstBus *bus;
    GstCaps *caps;

    // Initialize GStreamer
    gst_init(&argc, &argv);

    // Open the V4L2 device file for writing
    v4l2_fd = open(DEVICE_PATH, O_RDWR);
    if (v4l2_fd == -1) {
        perror("Failed to open V4L2 device");
        return 1;
    }

    // Build the GStreamer pipeline: videotestsrc ! capsfilter ! appsink
    pipeline = gst_pipeline_new("testsrc-pipeline");
    source = gst_element_factory_make("videotestsrc", "test-source");
    capsfilter = gst_element_factory_make("capsfilter", "filter");
    sink = gst_element_factory_make("appsink", "app-sink");

    if (!pipeline || !source || !capsfilter || !sink) {
        g_printerr("One of the elements could not be created. Exiting.\n");
        close(v4l2_fd);
        return -1;
    }
    
    // Set the output format to match the AR0330 driver's expectation (YUYV, 1920x1080)
    caps = gst_caps_new_simple("video/x-raw",
                               "format", G_TYPE_STRING, "YUY2", // YUY2 is often the 4-char code for V4L2_PIX_FMT_YUYV
                               "width", G_TYPE_INT, 1920,
                               "height", G_TYPE_INT, 1080,
                               NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);


    // Configure the appsink
    g_object_set(sink, "emit-signals", TRUE,     // Important: needed for new_sample_callback
                       "sync", FALSE,            // Don't synchronize to clock (for speed)
                       "max-buffers", 1,         // Only queue one frame at a time
                       NULL);
    g_signal_connect(sink, "new-sample", G_CALLBACK(new_sample_callback), NULL);

    // Add elements to the pipeline and link them
    gst_bin_add_many(GST_BIN(pipeline), source, capsfilter, sink, NULL);
    if (!gst_element_link_many(source, capsfilter, sink, NULL)) {
        g_printerr("Elements could not be linked. Exiting.\n");
        close(v4l2_fd);
        return -1;
    }

    // Start the pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Test Source Simulator started, writing 1920x1080 YUYV frames to %s. Press Ctrl+C to stop.\n", DEVICE_PATH);

    // Run the main GStreamer loop
    bus = gst_element_get_bus(pipeline);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    // Cleanup
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(bus));
    gst_object_unref(GST_OBJECT(pipeline));
    g_main_loop_unref(loop);
    close(v4l2_fd);

    return 0;
}

