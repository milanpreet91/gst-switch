/* GstSwitch
 * Copyright (C) 2012,2013 Duzy Chan <code@duzy.info>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <gst/gst.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include "../tools/gstswitchclient.h"
#include "../logutils.h"

gboolean verbose = FALSE;

enum {
#if ENABLE_LOW_RESOLUTION
  W = LOW_RES_AW, H = LOW_RES_AH,
#else
  W = 1280, H = 720,
#endif
};

static struct {
  gboolean disable_test_controller;
  gboolean disable_test_video;
  gboolean disable_test_audio;
  gboolean disable_test_ui_integration;
  gboolean disable_test_random_connection;
  gboolean disable_test_switching;
  gboolean disable_test_fuzz;
  gboolean disable_test_checking_timestamps;
  gboolean test_external_server;
  gboolean test_external_ui;
} opts = {
  .disable_test_controller		= FALSE,
  .disable_test_video			= FALSE,
  .disable_test_audio			= FALSE,
  .disable_test_ui_integration		= FALSE,
  .disable_test_random_connection	= FALSE,
  .disable_test_switching		= FALSE,
  .disable_test_fuzz			= FALSE,
  .disable_test_checking_timestamps	= FALSE,
  .test_external_server			= FALSE,
  .test_external_ui			= FALSE,
};

static GOptionEntry option_entries[] = {
  {"disable-test-controller",		0, 0, G_OPTION_ARG_NONE, &opts.disable_test_controller,		"Disable testing controller",        NULL},
  {"disable-test-video",		0, 0, G_OPTION_ARG_NONE, &opts.disable_test_video,		"Disable testing video",             NULL},
  {"disable-test-audio",		0, 0, G_OPTION_ARG_NONE, &opts.disable_test_audio,		"Disable testing audio",             NULL},
  {"disable-test-ui-integration",	0, 0, G_OPTION_ARG_NONE, &opts.disable_test_ui_integration,	"Disable testing UI integration",    NULL},
  {"disable-test-random-connection",	0, 0, G_OPTION_ARG_NONE, &opts.disable_test_random_connection,	"Disable testing random connection", NULL},
  {"disable-test-switching",		0, 0, G_OPTION_ARG_NONE, &opts.disable_test_switching,		"Disable testing switching",         NULL},
  {"disable-test-fuzz-ui",		0, 0, G_OPTION_ARG_NONE, &opts.disable_test_fuzz,		"Disable testing fuzz input",        NULL},
  {"disable-test-checking-timestamps",	0, 0, G_OPTION_ARG_NONE, &opts.disable_test_checking_timestamps,"Disable testing checking timestamps", NULL},
  {"test-external-server",		0, 0, G_OPTION_ARG_NONE, &opts.test_external_server,		"Testing external server",           NULL},
  {"test-external-ui",			0, 0, G_OPTION_ARG_NONE, &opts.test_external_ui,		"Testing external ui",               NULL},
  {NULL}
};

typedef struct _testcase testcase;
struct _testcase
{
  const gchar *name;
  GMainLoop *mainloop;
  GstElement *pipeline;
  GMutex lock;
  GThread *thread;
  GString *desc;
  gint timer;
  gint live_seconds;
  gint error_count;
};

static void
testcase_quit (testcase *t)
{
  gst_element_set_state (t->pipeline, GST_STATE_NULL);
  g_main_loop_quit (t->mainloop);
}

static void
testcase_fail (testcase *t)
{
  testcase_quit (t);
  g_test_fail ();
}

static void
testcase_ok (testcase *t)
{
  testcase_quit (t);
}

static void
testcase_state_change (testcase *t, GstState oldstate, GstState newstate, GstState pending)
{
  GstStateChange statechange = GST_STATE_TRANSITION (oldstate, newstate);
  switch (statechange) {
  case GST_STATE_CHANGE_NULL_TO_READY:
    gst_element_set_state (t->pipeline, GST_STATE_PAUSED);
    break;
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    gst_element_set_state (t->pipeline, GST_STATE_PLAYING);
    break;
  case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    break;
  case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    break;
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    break;    
  case GST_STATE_CHANGE_READY_TO_NULL:
    /*
    INFO ("quit: %s\n", t->name);
    */
    testcase_ok (t);
    break;
  default:
    break;
  }
}

static void
testcase_error_message (testcase *t, GError *error, const gchar *info)
{
  /*
  ERROR ("%s: %s", t->name, error->message);
  */

  /*
  g_print ("%s\n", info);
  */

  t->error_count += 1;
}

static gboolean
testcase_pipeline_message (GstBus * bus, GstMessage * message, gpointer data)
{
  testcase *t = (testcase *) data;
  switch (GST_MESSAGE_TYPE (message)) {
  case GST_MESSAGE_STATE_CHANGED:
  {
    if (GST_ELEMENT (message->src) == t->pipeline) {
      GstState oldstate, newstate, pending;
      gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
      testcase_state_change (t, oldstate, newstate, pending);
    }
  } break;
  case GST_MESSAGE_ERROR:
  {
    GError *error = NULL;
    gchar *info = NULL;
    gst_message_parse_error (message, &error, &info);
    testcase_error_message (t, error, info);
    g_error_free (error);
    g_free (info);
  } break;
  case GST_MESSAGE_EOS:
  {
    gst_element_set_state (t->pipeline, GST_STATE_NULL);
    testcase_ok (t);
  } break;
  default:
  {
    /*
    INFO ("%s: %s", GST_OBJECT_NAME (message->src), GST_MESSAGE_TYPE_NAME (message));
    */
  } break;
  }
  return TRUE;
}

static gboolean
testcase_launch_pipeline (testcase *t)
{
  GError *error = NULL;
  GstBus *bus;

  if (!t->desc) {
    ERROR ("invalid test case");
    testcase_fail (t);
    return FALSE;
  }

  t->pipeline = (GstElement *) gst_parse_launch (t->desc->str, &error);

  if (error) {
    ERROR ("%s: %s", t->name, error->message);
    testcase_fail (t);
    return FALSE;
  }

  gst_pipeline_set_auto_flush_bus (GST_PIPELINE (t->pipeline), FALSE);
  bus = gst_pipeline_get_bus (GST_PIPELINE (t->pipeline));
  gst_bus_add_watch (bus, testcase_pipeline_message, t);
  gst_element_set_state (t->pipeline, GST_STATE_READY);
  return TRUE;
}

static void
child_quit (GPid pid, gint status, gpointer data)
{
  GOutputStream *ostream = G_OUTPUT_STREAM (data);
  GError *error = NULL;

  INFO ("quit %d", pid);

  g_output_stream_flush (ostream, NULL, &error);
  g_assert_no_error (error);
  g_output_stream_close (ostream, NULL, &error);
  g_assert_no_error (error);
  g_object_unref (ostream);
}

static void
child_stdout (GIOChannel *channel, GIOCondition condition, gpointer data)
{
  char buf[1024];
  GError *error = NULL;
  gsize bytes_read;
  GOutputStream *ostream = G_OUTPUT_STREAM (data);

  //INFO ("out");

  if (condition & G_IO_IN) {
    gsize bytes_written;
    GIOStatus status = g_io_channel_read_chars (channel, buf, sizeof (buf), &bytes_read, &error);
    g_assert_no_error (error);
    g_output_stream_write_all (ostream, buf, bytes_read, &bytes_written, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpint (bytes_read, ==, bytes_written);
    (void) status;
  }
  if (condition & G_IO_HUP) {
    g_output_stream_flush (ostream, NULL, &error);
    g_assert_no_error (error);
  }
}

static void
child_stderr (GIOChannel *channel, GIOCondition condition, gpointer data)
{
  //INFO ("err");
}

static GPid
launch (const gchar *name, ...)
{
  GPid pid = 0;
  GError *error = NULL;
  GMainContext *context;
  GIOChannel *channel;
  GSource *source;
  gint fd_in, fd_out, fd_err;
  GFileOutputStream *outstream;
  GFile *outfile;
  gchar *outfilename;
  gchar **argv;
  gboolean ok;
  GPtrArray *array = g_ptr_array_new ();
  const gchar *arg;
  va_list va;
  va_start (va, name);
  for (arg = name; arg; arg = va_arg (va, const gchar *))
    g_ptr_array_add (array, g_strdup (arg));
  va_end (va);
  g_ptr_array_add (array, NULL);
  argv = (gchar **) g_ptr_array_free (array, FALSE);

  ok = g_spawn_async_with_pipes (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
      NULL, NULL, &pid, &fd_in, &fd_out, &fd_err, &error);

  g_free (argv);

  g_assert_no_error (error);
  g_assert (ok);

  context = g_main_context_default ();

  outfilename = g_strdup_printf ("test-server-%d.log", pid);
  outfile = g_file_new_for_path (outfilename);
  outstream = g_file_create (outfile, G_FILE_CREATE_NONE, NULL, &error);

  g_free (outfilename);
  g_assert_no_error (error);
  g_assert (outfile);
  g_assert (outstream);

  channel = g_io_channel_unix_new (fd_out);
  source = g_io_create_watch (channel, G_IO_IN | G_IO_HUP | G_IO_ERR);
  g_source_set_callback (source, (GSourceFunc) child_stdout, outstream, NULL);
  g_source_attach (source, context);
  g_source_unref (source);
  g_io_channel_unref (channel);

  (void) child_stderr;
  /*
  channel = g_io_channel_unix_new (fd_err);
  source = g_io_create_watch (channel, G_IO_IN | G_IO_HUP | G_IO_ERR);
  g_source_set_callback (source, (GSourceFunc) child_stderr, NULL, NULL);
  g_source_attach (source, context);
  g_source_unref (source);
  g_io_channel_unref (channel);
  */

  g_child_watch_add (pid, child_quit, outstream);

  //g_main_context_unref (context);
  return pid;
}

static GPid
launch_server ()
{
  GPid pid = launch ("./tools/gst-switch-srv", "-v",
      "--gst-debug-no-color",
      "--record=test-recording.data",
      NULL);
  INFO ("server %d", pid);
  return pid;
}

static GPid
launch_ui ()
{
  GPid pid = launch ("./tools/gst-switch-ui", "-v",
      "--gst-debug-no-color",
      NULL);
  INFO ("ui %d", pid);
  return pid;
}

static void
close_pid (GPid pid)
{
  //kill (pid, SIGKILL);
  kill (pid, SIGTERM);
  g_spawn_close_pid (pid);
  sleep (2); /* give a second for cleaning up */
}

static gboolean
testcase_second_timer (testcase *t)
{
  t->live_seconds -= 1;

  if (t->live_seconds == 0) {
    testcase_ok (t);
  }

  return 0 < t->live_seconds ? TRUE : FALSE;
}

static gpointer
testcase_run (testcase *t)
{
  g_mutex_init (&t->lock);

  g_print ("========== %s\n", t->name);
  t->mainloop = g_main_loop_new (NULL, TRUE);
  if (testcase_launch_pipeline (t)) {
    if (0 < t->live_seconds) {
      t->timer = g_timeout_add (1000, (GSourceFunc) testcase_second_timer, t);
    }
    g_main_loop_run (t->mainloop);
    gst_object_unref (t->pipeline);
  } else {
    ERROR ("launch failed");
    g_test_fail ();
  }
  //g_main_loop_unref (t->mainloop);
  g_string_free (t->desc, FALSE);
  if (t->timer) g_source_remove (t->timer);
  //t->mainloop = NULL;
  t->pipeline = NULL;
  t->desc = NULL;
  t->timer = 0;

  if (t->error_count) {
    ERROR ("%s: %d errors", t->name, t->error_count);
  }

  g_mutex_lock (&t->lock);
  g_thread_unref (t->thread);
  t->thread = NULL;
  g_mutex_unlock (&t->lock);
  return NULL;
}

static void
testcase_run_thread (testcase *t)
{
  t->thread = g_thread_new (t->name, (GThreadFunc) testcase_run, t);
}

static void
testcase_join (testcase *t)
{
  GThread *thread = NULL;
  g_mutex_lock (&t->lock);
  if (t->thread) {
    thread = t->thread;
    g_thread_ref (thread);
  }
  g_mutex_unlock (&t->lock);
  if (thread) g_thread_join (thread);
}

typedef struct _testclient {
  GstSwitchClient base;
  GThread *thread;
  GMainLoop *mainloop;
  gint audio_port0;
  gint audio_port;
  gint audio_port_count;
  gint compose_port0;
  gint compose_port;
  gint compose_port_count;
  gint encode_port0;
  gint encode_port;
  gint encode_port_count;
  gint preview_port_1;
  gint preview_port_2;
  gint preview_port_count;
} testclient;

typedef struct _testclientClass {
  GstSwitchClientClass baseclass;
} testclientClass;

GType testclient_get_type (void);

#define TYPE_TESTCLIENT (testclient_get_type ())
#define TESTCLIENT(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), TYPE_TESTCLIENT, testclient))
#define TESTCLIENTCLASS(class) (G_TYPE_CHECK_CLASS_CAST ((class), TYPE_TESTCLIENT, testclientClass))
G_DEFINE_TYPE (testclient, testclient, GST_TYPE_SWITCH_CLIENT);

static gint clientcount = 0;
static void
testclient_init (testclient *client)
{
  ++clientcount;
  client->mainloop = NULL;
  //INFO ("client init");
}

static void
testclient_finalize (testclient *client)
{
  --clientcount;
  //INFO ("client finalize");
}

static void
testclient_connection_closed (testclient *client, GError *error)
{
  INFO ("closed: %s", error ? error->message : "");
  g_main_loop_quit (client->mainloop);
}

static void
testclient_set_compose_port (testclient *client, gint port)
{
  INFO ("set-compose-port: %d", port);
  client->compose_port = port;
  client->compose_port_count += 1;
}

static void
testclient_set_audio_port (testclient *client, gint port)
{
  INFO ("set-audio-port: %d", port);
  client->audio_port = port;
  client->audio_port_count += 1;
}

static void
testclient_add_preview_port (testclient *client, gint port, gint type)
{
  INFO ("add-preview-port: %d, %d", port, type);
  client->preview_port_count += 1;
  switch (client->preview_port_count) {
  case 1: client->preview_port_1 = port; break;
  case 2: client->preview_port_2 = port; break;
  }
}

static void
testclient_class_init (testclientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstSwitchClientClass * client_class = GST_SWITCH_CLIENT_CLASS (klass);
  object_class->finalize = (GObjectFinalizeFunc) testclient_finalize;
  client_class->connection_closed = (GstSwitchClientConnectionClosedFunc)
    testclient_connection_closed;
  client_class->set_compose_port = (GstSwitchClientSetComposePortFunc)
    testclient_set_compose_port;
  client_class->set_audio_port = (GstSwitchClientSetAudioPortFunc)
    testclient_set_audio_port;
  client_class->add_preview_port = (GstSwitchClientAddPreviewPortFunc)
    testclient_add_preview_port;
}

static gpointer
testclient_run (gpointer data)
{
  testclient *client = (testclient *) data;
  gboolean connect_ok = FALSE;
  client->mainloop = g_main_loop_new (NULL, TRUE);

  connect_ok = gst_switch_client_connect (GST_SWITCH_CLIENT (client));
  g_assert (connect_ok);

  client->compose_port0 = gst_switch_client_get_compose_port (GST_SWITCH_CLIENT (client));
  client->encode_port0 = gst_switch_client_get_encode_port (GST_SWITCH_CLIENT (client));
  client->audio_port0 = gst_switch_client_get_audio_port (GST_SWITCH_CLIENT (client));
  g_assert_cmpint (client->compose_port0, ==, 3001);
  g_assert_cmpint (client->encode_port0, ==, 3002);
  //g_assert_cmpint (client->audio_port0, ==, 3004);

  g_main_loop_run (client->mainloop);
  //g_main_loop_unref (client->mainloop);
  return NULL;
}

static void
testclient_run_thread (testclient *client)
{
  client->thread = g_thread_new ("testclient", testclient_run, client);
}

static void
testclient_join (testclient *client)
{
  g_thread_join (client->thread);
  g_thread_unref (client->thread);
  client->thread = NULL;
}

static void
test_controller (void)
{
  GPid server_pid = 0;
  testclient *client;
  testcase video_source1 = { "test-video-source1", 0 };
  testcase audio_source1 = { "test-audio-source1", 0 };

  g_print ("\n");

  if (!opts.test_external_server) {
    server_pid = launch_server ();
    g_assert_cmpint (server_pid, !=, 0);
    sleep (1); /* give a second for server to be online */
  }

  client = TESTCLIENT (g_object_new (TYPE_TESTCLIENT, NULL));
  testclient_run_thread (client);
  g_assert_cmpint (clientcount, ==, 1);

  {
    {
      video_source1.live_seconds = 10;
      video_source1.desc = g_string_new ("");
      g_string_append_printf (video_source1.desc,"videotestsrc pattern=%d ", 0);
      g_string_append_printf (video_source1.desc, "! video/x-raw,width=%d,height=%d ", W, H);
      g_string_append_printf (video_source1.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
      g_string_append_printf (video_source1.desc, "! gdppay ! tcpclientsink port=3000 ");

      audio_source1.live_seconds = 10;
      audio_source1.desc = g_string_new ("");
      g_string_append_printf (audio_source1.desc, "audiotestsrc wave=%d ", 2);
      g_string_append_printf (audio_source1.desc, "! gdppay ! tcpclientsink port=4000");

      testcase_run_thread (&video_source1); sleep (1);
      testcase_run_thread (&audio_source1);
      testcase_join (&video_source1);
      testcase_join (&audio_source1);

      if (video_source1.error_count || audio_source1.error_count) {
	g_test_fail ();
      }

      g_assert_cmpint (client->compose_port, ==, 3001);
      g_assert_cmpint (client->compose_port, ==, client->compose_port0);
      g_assert_cmpint (client->compose_port_count, ==, 1);
      //g_assert_cmpint (client->encode_port0, ==, 3002);
      //g_assert_cmpint (client->encode_port, ==, client->encode_port0);
      g_assert_cmpint (client->audio_port, ==, 3004);
      //g_assert_cmpint (client->audio_port, ==, client->audio_port0);
      g_assert_cmpint (client->audio_port_count, ==, 1);
      g_assert_cmpint (client->preview_port_1, ==, 3003);
      g_assert_cmpint (client->preview_port_2, ==, 3004);
      g_assert_cmpint (client->preview_port_count, ==, 2);
    }
  }

  if (!opts.test_external_server) {
    close_pid (server_pid);
    {
      testcase play = { "play-test-record", 0 };
      GFile *file = g_file_new_for_path ("test-recording.data");
      g_assert (g_file_query_exists (file, NULL));
      play.desc = g_string_new ("filesrc location=test-recording.data ");
      g_string_append_printf (play.desc, "! avidemux name=dm ");
      g_string_append_printf (play.desc, "dm.audio_0 ! queue ! faad ! audioconvert ! alsasink ");
      g_string_append_printf (play.desc, "dm.video_0 ! queue ! vp8dec ! videoconvert ! xvimagesink ");
      testcase_run_thread (&play);
      testcase_join (&play);
      g_assert_cmpint (play.error_count, ==, 0);
      g_object_unref (file);
    }
  }

  testclient_join (client);
  g_object_unref (client);
  g_assert_cmpint (clientcount, ==, 0);
}

static void
test_video (void)
{
  const gint seconds = 10;
  GPid server_pid = 0;
  testcase source1 = { "test-video-source1", 0 };
  testcase source2 = { "test-video-source2", 0 };
  testcase source3 = { "test-video-source3", 0 };
  testcase sink0 = { "test_video_compose_sink", 0 };
  testcase sink1 = { "test_video_preview_sink1", 0 };
  testcase sink2 = { "test_video_preview_sink2", 0 };
  testcase sink3 = { "test_video_preview_sink3", 0 };
  const gchar *textoverlay = "textoverlay "
    "font-desc=\"Sans 80\" "
    "auto-resize=true "
    "shaded-background=true "
    ;

  g_print ("\n");
  g_assert (!source1.thread);
  g_assert (!source2.thread);
  g_assert (!source3.thread);
  g_assert (!sink0.thread);
  g_assert (!sink1.thread);
  g_assert (!sink2.thread);
  g_assert (!sink3.thread);

  source1.live_seconds = seconds;
  source1.desc = g_string_new ("videotestsrc pattern=0 ");
  g_string_append_printf (source1.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (source1.desc, "! %s text=source1 ", textoverlay);
  g_string_append_printf (source1.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
  g_string_append_printf (source1.desc, "! gdppay ! tcpclientsink port=3000 ");

  source2.live_seconds = seconds;
  source2.desc = g_string_new ("videotestsrc pattern=1 ");
  g_string_append_printf (source2.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (source2.desc, "! %s text=source2 ", textoverlay);
  g_string_append_printf (source2.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
  g_string_append_printf (source2.desc, "! gdppay ! tcpclientsink port=3000 ");

  source3.live_seconds = seconds;
  source3.desc = g_string_new ("videotestsrc pattern=15 ");
  g_string_append_printf (source3.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (source3.desc, "! %s text=source3 ", textoverlay);
  g_string_append_printf (source3.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
  g_string_append_printf (source3.desc, "! gdppay ! tcpclientsink port=3000 ");

  sink0.live_seconds = seconds;
  sink0.desc = g_string_new ("tcpclientsrc port=3001 ");
  g_string_append_printf (sink0.desc, "! gdpdepay ");
  g_string_append_printf (sink0.desc, "! videoconvert ");
  g_string_append_printf (sink0.desc, "! xvimagesink");

  sink1.live_seconds = seconds;
  sink1.desc = g_string_new ("tcpclientsrc port=3003 ");
  g_string_append_printf (sink1.desc, "! gdpdepay ");
  g_string_append_printf (sink1.desc, "! videoconvert ");
  g_string_append_printf (sink1.desc, "! xvimagesink");

  sink2.live_seconds = seconds;
  sink2.desc = g_string_new ("tcpclientsrc port=3004 ");
  g_string_append_printf (sink2.desc, "! gdpdepay ");
  g_string_append_printf (sink2.desc, "! videoconvert ");
  g_string_append_printf (sink2.desc, "! xvimagesink");

  sink3.live_seconds = seconds;
  sink3.desc = g_string_new ("tcpclientsrc port=3005 ");
  g_string_append_printf (sink3.desc, "! gdpdepay ");
  g_string_append_printf (sink3.desc, "! videoconvert ");
  g_string_append_printf (sink3.desc, "! xvimagesink");

  if (!opts.test_external_server) {
    server_pid = launch_server ();
    g_assert_cmpint (server_pid, !=, 0);
    sleep (2); /* give a second for server to be online */
  }

  testcase_run_thread (&source1);
  sleep (1); /* give a second for source1 to be online */
  testcase_run_thread (&source2);
  testcase_run_thread (&source3);
  sleep (1); /* give a second for sources to be online */
  testcase_run_thread (&sink0);
  testcase_run_thread (&sink1);
  testcase_run_thread (&sink2);
  testcase_run_thread (&sink3);
  testcase_join (&source1);
  testcase_join (&source2);
  testcase_join (&source3);
  testcase_join (&sink0);
  testcase_join (&sink1);
  testcase_join (&sink2);
  testcase_join (&sink3);

  if (!opts.test_external_server)
    close_pid (server_pid);

  g_assert_cmpstr (source1.name, ==, "test-video-source1");
  g_assert_cmpint (source1.timer, ==, 0);
  g_assert (source1.desc == NULL);
  g_assert (source1.mainloop == NULL);
  g_assert (source1.pipeline == NULL);

  g_assert_cmpstr (source2.name, ==, "test-video-source2");
  g_assert_cmpint (source2.timer, ==, 0);
  g_assert (source2.desc == NULL);
  g_assert (source2.mainloop == NULL);
  g_assert (source2.pipeline == NULL);

  g_assert_cmpstr (source3.name, ==, "test-video-source3");
  g_assert_cmpint (source3.timer, ==, 0);
  g_assert (source3.desc == NULL);
  g_assert (source3.mainloop == NULL);
  g_assert (source3.pipeline == NULL);

  g_assert_cmpstr (sink0.name, ==, "test_video_compose_sink");
  g_assert_cmpint (sink0.timer, ==, 0);
  g_assert (sink0.desc == NULL);
  g_assert (sink0.mainloop == NULL);
  g_assert (sink0.pipeline == NULL);

  g_assert_cmpstr (sink1.name, ==, "test_video_preview_sink1");
  g_assert_cmpint (sink1.timer, ==, 0);
  g_assert (sink1.desc == NULL);
  g_assert (sink1.mainloop == NULL);
  g_assert (sink1.pipeline == NULL);

  g_assert_cmpstr (sink2.name, ==, "test_video_preview_sink2");
  g_assert_cmpint (sink2.timer, ==, 0);
  g_assert (sink2.desc == NULL);
  g_assert (sink2.mainloop == NULL);
  g_assert (sink2.pipeline == NULL);

  g_assert_cmpstr (sink3.name, ==, "test_video_preview_sink3");
  g_assert_cmpint (sink3.timer, ==, 0);
  g_assert (sink3.desc == NULL);
  g_assert (sink3.mainloop == NULL);
  g_assert (sink3.pipeline == NULL);

  if (!opts.test_external_server) {
    GFile *file = g_file_new_for_path ("test-recording.data");
    g_assert (g_file_query_exists (file, NULL));
    g_object_unref (file);
  }
}

static void
test_video_recording_result (void)
{
  g_print ("\n");
  if (!opts.test_external_server) {
    GFile *file = g_file_new_for_path ("test-recording.data");
    GError *error = NULL;
    g_assert (g_file_query_exists (file, NULL));
    g_assert (g_file_delete (file, NULL, &error));
    g_assert (error == NULL);
    g_assert (!g_file_query_exists (file, NULL));
    g_object_unref (file);
  }
}

static void
test_audio (void)
{
  const gint seconds = 20;
  testcase source1 = { "test-audio-source1", 0 };
  testcase source2 = { "test-audio-source2", 0 };
  testcase source3 = { "test-audio-source3", 0 };
  testcase sink1 = { "test_audio_preview_sink1", 0 };
  testcase sink2 = { "test_audio_preview_sink2", 0 };
  testcase sink3 = { "test_audio_preview_sink3", 0 };
  GPid server_pid = 0;
  const gchar *textoverlay = "textoverlay "
    "font-desc=\"Sans 80\" "
    "auto-resize=true "
    "shaded-background=true "
    ;

  g_print ("\n");
  g_assert (!source1.thread);
  g_assert (!source2.thread);
  g_assert (!source3.thread);
  g_assert (!sink1.thread);
  g_assert (!sink2.thread);
  g_assert (!sink3.thread);

  source1.live_seconds = seconds;
  source1.desc = g_string_new ("audiotestsrc wave=2 ");
  g_string_append_printf (source1.desc, "! gdppay ! tcpclientsink port=4000");

  source2.live_seconds = seconds;
  source2.desc = g_string_new ("audiotestsrc wave=2 ");
  g_string_append_printf (source2.desc, "! gdppay ! tcpclientsink port=4000");

  source3.live_seconds = seconds;
  source3.desc = g_string_new ("audiotestsrc wave=2 ");
  g_string_append_printf (source3.desc, "! gdppay ! tcpclientsink port=4000");

  sink1.live_seconds = seconds;
  sink1.desc = g_string_new ("tcpclientsrc port=3003 ");
  g_string_append_printf (sink1.desc, "! gdpdepay ! faad ! goom2k1 ");
  g_string_append_printf (sink1.desc, "! %s text=audio1 ", textoverlay);
  g_string_append_printf (sink1.desc, "! videoconvert ");
  g_string_append_printf (sink1.desc, "! xvimagesink");

  sink2.live_seconds = seconds;
  sink2.desc = g_string_new ("tcpclientsrc port=3004 ");
  g_string_append_printf (sink2.desc, "! gdpdepay ! faad ! goom2k1 ");
  g_string_append_printf (sink2.desc, "! %s text=audio2 ", textoverlay);
  g_string_append_printf (sink2.desc, "! videoconvert ");
  g_string_append_printf (sink2.desc, "! xvimagesink");

  sink3.live_seconds = seconds;
  sink3.desc = g_string_new ("tcpclientsrc port=3005 ");
  g_string_append_printf (sink3.desc, "! gdpdepay ! faad ! goom2k1 ");
  g_string_append_printf (sink3.desc, "! %s text=audio3 ", textoverlay);
  g_string_append_printf (sink3.desc, "! videoconvert ");
  g_string_append_printf (sink3.desc, "! xvimagesink");

  if (!opts.test_external_server) {
    server_pid = launch_server ();
    g_assert_cmpint (server_pid, !=, 0);
    sleep (3); /* give a second for server to be online */
  }

  testcase_run_thread (&source1);
  testcase_run_thread (&source2);
  testcase_run_thread (&source3);
  sleep (2); /* give a second for audios to be online */
  if (!opts.test_external_ui) {
    testcase_run_thread (&sink1);
    testcase_run_thread (&sink2);
    testcase_run_thread (&sink3);
  }
  testcase_join (&source1);
  testcase_join (&source2);
  testcase_join (&source3);
  if (!opts.test_external_ui) {
    testcase_join (&sink1);
    testcase_join (&sink2);
    testcase_join (&sink3);
  }

  if (!opts.test_external_server)
    close_pid (server_pid);

  g_assert_cmpint (source1.timer, ==, 0);
  g_assert (source1.desc == NULL);
  g_assert (source1.pipeline == NULL);

  g_assert_cmpint (source2.timer, ==, 0);
  g_assert (source2.desc == NULL);
  g_assert (source2.pipeline == NULL);

  g_assert_cmpint (source3.timer, ==, 0);
  g_assert (source3.desc == NULL);
  g_assert (source3.pipeline == NULL);

  if (!opts.test_external_server) {
    GFile *file = g_file_new_for_path ("test-recording.data");
    g_assert (g_file_query_exists (file, NULL));
    g_object_unref (file);
  }
}

static void
test_audio_recording_result (void)
{
  g_print ("\n");
  if (!opts.test_external_server) {
    GFile *file = g_file_new_for_path ("test-recording.data");
    GError *error = NULL;
    g_assert (g_file_query_exists (file, NULL));
    g_assert (g_file_delete (file, NULL, &error));
    g_assert (error == NULL);
    g_assert (!g_file_query_exists (file, NULL));
    g_object_unref (file);
  }
}

static void
test_ui_integrated (void)
{
  const gint seconds = 10;
  GPid server_pid = 0;
  GPid ui_pid = 0;
  testcase video_source1 = { "test-video-source1", 0 };
  testcase video_source2 = { "test-video-source2", 0 };
  testcase video_source3 = { "test-video-source3", 0 };
  testcase audio_source1 = { "test-audio-source1", 0 };
  testcase audio_source2 = { "test-audio-source2", 0 };
  testcase audio_source3 = { "test-audio-source3", 0 };
  const gchar *textoverlay = "textoverlay "
    "font-desc=\"Sans 80\" "
    "auto-resize=true "
    "shaded-background=true "
    ;

  g_print ("\n");

  video_source1.live_seconds = seconds;
  video_source1.desc = g_string_new ("videotestsrc pattern=0 ");
  g_string_append_printf (video_source1.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (video_source1.desc, "! %s text=video1 ", textoverlay);
  g_string_append_printf (video_source1.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
  g_string_append_printf (video_source1.desc, "! gdppay ! tcpclientsink port=3000 ");

  video_source2.live_seconds = seconds;
  video_source2.desc = g_string_new ("videotestsrc pattern=1 ");
  g_string_append_printf (video_source2.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (video_source2.desc, "! %s text=video2 ", textoverlay);
  g_string_append_printf (video_source2.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
  g_string_append_printf (video_source2.desc, "! gdppay ! tcpclientsink port=3000 ");

  video_source3.live_seconds = seconds;
  video_source3.desc = g_string_new ("videotestsrc pattern=15 ");
  g_string_append_printf (video_source3.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (video_source3.desc, "! %s text=video3 ", textoverlay);
  g_string_append_printf (video_source3.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
  g_string_append_printf (video_source3.desc, "! gdppay ! tcpclientsink port=3000 ");

  audio_source1.live_seconds = seconds;
  audio_source1.desc = g_string_new ("audiotestsrc ");
  //g_string_append_printf (audio_source1.desc, "! audio/x-raw ");
  g_string_append_printf (audio_source1.desc, "! gdppay ! tcpclientsink port=4000");

  audio_source2.live_seconds = seconds;
  audio_source2.desc = g_string_new ("audiotestsrc ");
  g_string_append_printf (audio_source2.desc, "! gdppay ! tcpclientsink port=4000");

  audio_source3.live_seconds = seconds;
  audio_source3.desc = g_string_new ("audiotestsrc ");
  g_string_append_printf (audio_source3.desc, "! gdppay ! tcpclientsink port=4000");

  if (!opts.test_external_server) {
    server_pid = launch_server ();
    g_assert_cmpint (server_pid, !=, 0);
    sleep (3); /* give a second for server to be online */
  }

  if (!opts.test_external_ui) {
    ui_pid = launch_ui ();
    g_assert_cmpint (ui_pid, !=, 0);
    sleep (2); /* give a second for ui to be ready */
  }

  testcase_run_thread (&video_source1); //sleep (1);
  testcase_run_thread (&video_source2); //sleep (1);
  testcase_run_thread (&video_source3); //sleep (1);
  testcase_run_thread (&audio_source1); //sleep (1);
  testcase_run_thread (&audio_source2); //sleep (1);
  testcase_run_thread (&audio_source3); //sleep (1);
  testcase_join (&video_source1);
  testcase_join (&video_source2);
  testcase_join (&video_source3);
  testcase_join (&audio_source1);
  testcase_join (&audio_source2);
  testcase_join (&audio_source3);

  if (!opts.test_external_ui)
    close_pid (ui_pid);
  if (!opts.test_external_server)
    close_pid (server_pid);
}

static void
test_recording_result (void)
{
  g_print ("\n");
  if (!opts.test_external_server) {
    GFile *file = g_file_new_for_path ("test-recording.data");
    GError *error = NULL;
    g_assert (g_file_query_exists (file, NULL));
    g_assert (g_file_delete (file, NULL, &error));
    g_assert (error == NULL);
    g_assert (!g_file_query_exists (file, NULL));
    g_object_unref (file);
  }
}

static gpointer
test_random_connection_1 (gpointer d)
{
  testcase video_source1 = { "test-video-source1", 0 };
  testcase audio_source0 = { "test-audio-source0", 0 };
  testcase audio_source1 = { "test-audio-source1", 0 };
  const gchar *textoverlay = "textoverlay "
    "font-desc=\"Sans 80\" "
    "auto-resize=true "
    "shaded-background=true "
    ;
  gint n, m, i;

  audio_source0.live_seconds = 102;
  audio_source0.desc = g_string_new ("");
  g_string_append_printf (audio_source0.desc, "audiotestsrc wave=2 ");
  g_string_append_printf (audio_source0.desc, "! gdppay ! tcpclientsink port=4000");
  testcase_run_thread (&audio_source0);
  sleep (2);

  for (i = m = 0; m < 3; ++m) {
    for (n = 0; n < 3; ++n, ++i) {
      video_source1.live_seconds = 5;
      video_source1.name = g_strdup_printf ("test-video-source1-%d", i);
      video_source1.desc = g_string_new ("");
      g_string_append_printf (video_source1.desc,"videotestsrc pattern=%d ", rand() % 20);
      g_string_append_printf (video_source1.desc, "! video/x-raw,width=%d,height=%d ", W, H);
      g_string_append_printf (video_source1.desc, "! %s text=video1-%d ", textoverlay, n);
      g_string_append_printf (video_source1.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
      g_string_append_printf (video_source1.desc, "! gdppay ! tcpclientsink port=3000 ");

      audio_source1.live_seconds = 7;
      audio_source1.name = g_strdup_printf ("test-audio-source1-%d", i);
      audio_source1.desc = g_string_new ("");
      g_string_append_printf (audio_source1.desc, "audiotestsrc wave=%d ", rand() % 12);
      g_string_append_printf (audio_source1.desc, "! gdppay ! tcpclientsink port=4000");

      testcase_run_thread (&video_source1);
      testcase_run_thread (&audio_source1);
      testcase_join (&video_source1);
      testcase_join (&audio_source1);

      g_free ((void*) video_source1.name);
      g_free ((void*) audio_source1.name);
    }
  }

  testcase_join (&audio_source0);
  return NULL;
}

static gpointer
test_random_connection_2 (gpointer d)
{
  testcase video_source1 = { "test-video-source1", 0 };
  testcase audio_source1 = { "test-audio-source1", 0 };
  const gchar *textoverlay = "textoverlay "
    "font-desc=\"Sans 80\" "
    "auto-resize=true "
    "shaded-background=true "
    ;
  gint n, m, i;

  g_print ("\n");

  for (i = m = 0; m < 3; ++m) {
    for (n = 0; n < 3; ++n, ++i) {
      video_source1.live_seconds = 2;
      video_source1.name = g_strdup_printf ("test-video-source2-%d", i);
      video_source1.desc = g_string_new ("");
      g_string_append_printf (video_source1.desc,"videotestsrc pattern=%d ", rand() % 20);
      g_string_append_printf (video_source1.desc, "! video/x-raw,width=%d,height=%d ", W, H);
      g_string_append_printf (video_source1.desc, "! %s text=video1-%d ", textoverlay, n);
      g_string_append_printf (video_source1.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
      g_string_append_printf (video_source1.desc, "! gdppay ! tcpclientsink port=3000 ");

      audio_source1.live_seconds = 3;
      audio_source1.name = g_strdup_printf ("test-audio-source2-%d", i);
      audio_source1.desc = g_string_new ("");
      g_string_append_printf (audio_source1.desc, "audiotestsrc wave=%d ", rand () % 12);
      g_string_append_printf (audio_source1.desc, "! gdppay ! tcpclientsink port=4000");

      testcase_run_thread (&video_source1);
      testcase_run_thread (&audio_source1);
      testcase_join (&video_source1);
      testcase_join (&audio_source1);

      g_free ((void*) video_source1.name);
      g_free ((void*) audio_source1.name);
    }
  }
  return NULL;
}

static void
test_random_connections (void)
{
  GPid server_pid = 0;
  GPid ui_pid = 0;
  GThread *t1, *t2;

  g_print ("\n");

  if (!opts.test_external_server) {
    server_pid = launch_server ();
    g_assert_cmpint (server_pid, !=, 0);
    sleep (2); /* give a second for server to be online */
  }

  if (!opts.test_external_ui) {
    ui_pid = launch_ui ();
    g_assert_cmpint (ui_pid, !=, 0);
    sleep (1); /* give a second for ui to be ready */
  }

  t1 = g_thread_new ("random-1", test_random_connection_1, NULL); sleep (1);
  t2 = g_thread_new ("random-2", test_random_connection_2, NULL);

  g_thread_join (t1);
  g_thread_join (t2);
  g_thread_unref(t1);
  g_thread_unref(t2);

  if (!opts.test_external_ui)
    close_pid (ui_pid);
  if (!opts.test_external_server)
    close_pid (server_pid);
}

static void
test_switching (void)
{
  const gint seconds = 180;
  GPid server_pid = 0;
  GPid ui_pid = 0;
  testcase video_source1 = { "test-video-source1", 0 };
  testcase video_source2 = { "test-video-source2", 0 };
  testcase video_source3 = { "test-video-source3", 0 };
  testcase audio_source1 = { "test-audio-source1", 0 };
  testcase audio_source2 = { "test-audio-source2", 0 };
  testcase audio_source3 = { "test-audio-source3", 0 };
  
  const gchar *textoverlay = "textoverlay "
    "font-desc=\"Sans 80\" "
    "auto-resize=true "
    "shaded-background=true "
    ;

  g_print ("\n");

  video_source1.live_seconds = seconds;
  video_source1.desc = g_string_new ("videotestsrc pattern=0 ");
  g_string_append_printf (video_source1.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (video_source1.desc, "! %s text=video1 ", textoverlay);
  g_string_append_printf (video_source1.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
  g_string_append_printf (video_source1.desc, "! gdppay ! tcpclientsink port=3000 ");

  video_source2.live_seconds = seconds;
  video_source2.desc = g_string_new ("videotestsrc pattern=1 ");
  g_string_append_printf (video_source2.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (video_source2.desc, "! %s text=video2 ", textoverlay);
  g_string_append_printf (video_source2.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
  g_string_append_printf (video_source2.desc, "! gdppay ! tcpclientsink port=3000 ");

  video_source3.live_seconds = seconds;
  video_source3.desc = g_string_new ("videotestsrc pattern=15 ");
  g_string_append_printf (video_source3.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (video_source3.desc, "! %s text=video3 ", textoverlay);
  g_string_append_printf (video_source3.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ");
  g_string_append_printf (video_source3.desc, "! gdppay ! tcpclientsink port=3000 ");

  audio_source1.live_seconds = seconds;
  audio_source1.desc = g_string_new ("audiotestsrc wave=2 ");
  //g_string_append_printf (audio_source1.desc, "! audio/x-raw ");
  g_string_append_printf (audio_source1.desc, "! gdppay ! tcpclientsink port=4000");

  audio_source2.live_seconds = seconds;
  audio_source2.desc = g_string_new ("audiotestsrc ");
  g_string_append_printf (audio_source2.desc, "! gdppay ! tcpclientsink port=4000");

  audio_source3.live_seconds = seconds;
  audio_source3.desc = g_string_new ("audiotestsrc ");
  g_string_append_printf (audio_source3.desc, "! gdppay ! tcpclientsink port=4000");

  if (!opts.test_external_server) {
    server_pid = launch_server ();
    g_assert_cmpint (server_pid, !=, 0);
    sleep (3); /* give a second for server to be online */
  }

  if (!opts.test_external_ui) {
    ui_pid = launch_ui ();
    g_assert_cmpint (ui_pid, !=, 0);
    sleep (2); /* give a second for ui to be ready */
  }

  testcase_run_thread (&video_source1); //sleep (1);
  testcase_run_thread (&video_source2); //sleep (1);
  testcase_run_thread (&video_source3); //sleep (1);
  testcase_run_thread (&audio_source1); //sleep (1);
  testcase_run_thread (&audio_source2); //sleep (1);
  testcase_run_thread (&audio_source3); //sleep (1);
  testcase_join (&video_source1);
  testcase_join (&video_source2);
  testcase_join (&video_source3);
  testcase_join (&audio_source1);
  testcase_join (&audio_source2);
  testcase_join (&audio_source3);

  if (!opts.test_external_ui)
    close_pid (ui_pid);
  if (!opts.test_external_server)
    close_pid (server_pid);
}

static void
test_fuzz (void)
{
  WARN ("TODO: fuzz");
}

static void
test_checking_timestamps (void)
{
  const gint seconds = 60 * 5;
  GPid server_pid = 0;
  GPid ui_pid = 0;
  testcase video_source = { "test-video-source", 0 };
  
  g_print ("\n");

  video_source.live_seconds = seconds;
  video_source.desc = g_string_new ("videotestsrc pattern=0 ");
  g_string_append_printf (video_source.desc, "! video/x-raw,width=%d,height=%d ", W, H);
  g_string_append_printf (video_source.desc, "! timeoverlay font-desc=\"Verdana bold 50\" ! tee name=v ");
  g_string_append_printf (video_source.desc, "v. ! queue "
      "! textoverlay font-desc=\"Sans 120\" text=111 "
      "! gdppay ! tcpclientsink port=3000 ");
  g_string_append_printf (video_source.desc, "v. ! queue "
      "! textoverlay font-desc=\"Sans 120\" text=222 "
      "! gdppay ! tcpclientsink port=3000 ");

  if (!opts.test_external_server) {
    server_pid = launch_server ();
    g_assert_cmpint (server_pid, !=, 0);
    sleep (3); /* give a second for server to be online */
  }

  if (!opts.test_external_ui) {
    ui_pid = launch_ui ();
    g_assert_cmpint (ui_pid, !=, 0);
    sleep (2); /* give a second for ui to be ready */
  }

  testcase_run_thread (&video_source);
  testcase_join (&video_source);

  if (!opts.test_external_ui)
    close_pid (ui_pid);
  if (!opts.test_external_server)
    close_pid (server_pid);
}

int main (int argc, char**argv)
{
  {
    GOptionContext *context;
    GOptionGroup *group;
    GError *error = NULL;
    gboolean ok;
    group = g_option_group_new ("gst-switch-test", "gst-switch test suite",
	"",
	NULL, NULL);
    context = g_option_context_new ("");
    g_option_context_add_main_entries (context, option_entries, "test-switch-server");
    g_option_context_add_group (context, group);
    ok = g_option_context_parse (context, &argc, &argv, &error);
    g_option_context_free (context);
    if (!ok) {
      g_print ("option parsing failed: %s\n", error->message);
      return 1;
    }
  }

  srand (time (NULL));

  gst_init (&argc, &argv);
  g_test_init (&argc, &argv, NULL);
  if (!opts.disable_test_controller) {
    g_test_add_func ("/gst-switch/controller", test_controller);
  }
  if (!opts.disable_test_video) {
    g_test_add_func ("/gst-switch/video", test_video);
    g_test_add_func ("/gst-switch/video-recording-result", test_video_recording_result);
  }
  if (!opts.disable_test_audio) {
    g_test_add_func ("/gst-switch/audio", test_audio);
    g_test_add_func ("/gst-switch/audio-recording-result", test_audio_recording_result);
  }
  if (!opts.disable_test_ui_integration) {
    g_test_add_func ("/gst-switch/ui-integrated", test_ui_integrated);
    g_test_add_func ("/gst-switch/recording-result", test_recording_result);
  }
  if (!opts.disable_test_switching) {
    g_test_add_func ("/gst-switch/switching", test_switching);
    g_test_add_func ("/gst-switch/recording-result", test_recording_result);
  }
  if (!opts.disable_test_random_connection) {
    g_test_add_func ("/gst-switch/random-connections", test_random_connections);
    g_test_add_func ("/gst-switch/recording-result", test_recording_result);
  }
  if (!opts.disable_test_fuzz) {
    g_test_add_func ("/gst-switch/fuzz", test_fuzz);
    g_test_add_func ("/gst-switch/recording-result", test_recording_result);
  }
  if (!opts.disable_test_checking_timestamps) {
    g_test_add_func ("/gst-switch/checking-timestamps", test_checking_timestamps);
    g_test_add_func ("/gst-switch/recording-result", test_recording_result);
  }
  return g_test_run ();
}
