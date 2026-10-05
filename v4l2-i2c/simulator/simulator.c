#include <gst/gst.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE_PATH "/dev/video1"
#define FILE_PATH "test.h264" // Provide your own H.264 file here

static int v4l2_fd = -1;

static GstFlowReturn new_sample_callback(GstElement *sink, gpointer data) {
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo map;
    ssize_t written_bytes;

    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (!sample) {
        return GST_FLOW_ERROR;
    }

    buffer = gst_sample_get_buffer(sample);
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    
    if (v4l2_fd != -1) {
        written_bytes = write(v4l2_fd, map.data, map.size);
        if (written_bytes == -1) {
            perror("Failed to write to V4L2 device");
        }
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

static void on_pad_added(GstElement *decodebin, GstPad *pad, gpointer data) {
    GstElement *app_sink = (GstElement *)data;
    GstPad *app_sink_pad = gst_element_get_static_pad(app_sink, "sink");

    if (gst_pad_link(pad, app_sink_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link pads!\n");
    }

    gst_object_unref(app_sink_pad);
}

int main(int argc, char *argv[]) {
    GstElement *pipeline, *source, *decoder, *sink;
    GstBus *bus;

    gst_init(&argc, &argv);

    v4l2_fd = open(DEVICE_PATH, O_RDWR);
    if (v4l2_fd == -1) {
        perror("Failed to open V4L2 device");
        return 1;
    }

    pipeline = gst_pipeline_new("video-pipeline");
    source = gst_element_factory_make("filesrc", "file-source");
    decoder = gst_element_factory_make("decodebin", "video-decoder");
    sink = gst_element_factory_make("appsink", "app-sink");

    if (!pipeline || !source || !decoder || !sink) {
        g_printerr("One or more elements could not be created. Exiting.\n");
        return -1;
    }

    g_object_set(source, "location", FILE_PATH, NULL);
    g_object_set(sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(sink, "new-sample", G_CALLBACK(new_sample_callback), NULL);
    g_signal_connect(decoder, "pad-added", G_CALLBACK(on_pad_added), sink);

    gst_bin_add_many(GST_BIN(pipeline), source, decoder, sink, NULL);
    if (!gst_element_link(source, decoder)) {
        g_printerr("Source and decoder could not be linked. Exiting.\n");
        return -1;
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Simulator started from file, writing to %s. Press Ctrl+C to stop.\n", DEVICE_PATH);

    bus = gst_element_get_bus(pipeline);
    gst_bus_add_signal_watch(bus);
    gst_bus_add_watch(bus, NULL, NULL);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(bus));
    gst_object_unref(GST_OBJECT(pipeline));
    close(v4l2_fd);

    return 0;
}
