/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be ureful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "rcvctrler.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include "mprtprpath.h"
#include "streamjoiner.h"
#include "ricalcer.h"
#include "mprtplogger.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define MIN_MEDIA_RATE 50000

#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (rcvctrler_debug_category);
#define GST_CAT_DEFAULT rcvctrler_debug_category

G_DEFINE_TYPE (RcvController, rcvctrler, G_TYPE_OBJECT);

#define REGULAR_REPORT_PERIOD_TIME (5*GST_SECOND)

typedef struct _Subflow Subflow;

//                        ^
//                        | Event
// .---.    .--------.-----------.
// |   |    | T | SystemNotifier |
// |   |    | i |----------------|
// | I |    | c |      ORP       |->Reports
// | R |-E->| k |----------------|
// | P |    | e |   PlayCtrler   |
// |   |    | r |                |
// '---'    '---'----------------'
//                        | Delays
//                        V

#define RATEWINDOW_LENGTH 10
typedef struct _RateWindow{
  guint32 items[RATEWINDOW_LENGTH];
  gint    index;
  guint32 rate_value;
}RateWindow;

struct _Subflow
{
  guint8                        id;
  MpRTPRPath*                   path;
  GstClock*                     sysclock;
  GstClockTime                  joined_time;
  ReportIntervalCalculator*     ricalcer;

  gdouble                       avg_rtcp_size;
  guint32                       total_lost;
  guint32                       total_received;
  guint32                       total_discarded_bytes;
  guint16                       HSSN;
  guint64                       last_seen_report;
  GstClockTime                  LRR;

  gchar                        *logfile;
  gchar                        *statfile;

  gboolean                      reporting;
  gboolean                      rfc3550_enabled;
  gboolean                      rfc7243_enabled;
  gboolean                      owd_enabled;
  guint                         fbra_marc_enabled;

  RateWindow                    discarded_bytes;
  RateWindow                    discarded_packets;
  RateWindow                    lost_packets;
  RateWindow                    received_packets;
  RateWindow                    received_bytes;
  RateWindow                    HSSNs;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
rcvctrler_finalize (GObject * object);

static void
refctrler_ticker (void *data);

static void
_logging (RcvController *this);

static void
_refresh_subflows (RcvController *this);

//------------------------ Outgoing Report Producer -------------------------

static void
_orp_main(RcvController * this);

static void
_orp_add_rr(
    RcvController *this,
    Subflow *subflow);

static void
_orp_add_xr_rfc7243(
    RcvController *this,
    Subflow *subflow);

static void
_orp_add_xr_owd(
    RcvController *this,
    Subflow *subflow);

void
_orp_fbra_marc_feedback(
    RcvController * this,
    Subflow *subflow);


//----------------------------- System Notifier ------------------------------
static void
_system_notifier_main(RcvController * this);

//----------------------------- Path Ticker Main -----------------------------


//------------------------- Utility functions --------------------------------
static Subflow*
_make_subflow (
    guint8 id,
    MpRTPRPath * path);

static void
_ruin_subflow (gpointer * subflow);

static void
_reset_subflow (Subflow * subflow);

static Subflow*
_subflow_ctor (void);

static void
_subflow_dtor (Subflow * this);

static void
_change_reporting_mode(
    Subflow *this,
    guint mode,
    guint cngctrler);


static guint32
_uint32_diff (
    guint32 a,
    guint32 b);

static guint16
_uint16_diff (
    guint16 a,
    guint16 b);

static guint32
_update_ratewindow(
    RateWindow *window,
    guint32 value);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


void
rcvctrler_class_init (RcvControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rcvctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (rcvctrler_debug_category, "rndctrler", 0,
      "MpRTP Receiving Controller");

}


void
rcvctrler_setup (RcvController *this, StreamJoiner * joiner, FECDecoder* fecdecoder)
{
  THIS_WRITELOCK (this);
  this->joiner     = joiner;
  this->fecdecoder = fecdecoder;
  THIS_WRITEUNLOCK (this);
}

void rcvctrler_change_interval_type(RcvController * this, guint8 subflow_id, guint type)
{
  Subflow *subflow;
  ReportIntervalCalculator *ricalcer;
  GHashTableIter iter;
  gpointer key, val;

  THIS_WRITELOCK (this);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    ricalcer = subflow->ricalcer;
    if(subflow_id == 255 || subflow_id == 0 || subflow_id == subflow->id){
      switch(type){
        case 0:
          ricalcer_set_mode(ricalcer, RTCP_INTERVAL_REGULAR_INTERVAL_MODE);
        break;
        case 1:
          ricalcer_set_mode(ricalcer, RTCP_INTERVAL_EARLY_RTCP_MODE);
        break;
        case 2:
        default:
          ricalcer_set_mode(ricalcer, RTCP_INTERVAL_IMMEDIATE_FEEDBACK_MODE);
        break;
      }
    }
  }
  THIS_WRITEUNLOCK (this);
}

void rcvctrler_change_reporting_mode(RcvController * this, guint8 subflow_id, guint reports, guint cngctrler)
{
  Subflow *subflow;
  GHashTableIter iter;
  gpointer key, val;
  THIS_WRITELOCK (this);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    if(subflow_id == 255 || subflow_id == 0 || subflow_id == subflow->id){
      _change_reporting_mode(subflow, reports, cngctrler);
    }
  }
  THIS_WRITEUNLOCK (this);
}


void
rcvctrler_finalize (GObject * object)
{
  RcvController *this = RCVCTRLER (object);
  g_hash_table_destroy (this->subflows);
  gst_task_stop (this->thread);
  gst_task_join (this->thread);
//  g_object_unref (this->ricalcer);
  g_object_unref (this->sysclock);
  g_object_unref(this->report_producer);

  mprtp_free(this->fec_early_repaired_bytes);
  mprtp_free(this->fec_total_repaired_bytes);
}

void
rcvctrler_init (RcvController * this)
{
  this->sysclock           = gst_system_clock_obtain ();
  this->subflows           = g_hash_table_new_full (NULL, NULL,NULL, (GDestroyNotify) _ruin_subflow);
  this->ssrc               = g_random_int ();
  this->report_is_flowable = FALSE;
  this->report_producer    = g_object_new(REPORTPRODUCER_TYPE, NULL);
  this->report_processor   = g_object_new(REPORTPROCESSOR_TYPE, NULL);

  this->fec_early_repaired_bytes         = mprtp_malloc(sizeof(RateWindow));
  this->fec_total_repaired_bytes         = mprtp_malloc(sizeof(RateWindow));

  this->made               = _now(this);

  report_processor_set_logfile(this->report_processor, "rcv_reports.log");
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new (refctrler_ticker, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
}

void
_logging (RcvController *this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  GstClockTime median_delay;
  gdouble fraction_lost;
  guint32 goodput_bytes;
  guint32 goodput_packets;
  gdouble fecdecoder_early_ratio;

  if(!this->joiner) goto done;

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {


    subflow = (Subflow *) val;
    if(!subflow->logfile){
      subflow->logfile = g_malloc0(255);
      sprintf(subflow->logfile, "sub_%d_rcv.csv", subflow->id);
      subflow->statfile = g_malloc0(255);
      sprintf(subflow->statfile, "sub_%d_stat.csv", subflow->id);
    }

    mprtpr_path_get_owd_stats(subflow->path, &median_delay, NULL, NULL);

    fraction_lost = (gdouble)subflow->HSSNs.rate_value / (gdouble)subflow->received_packets.rate_value;
    goodput_bytes = subflow->received_bytes.rate_value - subflow->discarded_bytes.rate_value;
    goodput_packets = subflow->received_packets.rate_value - subflow->received_packets.rate_value;


    mprtp_logger(subflow->logfile, "%u,%u,%lu,%u\n",
                 (subflow->received_bytes.rate_value  * 8 + 48 * 8 * subflow->received_packets.rate_value )/1000, //KBit
                 (subflow->discarded_bytes.rate_value  * 8 + 48 * 8 * subflow->discarded_packets.rate_value)/1000, //KBit
            GST_TIME_AS_USECONDS(median_delay),
            0);


    mprtp_logger(subflow->statfile,
                 "%u,%f,%u,%f\n",
                 goodput_bytes * 8 + (goodput_packets * 48 /*bytes overhead */ * 8),
                 fraction_lost,
                 subflow->lost_packets.rate_value,
                 (subflow->discarded_bytes.rate_value * 8 + 48 * 8 * subflow->discarded_packets.rate_value )/1000);

  }

  fecdecoder_early_ratio     = !((RateWindow*) this->fec_total_repaired_bytes)->rate_value ? 0. : (gdouble) ((RateWindow*) this->fec_early_repaired_bytes)->rate_value / (gdouble) ((RateWindow*) this->fec_total_repaired_bytes)->rate_value;
  mprtp_logger("fecdec_stat.csv",
               "%u,%u,%f,%f\n",
               ((RateWindow*) this->fec_early_repaired_bytes)->rate_value,
               ((RateWindow*) this->fec_total_repaired_bytes)->rate_value,
               fecdecoder_early_ratio,
               this->FFRE
               );

done:
  return;
}

void
refctrler_ticker (void *data)
{
  GstClockTime next_scheduler_time;
  RcvController *this;
  GstClockID clock_id;
//  guint64 max_path_skew = 0;
  this = RCVCTRLER (data);
  THIS_WRITELOCK (this);
  _refresh_subflows(this);
  _logging(this);
  _orp_main(this);

  _system_notifier_main(this);

  next_scheduler_time = _now(this) + 100 * GST_MSECOND;
  THIS_WRITEUNLOCK (this);
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The playout clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
  //clockshot;
}


void
rcvctrler_add_path (RcvController *this, guint8 subflow_id,
    MpRTPRPath * path)
{
  Subflow *lookup_result;
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result != NULL) {
    GST_WARNING_OBJECT (this, "The requested add operation can not be done "
        "due to duplicated subflow id (%d)", subflow_id);
    goto exit;
  }
  lookup_result = _make_subflow (subflow_id, path);
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow_id),
                       lookup_result);
//  lookup_result->ricalcer = this->ricalcer;
exit:
  THIS_WRITEUNLOCK (this);
}

void
rcvctrler_rem_path (RcvController *this, guint8 subflow_id)
{
  Subflow *lookup_result;
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result == NULL) {
    GST_WARNING_OBJECT (this, "The requested remove operation can not be done "
        "due to not existed subflow id (%d)", subflow_id);
    goto exit;
  }
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));
exit:
  THIS_WRITEUNLOCK (this);
}


void
rcvctrler_setup_callbacks(RcvController * this,
                          gpointer mprtcp_send_data,
                          GstBufferReceiverFunc mprtcp_send_func)
{
  THIS_WRITELOCK (this);
  this->send_mprtcp_packet_func = mprtcp_send_func;
  this->send_mprtcp_packet_data = mprtcp_send_data;
  THIS_WRITEUNLOCK (this);
}



//------------------------- Incoming Report Processor -------------------


void
rcvctrler_receive_mprtcp (RcvController *this, GstBuffer * buf)
{
  Subflow *subflow;
  GstMPRTCPReportSummary *summary;

  THIS_WRITELOCK (this);

  summary = &this->reports_summary;
  memset(summary, 0, sizeof(GstMPRTCPReportSummary));

  report_processor_process_mprtcp(this->report_processor, buf, summary);

  subflow =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (summary->subflow_id));

  if (subflow == NULL) {
    GST_WARNING_OBJECT (this,
        "MPRTCP riport can not be binded any "
        "subflow with the given id: %d", summary->subflow_id);
    goto done;
  }

  if(summary->SR.processed){
    this->report_is_flowable = TRUE;
    mprtpr_path_add_delay(subflow->path, get_epoch_time_from_ntp_in_ns(NTP_NOW - summary->SR.ntptime));
    report_producer_set_ssrc(this->report_producer, summary->ssrc);
    subflow->last_seen_report = summary->SR.ntptime;
  }

  mprtp_free(summary);

done:
  THIS_WRITEUNLOCK (this);
}


//------------------------ Outgoing Report Producer -------------------------

void
_refresh_subflows (RcvController *this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint32 discarded_bytes;
  guint32 discarded_packets;
  guint32 lost_packets;
  guint32 received_packets;
  guint32 received_bytes;
  guint32 fec_early_repaired_bytes;
  guint32 fec_total_repaired_bytes;
  guint16 HSSN, cycle_num;
  guint32 u32_HSSN;

  if(!this->joiner) goto done;

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    mprtpr_path_get_total_discards(subflow->path, &discarded_packets, &discarded_bytes);
    mprtpr_path_get_total_losts(subflow->path, &lost_packets);
    mprtpr_path_get_regular_stats(subflow->path,
                                  &HSSN,
                                  &cycle_num,
                                  NULL,
                                  &received_packets,
                                  &lost_packets,
                                  &received_bytes);
    u32_HSSN = (((guint32) cycle_num)<<16) | ((guint32) HSSN);

    _update_ratewindow(&subflow->discarded_bytes, discarded_bytes);
    _update_ratewindow(&subflow->discarded_packets, discarded_packets);
    _update_ratewindow(&subflow->received_packets, received_packets);
    _update_ratewindow(&subflow->received_bytes, received_bytes);
    _update_ratewindow(&subflow->lost_packets, lost_packets);
    _update_ratewindow(&subflow->HSSNs, u32_HSSN);

  }

  fecdecoder_get_stat(this->fecdecoder,
                          &fec_early_repaired_bytes,
                          &fec_total_repaired_bytes,
                          &this->FFRE);

  _update_ratewindow(this->fec_early_repaired_bytes, fec_early_repaired_bytes);
  _update_ratewindow(this->fec_total_repaired_bytes, fec_total_repaired_bytes);

done:
  return;
}

static void
_orp_main(RcvController * this)
{
  ReportIntervalCalculator* ricalcer;
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint report_length = 0;
  GstBuffer *buffer;
  gchar logfile[255];
  GstClockTime elapsed_x, elapsed_y;

  if (!this->report_is_flowable) {
      //Todo: fix this;
      //goto done;
  }

  ++this->orp_tick;
  elapsed_x  = GST_TIME_AS_MSECONDS(_now(this) - this->made);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    gboolean send_regular, send_fb;
    subflow  = (Subflow *) val;
    ricalcer = subflow->ricalcer;

    if(!subflow->reporting){
      continue;
    }

    //Todo: decide when we need this.
    //ricalcer_urgent_report_request(ricalcer);

    send_regular = ricalcer_rtcp_regular_allowed(subflow->ricalcer);
    send_fb = ricalcer_rtcp_fb_allowed(subflow->ricalcer);

    if(!send_regular && !send_fb){
      continue;
    }

    report_producer_begin(this->report_producer, subflow->id);
    if(send_regular){
      _orp_add_rr(this, subflow);
      if(subflow->owd_enabled){
        _orp_add_xr_owd(this, subflow);
      }
      if(subflow->rfc7243_enabled){
        _orp_add_xr_rfc7243(this, subflow);
      }

      //logging the report timeout
      memset(logfile, 0, 255);
      sprintf(logfile, "sub_%d_rtcp_ints.csv", subflow->id);
      elapsed_y  = GST_TIME_AS_MSECONDS(_now(this) - subflow->LRR);
      subflow->LRR = _now(this);
      mprtp_logger(logfile, "%lu,%lu\n", elapsed_x, elapsed_y);
    }

    if(subflow->fbra_marc_enabled){
      _orp_fbra_marc_feedback(this, subflow);
    }

    buffer = report_producer_end(this->report_producer, &report_length);
    this->send_mprtcp_packet_func(this->send_mprtcp_packet_data, buffer);

    report_length += 12 /* RTCP HEADER*/ + (28<<3) /*UDP+IP HEADER*/;
    subflow->avg_rtcp_size += (report_length - subflow->avg_rtcp_size) / 4.;

    ricalcer_refresh_parameters(ricalcer,
                                MIN_MEDIA_RATE,
                                subflow->avg_rtcp_size);


  }

  DISABLE_LINE _uint16_diff(0,0);
}



void _orp_add_rr(RcvController * this, Subflow *subflow)
{
  guint8 fraction_lost;
  guint32 ext_hsn;
  guint32 received;
  guint32 lost;
  guint32 expected;
  guint32 total_received;
  guint32 total_lost;
  guint32 jitter;
  guint16 cycle_num;
  guint16 HSSN;
  guint32 LSR;
  guint32 DLSR;

  mprtpr_path_get_regular_stats(subflow->path,
                             &HSSN,
                             &cycle_num,
                             &jitter,
                             &total_received,
                             &total_lost,
                             NULL);

  expected      = _uint32_diff(subflow->HSSN, HSSN);
  received      = total_received - subflow->total_received;
  lost          = expected - received;

  fraction_lost = (expected == 0 || lost <= 0) ? 0 : (lost << 8) / expected;
  ext_hsn       = (((guint32) cycle_num) << 16) | ((guint32) HSSN);

  subflow->HSSN            = HSSN;
  subflow->total_lost     = total_lost;
  subflow->total_received = total_received;

  LSR = (guint32) (subflow->last_seen_report >> 16);

  if (subflow->last_seen_report == 0) {
      DLSR = 0;
  } else {
      guint64 temp;
      temp = NTP_NOW - subflow->last_seen_report;
      DLSR = (guint32)(temp>>16);
  }


  report_producer_add_rr(this->report_producer,
                         fraction_lost,
                         subflow->total_lost,
                         ext_hsn,
                         jitter,
                         LSR,
                         DLSR
                         );

}


void _orp_add_xr_rfc7243(RcvController * this, Subflow *subflow)
{
  guint32 total_discarded_bytes;

  mprtpr_path_get_total_discards(subflow->path, NULL, &total_discarded_bytes);
  if(total_discarded_bytes == subflow->total_discarded_bytes){
    goto done;
  }

  report_producer_add_xr_rfc7243(this->report_producer,
                                 total_discarded_bytes);

done:
  return;
}

void _orp_add_xr_owd(RcvController * this, Subflow *subflow)
{
  GstClockTime median_delay, min_delay, max_delay;
  guint32      u32_median_delay, u32_min_delay, u32_max_delay;

  mprtpr_path_get_owd_stats(subflow->path,
                                 &median_delay,
                                 &min_delay,
                                 &max_delay);
  u32_median_delay = (guint32)(get_ntp_from_epoch_ns(median_delay)>>16);
  u32_min_delay    = (guint32)(get_ntp_from_epoch_ns(min_delay)>>16);
  u32_max_delay    = (guint32)(get_ntp_from_epoch_ns(max_delay)>>16);

  report_producer_add_xr_owd(this->report_producer,
                             u32_median_delay,
                             u32_min_delay,
                             u32_max_delay);
}

void _orp_fbra_marc_feedback(RcvController * this, Subflow *subflow)
{
  GstClockTime median_delay;
  guint32 owd_sampling;
  guint16 HSSN;
  GstRTCPFB_FBRA_MARC fbra_marc;
  guint32 expected,received,lost;
  guint8 fraction_lost;

  mprtpr_path_get_owd_stats(subflow->path, &median_delay, NULL, NULL);

  HSSN = mprtpr_path_get_HSSN(subflow->path);
  owd_sampling = get_ntp_from_epoch_ns(median_delay)>>16;

  fbra_marc.rsvd = 0;
  fbra_marc.records_num = RTCP_AFB_FBRA_MARC_RECORDS_NUM;
  fbra_marc.length = g_htons(((sizeof(GstRTCPAFBMARCRecord) * RTCP_AFB_FBRA_MARC_RECORDS_NUM)>>2)-1);
  fbra_marc.records[0].HSSN = g_htons(HSSN);
  fbra_marc.records[0].owd_sample = g_htonl(owd_sampling);
  fbra_marc.records[0].discarded_bytes = g_htonl(subflow->discarded_bytes.rate_value);

  expected      = subflow->HSSNs.rate_value;
  received      = subflow->received_packets.rate_value;
  lost          = expected - received;

  fraction_lost = (expected == 0 || lost <= 0) ? 0 : (lost << 8) / expected;

  fbra_marc.records[0].fraction_lost = fraction_lost;

  report_producer_add_afb(this->report_producer,
                          this->ssrc,
                          RTCP_AFB_FBRA_MARC_ID,
                          &fbra_marc,
                          sizeof(GstRTCPFB_FBRA_MARC));

}



//----------------------------- System Notifier ------------------------------
void
_system_notifier_main(RcvController * this)
{

}

//----------------------------- Refresh subflows -----------------------------



//------------------------- Utility functions --------------------------------

Subflow *
_make_subflow (guint8 id, MpRTPRPath * path)
{
  Subflow *result     = _subflow_ctor ();
  result->sysclock    = gst_system_clock_obtain ();
  result->path        = g_object_ref (path);;
  result->id          = id;
  result->joined_time = gst_clock_get_time (result->sysclock);
  result->ricalcer    = make_ricalcer(FALSE);
  result->LRR         = _now(result);
  _reset_subflow (result);
  return result;
}


void
_ruin_subflow (gpointer * subflow)
{
  Subflow *this;
  g_return_if_fail (subflow);
  this = (Subflow *) subflow;
  g_object_unref (this->sysclock);
  g_object_unref (this->path);
  g_object_unref (this->ricalcer);
  if(this->logfile){
    mprtp_free(this->logfile);
  }
  _subflow_dtor (this);
}

void
_reset_subflow (Subflow * this)
{
  this->avg_rtcp_size = 1024.;
}


Subflow *
_subflow_ctor (void)
{
  Subflow *result;
  result = mprtp_malloc (sizeof (Subflow));
  return result;
}

void
_subflow_dtor (Subflow * this)
{
  g_return_if_fail (this);
  mprtp_free (this);
}

void _change_reporting_mode(Subflow *this, guint reports, guint cngctrler)
{

  this->rfc3550_enabled = (reports & 1) > 0;
  this->rfc7243_enabled = (reports & 2) > 0;
  this->owd_enabled = (reports & 4) > 0;
  switch(cngctrler){
    case 1:
      this->fbra_marc_enabled = TRUE;
      break;
    case 0:
    default:

    break;
  }

  this->reporting = this->rfc3550_enabled ||
                    this->rfc7243_enabled ||
                    this->owd_enabled     ||
                    this->fbra_marc_enabled;
}

guint32
_uint32_diff (guint32 start, guint32 end)
{
  if (start <= end) {
    return end - start;
  }
  return ~((guint32) (start - end));
}

guint16
_uint16_diff (guint16 start, guint16 end)
{
  if (start <= end) {
    return end - start;
  }
  return ~((guint16) (start - end));
}

guint32 _update_ratewindow(RateWindow *window, guint32 value)
{
  window->items[window->index] = value;
  window->index = (window->index + 1) % RATEWINDOW_LENGTH;
  window->rate_value = value - window->items[window->index];
  return window->rate_value;
}

#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
