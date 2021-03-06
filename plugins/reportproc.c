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
#include "reportproc.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (report_processor_debug_category);
#define GST_CAT_DEFAULT report_processor_debug_category

G_DEFINE_TYPE (ReportProcessor, report_processor, G_TYPE_OBJECT);

#define _now(this) (gst_clock_get_time (this->sysclock))

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
report_processor_finalize (GObject * object);

static void
_processing_mprtcp_subflow_block (
    ReportProcessor *this,
    GstMPRTCPSubflowBlock * block,
    GstMPRTCPReportSummary* summary);

static void
_processing_rrblock (
    ReportProcessor *this,
    GstRTCPRRBlock * rrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_xr_discarded_bytes_block (
    ReportProcessor *this,
    GstRTCPXRDiscardedBlock * xrb,
    GstMPRTCPReportSummary* summary);

void
_processing_xr_discarded_packets_block (
    ReportProcessor *this,
    GstRTCPXRDiscardedBlock * xrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_afb (ReportProcessor *this,
                 GstRTCPFB *afb,
                 GstMPRTCPReportSummary* summary);

static void
_processing_xr_owd_block (
    ReportProcessor *this,
    GstRTCPXROWDBlock * xrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_xr_rle_losts_block (
    ReportProcessor *this,
    GstRTCPXRRLELostsRLEBlock * xrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_xr_cc_rle_fb_block (ReportProcessor *this,
                        GstRTCPXRCCFeedbackRLEBlock * xrb,
                        GstMPRTCPReportSummary* summary);


static void
_processing_srblock (
    ReportProcessor *this,
    GstRTCPSRBlock * rrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_xr(ReportProcessor *this,
                    GstRTCPXR * xr,
                    GstMPRTCPReportSummary* summary);


static gint
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}


void
report_processor_class_init (ReportProcessorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = report_processor_finalize;

  GST_DEBUG_CATEGORY_INIT (report_processor_debug_category, "report_processor", 0,
      "MpRTP Receiving Controller");

}

void
report_processor_finalize (GObject * object)
{
  ReportProcessor *this = REPORTPROCESSOR (object);
  g_object_unref (this->sysclock);
}

void
report_processor_init (ReportProcessor * this)
{
  g_rw_lock_init (&this->rwmutex);

  this->sysclock   = gst_system_clock_obtain ();
  this->ssrc       = g_random_int();
  this->made       = _now(this);

  memset(this->logfile, 0, 255);
  sprintf(this->logfile, "report_processor.log");
}

void report_processor_set_ssrc(ReportProcessor *this, guint32 ssrc)
{
  this->ssrc = ssrc;
}

void report_processor_process_mprtcp(ReportProcessor * this, GstBuffer* buffer, GstMPRTCPReportSummary* result)
{
  guint32 ssrc;
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstMPRTCPSubflowReport *report;
  GstMPRTCPSubflowBlock *block;

  gst_buffer_map(buffer, &map, GST_MAP_READ);
  report = (GstMPRTCPSubflowReport *)map.data;
  gst_mprtcp_report_getdown(report, &ssrc);
  result->created = _now(this);
  result->ssrc = ssrc;
  result->updated = _now(this);
  block = gst_mprtcp_get_first_block(report);
  _processing_mprtcp_subflow_block(this, block, result);

  gst_buffer_unmap(buffer, &map);
}

void report_processor_set_logfile(ReportProcessor *this, const gchar *logfile)
{
  strcpy(this->logfile, logfile);
}

void _processing_mprtcp_subflow_block (
    ReportProcessor *this,
    GstMPRTCPSubflowBlock * block,
    GstMPRTCPReportSummary* summary)
{
  guint8 pt;
  guint8 block_length;
  guint8 processed_length;
  guint8 rsvd;
  guint16 actual_length;
  guint16 subflow_id;
  GstRTCPHeader *header;
  gpointer databed, actual;
  gst_mprtcp_block_getdown(&block->info, NULL, &block_length, &subflow_id);
  summary->subflow_id = subflow_id;
  actual = databed = header = &block->block_header;
  processed_length = 0;

//  gst_print_rtcp(header);

again:
  gst_rtcp_header_getdown (header, NULL, NULL, &rsvd, &pt, &actual_length, NULL);

  switch(pt){
    case GST_RTCP_TYPE_SR:
      {
        GstRTCPSR* sr = (GstRTCPSR*)header;
        gst_rtcp_header_getdown(header, NULL, NULL, NULL, NULL, NULL, &summary->ssrc);
        _processing_srblock (this, &sr->sender_block, summary);
      }
    break;
    case GST_RTCP_TYPE_RTPFB:
      if(rsvd == GST_RTCP_PSFB_TYPE_AFB){
        GstRTCPFB *afb = (GstRTCPFB*) header;
        _processing_afb(this, afb, summary);
      }
      break;
    case GST_RTCP_TYPE_RR:
      {
        GstRTCPRR* rr = (GstRTCPRR*)header;
        _processing_rrblock (this, &rr->blocks, summary);
      }
    break;
    case GST_RTCP_TYPE_XR:
    {
      GstRTCPXR* xr = (GstRTCPXR*) header;
      _processing_xr(this, xr, summary);
    }
    break;
    default:
      GST_WARNING_OBJECT(this, "Unrecognized MPRTCP Report block");
    break;
  }
  processed_length += actual_length + 1;
  if(processed_length < block_length){
    header = actual = processed_length * 4 + (gchar*)databed;
    goto again;
  }
}

void
_processing_rrblock (ReportProcessor *this,
                     GstRTCPRRBlock * rrb,
                     GstMPRTCPReportSummary* summary)
{
  guint64 LSR, DLSR;
  guint32 LSR_read, DLSR_read, HSSN_read;
  guint8 fraction_lost;

  summary->RR.processed = TRUE;
  //--------------------------
  //validating
  //--------------------------
  gst_rtcp_rrb_getdown (rrb, NULL, &fraction_lost, &summary->RR.total_packet_lost,
                        &HSSN_read, &summary->RR.jitter,
                        &LSR_read, &DLSR_read);
  summary->RR.HSSN = (guint16) (HSSN_read & 0x0000FFFF);
  summary->RR.cycle_num = (guint16) (HSSN_read>>16);
  LSR = (guint64) LSR_read;
  DLSR = (guint64) DLSR_read;

  if (LSR == 0 || DLSR == 0) {
    g_warning("The Last Sent Report and the Delay Since Last Sent Report can not be 0");
    return;
  }
  //--------------------------
  //processing
  //--------------------------
  {
    guint64 diff;
    diff = ((guint32)(NTP_NOW>>16)) - LSR - DLSR;
    summary->RR.RTT = get_epoch_time_from_ntp_in_ns(diff<<16);
  }

  summary->RR.lost_rate = ((gdouble) fraction_lost) / 256.;

}


void
_processing_xr_discarded_bytes_block (ReportProcessor *this,
                                GstRTCPXRDiscardedBlock * xrb,
                                GstMPRTCPReportSummary* summary)
{
  summary->XR.DiscardedBytes.processed = TRUE;
  gst_rtcp_xr_discarded_bytes_getdown (xrb,
                               &summary->XR.DiscardedBytes.interval_metric,
                               &summary->XR.DiscardedBytes.early_bit,
                               NULL,
                               &summary->XR.DiscardedBytes.discarded_bytes);
}

void
_processing_xr_discarded_packets_block (ReportProcessor *this,
                                GstRTCPXRDiscardedBlock * xrb,
                                GstMPRTCPReportSummary* summary)
{
  summary->XR.DiscardedPackets.processed = TRUE;
  gst_rtcp_xr_discarded_packets_getdown (xrb,
                               &summary->XR.DiscardedPackets.interval_metric,
                               &summary->XR.DiscardedPackets.early_bit,
                               NULL,
                               &summary->XR.DiscardedPackets.discarded_packets);
}


void
_processing_afb (ReportProcessor *this,
                 GstRTCPFB *afb,
                 GstMPRTCPReportSummary* summary)
{
  summary->AFB.processed = TRUE;
  gst_rtcp_afb_getdown(afb,
                       NULL,
                       &summary->AFB.media_source_ssrc,
                       &summary->AFB.fci_id);

  gst_rtcp_afb_getdown_fci_data(afb,
                                summary->AFB.fci_data,
                                &summary->AFB.fci_length);
}


void
_processing_xr_owd_block (ReportProcessor *this,
                    GstRTCPXROWDBlock * xrb,
                    GstMPRTCPReportSummary* summary)
{
  guint32 median_delay,min_delay,max_delay;

  summary->XR.OWD.processed = TRUE;

  gst_rtcp_xr_owd_block_getdown(xrb,
                          &summary->XR.OWD.interval_metric,
                          NULL,
                          &median_delay,
                          &min_delay,
                          &max_delay);

  summary->XR.OWD.median_delay = median_delay;
  summary->XR.OWD.median_delay<<=16;
  summary->XR.OWD.median_delay = get_epoch_time_from_ntp_in_ns(summary->XR.OWD.median_delay);
  summary->XR.OWD.min_delay = min_delay;
  summary->XR.OWD.min_delay<<=16;
  summary->XR.OWD.min_delay = get_epoch_time_from_ntp_in_ns(summary->XR.OWD.min_delay);
  summary->XR.OWD.max_delay = max_delay;
  summary->XR.OWD.max_delay<<=16;
  summary->XR.OWD.max_delay = get_epoch_time_from_ntp_in_ns(summary->XR.OWD.max_delay);

//  gst_print_rtcp_xr_owd(xrb);
  //--------------------------
  //evaluating
  //--------------------------
}


void
_processing_xr_rle_losts_block (ReportProcessor *this,
                        GstRTCPXRRLELostsRLEBlock * xrb,
                        GstMPRTCPReportSummary* summary)
{
  guint chunks_num;
  GstRTCPXRChunk chunk, *src;
  guint chunk_i, bit_i;
  guint16 seq;

  summary->XR.LostRLE.processed = TRUE;
  src = xrb->chunks;
  summary->XR.LostRLE.vector_length = 0;
  gst_rtcp_xr_rle_losts_getdown(xrb,
                                    &summary->XR.LostRLE.early_bit,
                                    &summary->XR.LostRLE.thinning,
                                    NULL,
                                    &summary->XR.LostRLE.begin_seq,
                                    &summary->XR.LostRLE.end_seq);

  seq = summary->XR.LostRLE.begin_seq;
  chunks_num = gst_rtcp_xr_rle_losts_block_get_chunks_num(xrb);

  for(chunk_i = 0; chunk_i < chunks_num; ++chunk_i){
    gst_rtcp_xr_chunk_ntoh_cpy(&chunk, src + chunk_i);
    for(bit_i = 0; bit_i < 15 && _cmp_seq(seq, summary->XR.LostRLE.end_seq) <= 0; ++bit_i){
      summary->XR.LostRLE.vector[summary->XR.LostRLE.vector_length++] =
          0 < (chunk.Bitvector.bitvector & (guint16)(1<<bit_i)) ? TRUE : FALSE;
    }
  }
}



void
_processing_xr_cc_rle_fb_block (ReportProcessor *this,
                        GstRTCPXRCCFeedbackRLEBlock * xrb,
                        GstMPRTCPReportSummary* summary)
{
  guint chunks_num;
  GstRTCPXRChunk chunk, *src;
  guint chunk_i;

  summary->XR.CongestionControlFeedback.processed = TRUE;
  src = xrb->chunks;
  summary->XR.LostRLE.vector_length = 0;
  gst_rtcp_xr_cc_fb_rle_getdown(xrb,
      &summary->XR.CongestionControlFeedback.report_count,
      &summary->XR.CongestionControlFeedback.report_timestamp,
      NULL, //ssrc
      &summary->XR.CongestionControlFeedback.begin_seq,
      &summary->XR.CongestionControlFeedback.end_seq
  );
  chunks_num = gst_rtcp_xr_cc_fb_rle_block_get_chunks_num(xrb);
  for(chunk_i = 0; chunk_i < chunks_num; ++chunk_i){
    gst_rtcp_xr_chunk_ntoh_cpy(&chunk, src + chunk_i);
    summary->XR.CongestionControlFeedback.vector[chunk_i].ato = chunk.CCFeedback.ato;
    summary->XR.CongestionControlFeedback.vector[chunk_i].ecn = chunk.CCFeedback.ecn;
    summary->XR.CongestionControlFeedback.vector[chunk_i].lost = chunk.CCFeedback.lost;
  }
  if (summary->XR.CongestionControlFeedback.begin_seq < summary->XR.CongestionControlFeedback.end_seq) {
      summary->XR.CongestionControlFeedback.vector_length = summary->XR.CongestionControlFeedback.end_seq -
          summary->XR.CongestionControlFeedback.begin_seq + 1;
  } else {
    summary->XR.CongestionControlFeedback.vector_length = 65536 -
        summary->XR.CongestionControlFeedback.begin_seq + summary->XR.CongestionControlFeedback.end_seq + 1;
  }

}


void
_processing_srblock(ReportProcessor *this,
                GstRTCPSRBlock * srb,
                GstMPRTCPReportSummary* summary)
{
  summary->SR.processed = TRUE;
  gst_rtcp_srb_getdown(srb,
                       &summary->SR.ntptime,
                       &summary->SR.rtptime,
                       &summary->SR.packet_count,
                       &summary->SR.octet_count);
}



void
_processing_xr(ReportProcessor *this,
                GstRTCPXR * xr,
                GstMPRTCPReportSummary* summary)
{
  GstRTCPXRBlock *block;
  guint8 block_type;
  guint16 block_words;
  guint16 header_words;
  guint16 read_words;

  summary->XR.processed = TRUE;

  gst_rtcp_header_getdown(&xr->header, NULL, NULL, NULL, NULL, &header_words, NULL);
  block = &xr->blocks;
  read_words = 1;
again:
  gst_rtcp_xr_block_getdown(block, &block_type, &block_words, NULL);
//  g_print("block type: %d\n", block_type);
  switch(block_type){
    case GST_RTCP_XR_OWD_BLOCK_TYPE_IDENTIFIER:
        _processing_xr_owd_block(this, (GstRTCPXROWDBlock*) block, summary);
        break;
    case GST_RTCP_XR_LOSS_RLE_BLOCK_TYPE_IDENTIFIER:
        _processing_xr_rle_losts_block(this, (GstRTCPXRRLELostsRLEBlock*) block, summary);
        break;
    case GST_RTCP_XR_DISCARDED_BYTES_BLOCK_TYPE_IDENTIFIER:
        _processing_xr_discarded_bytes_block(this, (GstRTCPXRDiscardedBlock*) block, summary);
        break;
    case GST_RTCP_XR_DISCARDED_PACKETS_BLOCK_TYPE_IDENTIFIER:
        _processing_xr_discarded_packets_block(this, (GstRTCPXRDiscardedBlock*) block, summary);
        break;
    case GST_RTCP_XR_CC_FB_RLE_BLOCK_TYPE_IDENTIFIER:
      _processing_xr_cc_rle_fb_block(this, (GstRTCPXRCCFeedbackRLEBlock*) block, summary);
      break;
    default:
      GST_WARNING_OBJECT(this, "Unrecognized XR block to process");
      goto done;
      break;
  }
  read_words += block_words + 1;
  if(read_words < header_words){
    guint8 *pos;
    pos = (guint8*)block;
    pos += (block_words + 1) << 2;
    block = (GstRTCPXRBlock*) pos;
    goto again;
  }
done:
  return;
}

