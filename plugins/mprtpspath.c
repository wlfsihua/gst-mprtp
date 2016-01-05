/* GStreamer Mprtp sender subflow
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "mprtpspath.h"
#include "gstmprtcpbuffer.h"


#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)


GST_DEBUG_CATEGORY_STATIC (gst_mprtps_path_category);
#define GST_CAT_DEFAULT gst_mprtps_path_category

G_DEFINE_TYPE (MPRTPSPath, mprtps_path, G_TYPE_OBJECT);

static void mprtps_path_finalize (GObject * object);
static void mprtps_path_reset (MPRTPSPath * this);
static void _setup_rtp2mprtp (MPRTPSPath * this, GstBuffer * buffer);
static void _refresh_stat(MPRTPSPath * this, GstBuffer *buffer);
static void _send_mprtp_packet(MPRTPSPath * this,
                               GstBuffer *buffer);
static gboolean _try_flushing(MPRTPSPath * this);
static gboolean _is_overused(MPRTPSPath * this);
static GstBuffer* _create_monitor_packet(MPRTPSPath * this);


void
mprtps_path_class_init (MPRTPSPathClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = mprtps_path_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_mprtps_path_category, "mprtpspath", 0,
      "MPRTP Sender Path");
}

MPRTPSPath *
make_mprtps_path (guint8 id, void (*send_func)(gpointer, GstBuffer*), gpointer func_this)
{
  MPRTPSPath *result;

  result = g_object_new (MPRTPS_PATH_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->id = id;
  result->send_mprtp_func_data = func_this;
  result->send_mprtp_packet_func = send_func;
  THIS_WRITEUNLOCK (result);
  return result;


}

/**
 * mprtps_path_reset:
 * @src: an #MPRTPSPath
 *
 * Reset the subflow of @src.
 */
void
mprtps_path_reset (MPRTPSPath * this)
{
  this->is_new = TRUE;
  this->seq = 0;
  this->cycle_num = 0;
  this->state = MPRTPS_PATH_FLAG_ACTIVE |
      MPRTPS_PATH_FLAG_NON_CONGESTED | MPRTPS_PATH_FLAG_NON_LOSSY;

  this->total_sent_packet_num = 0;
  this->total_sent_normal_packet_num = 0;
  this->total_sent_payload_bytes_sum = 0;
  this->total_sent_frames_num = 0;
  this->last_sent_frame_timestamp = 0;
  this->sent_octets_read = 0;
  this->sent_octets_write = 0;
  this->max_bytes_per_ms = 0;
  this->monitor_payload_type = FALSE;
  this->monitoring_interval = 0;

  packetssndqueue_reset(this->packetsqueue);
  variancetracker_reset(this->sent_bytes);
}


void
mprtps_path_init (MPRTPSPath * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain ();
  this->packetsqueue = make_packetssndqueue();
  this->sent_bytes = make_variancetracker(1024, GST_SECOND);
  mprtps_path_reset (this);
}


void
mprtps_path_finalize (GObject * object)
{
  MPRTPSPath *this = MPRTPS_PATH_CAST (object);
  g_object_unref (this->sysclock);
}

gboolean
mprtps_path_is_new (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = this->is_new;
  THIS_READUNLOCK (this);
  return result;
}

void
mprtps_path_set_not_new (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->is_new = FALSE;
  THIS_WRITEUNLOCK (this);
}

MPRTPSPathState
mprtps_path_get_state (MPRTPSPath * this)
{
  MPRTPSPathState result;
  THIS_READLOCK (this);
  if ((this->state & (guint8) MPRTPS_PATH_FLAG_ACTIVE) == 0) {
    result = MPRTPS_PATH_STATE_PASSIVE;
    goto done;
  }
  if ((this->state & (guint8) MPRTPS_PATH_FLAG_NON_CONGESTED) == 0) {
    result = MPRTPS_PATH_STATE_CONGESTED;
    goto done;
  }
  if ((this->state & (guint8) MPRTPS_PATH_FLAG_NON_LOSSY) == 0) {
    result = MPRTPS_PATH_STATE_LOSSY;
    goto done;
  }
  result = MPRTPS_PATH_STATE_NON_CONGESTED;
done:
  THIS_READUNLOCK (this);
  return result;
}


gboolean
mprtps_path_is_active (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = (this->state & (guint8) MPRTPS_PATH_FLAG_ACTIVE) ? TRUE : FALSE;
  THIS_READUNLOCK (this);
  return result;
}


void
mprtps_path_set_active (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->state |= (guint8) MPRTPS_PATH_FLAG_ACTIVE;
  this->sent_active = gst_clock_get_time (this->sysclock);
  this->sent_passive = 0;
  THIS_WRITEUNLOCK (this);
}


void
mprtps_path_set_passive (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->state &= (guint8) 255 ^ (guint8) MPRTPS_PATH_FLAG_ACTIVE;
  this->sent_passive = gst_clock_get_time (this->sysclock);
  this->sent_active = 0;
  THIS_WRITEUNLOCK (this);
}

GstClockTime
mprtps_path_get_time_sent_to_passive (MPRTPSPath * this)
{
  GstClockTime result;
  THIS_READLOCK (this);
  result = this->sent_passive;
  THIS_READUNLOCK (this);
  return result;
}

GstClockTime
mprtps_path_get_time_sent_to_non_congested (MPRTPSPath * this)
{
  GstClockTime result;
  THIS_READLOCK (this);
  result = this->sent_non_congested;
  THIS_READUNLOCK (this);
  return result;
}

GstClockTime
mprtps_path_get_time_sent_to_lossy (MPRTPSPath * this)
{
  GstClockTime result;
  THIS_READLOCK (this);
  result = this->sent_middly_congested;
  THIS_READUNLOCK (this);
  return result;
}

GstClockTime
mprtps_path_get_time_sent_to_congested (MPRTPSPath * this)
{
  GstClockTime result;
  THIS_READLOCK (this);
  result = this->sent_congested;
  THIS_READUNLOCK (this);
  return result;
}

guint16 mprtps_path_get_HSN(MPRTPSPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->seq;
  THIS_READUNLOCK (this);
  return result;
}

void
mprtps_path_set_pacing (MPRTPSPath * this, guint32 bytes_per_s)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->pacing = bytes_per_s ? TRUE : FALSE;
  this->max_bytes_per_s = bytes_per_s;
  //g_print ("T%d pacing changed to %d, max bytes: %u\n", this->id, this->pacing, this->max_bytes_per_s);
  THIS_WRITEUNLOCK (this);
}

void mprtps_path_set_monitor_payload_id(MPRTPSPath *this, guint8 payload_type)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->monitor_payload_type = payload_type;
  THIS_WRITEUNLOCK (this);
}



void mprtps_path_set_mprtp_ext_header_id(MPRTPSPath *this, guint ext_header_id)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->mprtp_ext_header_id = ext_header_id;
  THIS_WRITEUNLOCK (this);
}

gboolean
mprtps_path_is_non_lossy (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = (this->state & (guint8) MPRTPS_PATH_FLAG_NON_LOSSY) ? TRUE : FALSE;
  THIS_READUNLOCK (this);
  return result;

}

void
mprtps_path_set_lossy (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->state &= (guint8) 255 ^ (guint8) MPRTPS_PATH_FLAG_NON_LOSSY;
  this->sent_middly_congested = gst_clock_get_time (this->sysclock);
  THIS_WRITEUNLOCK (this);
}

void
mprtps_path_set_non_lossy (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->state |= (guint8) MPRTPS_PATH_FLAG_NON_LOSSY;
  THIS_WRITEUNLOCK (this);
}


gboolean
mprtps_path_is_non_congested (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result =
      (this->state & (guint8) MPRTPS_PATH_FLAG_NON_CONGESTED) ? TRUE : FALSE;
  THIS_READUNLOCK (this);
  return result;

}

gboolean
mprtps_path_is_in_trial (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = (this->state & (guint8) MPRTPS_PATH_FLAG_TRIAL) ? TRUE : FALSE;
  THIS_READUNLOCK (this);
  return result;

}

void
mprtps_path_set_congested (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->state &= (guint8) 255 ^ (guint8) MPRTPS_PATH_FLAG_NON_CONGESTED;
  this->sent_congested = gst_clock_get_time (this->sysclock);
  THIS_WRITEUNLOCK (this);
}


void
mprtps_path_set_trial_begin (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->state |= (guint8) MPRTPS_PATH_FLAG_TRIAL;
  this->sent_congested = gst_clock_get_time (this->sysclock);
  THIS_WRITEUNLOCK (this);
}

void
mprtps_path_set_trial_end (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->state &= (guint8) 255 ^ (guint8) MPRTPS_PATH_FLAG_TRIAL;
  this->sent_congested = gst_clock_get_time (this->sysclock);
  THIS_WRITEUNLOCK (this);
}


void
mprtps_path_set_non_congested (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->state |= (guint8) MPRTPS_PATH_FLAG_NON_CONGESTED;
  this->sent_non_congested = gst_clock_get_time (this->sysclock);
  THIS_WRITEUNLOCK (this);
}


guint8
mprtps_path_get_id (MPRTPSPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->id;
  THIS_READUNLOCK (this);
  return result;
}

void mprtps_path_set_monitor_interval(MPRTPSPath *this, guint interval)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->monitoring_interval = interval;
  THIS_WRITEUNLOCK (this);
}

void mprtps_path_set_extra(MPRTPSPath *this, guint32 extra)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->extra_packets_per_100tick = 0;
  this->extra_packets_per_10tick = 0;
  this->extra_packets_per_tick = 0;
  if(extra < 14000){
    this->extra_packets_per_100tick = extra / 1400;
  }else if(extra < 140000){
    this->extra_packets_per_10tick = extra / 14000;
  }else{
    this->extra_packets_per_tick = extra / 140000;
  }
  THIS_WRITEUNLOCK (this);
}

gboolean
mprtps_path_is_monitoring (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = this->monitoring_interval > 0;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtps_path_get_total_sent_packets_num (MPRTPSPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_sent_packet_num;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtps_path_get_total_sent_payload_bytes (MPRTPSPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_sent_payload_bytes_sum;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtps_path_get_total_sent_frames_num (MPRTPSPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_sent_frames_num;
  THIS_READUNLOCK (this);
  return result;
}

gboolean
mprtps_path_is_overused (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = _is_overused(this);
  THIS_READUNLOCK (this);
  return result;
}



guint32
mprtps_path_get_sent_octet_sum_for (MPRTPSPath * this, guint32 amount)
{
  guint32 result = 0;
  guint16 read;

  THIS_WRITELOCK (this);
  if (amount < 1 || this->sent_octets_read == this->sent_octets_write) {
    goto done;
  }
  for (read = 0;
      this->sent_octets_read != this->sent_octets_write && read < amount;
      ++read) {
    result += (guint32) this->sent_octets[this->sent_octets_read];
    this->sent_octets_read += 1;
    this->sent_octets_read &= MAX_INT32_POSPART;
  }
done:
  THIS_WRITEUNLOCK (this);
  return result;
}


void
mprtps_path_tick(MPRTPSPath *this)
{
//  guint generate = 0, generated = 0;
  THIS_WRITELOCK (this);
  ++this->ticknum;
//  if(this->extra_packets_per_tick > 0){
//    generate = this->extra_packets_per_tick;
//  }
//  else if(this->extra_packets_per_10tick > 0 && this->ticknum % 10 == 0){
//    generate = this->extra_packets_per_10tick;
//  }
//  else if(this->extra_packets_per_100tick > 0 && this->ticknum % 100 == 0){
//    generate = this->extra_packets_per_100tick;
//  }
//  for(generated = 0; generated < generate; ++generated){
//    GstBuffer *buffer;
//    buffer = _create_monitor_packet(this);
//    _setup_rtp2mprtp(this, buffer);
//    _send_mprtp_packet(this, buffer);
//  }

  if(packetssndqueue_has_buffer(this->packetsqueue)){
    _try_flushing(this);
  }
  THIS_WRITEUNLOCK (this);
}

void
mprtps_path_process_rtp_packet(MPRTPSPath * this,
                               GstBuffer * buffer)
{

  THIS_WRITELOCK (this);
  _setup_rtp2mprtp (this, buffer);
  _send_mprtp_packet(this, buffer);
//  goto done;
  if(!this->monitoring_interval) goto done;
  if(this->total_sent_normal_packet_num % this->monitoring_interval != 0) goto done;
  {
    GstBuffer *buffer;
    buffer = _create_monitor_packet(this);
    _setup_rtp2mprtp(this, buffer);
    _send_mprtp_packet(this, buffer);
  }
done:
  THIS_WRITEUNLOCK (this);

}

void
_setup_rtp2mprtp (MPRTPSPath * this,
                  GstBuffer * buffer)
{
  MPRTPSubflowHeaderExtension data;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  gst_rtp_buffer_map(buffer, GST_MAP_READWRITE, &rtp);
  data.id = this->id;
  if (++(this->seq) == 0) {
    ++(this->cycle_num);
  }
  data.seq = this->seq;

  gst_rtp_buffer_add_extension_onebyte_header (&rtp, this->mprtp_ext_header_id,
      (gpointer) & data, sizeof (data));
  if(this->ssrc_allowed == 0)
    this->ssrc_allowed = gst_rtp_buffer_get_ssrc(&rtp);
  gst_rtp_buffer_unmap(&rtp);
}

void
_refresh_stat(MPRTPSPath * this,
              GstBuffer *buffer)
{
  guint payload_bytes;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  payload_bytes = gst_rtp_buffer_get_payload_len (&rtp);
  ++this->total_sent_packet_num;
  this->last_sent_payload_bytes = payload_bytes;
  this->last_packet_sent_time = gst_clock_get_time (this->sysclock);
  if(gst_rtp_buffer_get_timestamp(&rtp) != this->last_sent_frame_timestamp){
      ++this->total_sent_frames_num;
      this->last_sent_frame_timestamp = gst_rtp_buffer_get_timestamp(&rtp);
  }
  if(gst_rtp_buffer_get_payload_type(&rtp) != this->monitor_payload_type){
    this->total_sent_payload_bytes_sum += payload_bytes;
    this->sent_octets[this->sent_octets_write] = payload_bytes >> 3;
    ++this->total_sent_normal_packet_num;
  } else {
    this->sent_octets[this->sent_octets_write] = 0;
  }

  variancetracker_add(this->sent_bytes, payload_bytes);
  this->sent_octets_write += 1;
  this->sent_octets_write &= MAX_INT32_POSPART;
  gst_rtp_buffer_unmap(&rtp);
}

void
_send_mprtp_packet(MPRTPSPath * this,
                      GstBuffer *buffer)
{
  variancetracker_obsolate(this->sent_bytes);
  if (_is_overused(this)) {
    GST_WARNING_OBJECT (this, "Path is overused");
    packetssndqueue_push(this->packetsqueue, buffer);
//    g_print("Overused:%u\n", packetssndqueue_get_num(this->packetsqueue));
    goto done;
  }
  if(packetssndqueue_has_buffer(this->packetsqueue)){
    packetssndqueue_push(this->packetsqueue, buffer);
    _try_flushing(this);
    goto done;
  }
  _refresh_stat(this, buffer);
  this->send_mprtp_packet_func(this->send_mprtp_func_data, buffer);
done:
 return;
}

gboolean _try_flushing(MPRTPSPath * this)
{
  variancetracker_obsolate(this->sent_bytes);
//  percentiletracker_obsolate(this->sent_bytes);
  while(packetssndqueue_has_buffer(this->packetsqueue)){
    GstBuffer *buffer;
    if(_is_overused(this)) goto failed;
    buffer = packetssndqueue_pop(this->packetsqueue);
    _refresh_stat(this, buffer);
    this->send_mprtp_packet_func(this->send_mprtp_func_data, buffer);
  }
  return TRUE;
failed:
  return FALSE;
}

gboolean _is_overused(MPRTPSPath * this)
{
  gint64 sum;
  if(!this->pacing || !this->max_bytes_per_s){
    return FALSE;
  }
  variancetracker_get_stats(this->sent_bytes, &sum, NULL);
//  g_print("S%d sent_bytes: %ld, max bytes: %u\n", this->id, sum, this->max_bytes_per_s);
  return this->max_bytes_per_s < sum;
//  percentiletracker_get_stats(this->sent_bytes, NULL, NULL, &sum);
//  g_print("S%d max_bytes_per_s: %u - outgoing_sum: %d\n",
//          this->id, this->max_bytes_per_s, this->outgoing_sum);
//  if(this->max_bytes_per_s < this->outgoing_sum){
//      g_print("Overused\n");
//    return FALSE;
//  }
  return FALSE;
}

GstBuffer* _create_monitor_packet(MPRTPSPath * this)
{
  GstBuffer *result;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  result = gst_rtp_buffer_new_allocate (1400, 0, 0);
  gst_rtp_buffer_map(result, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type(&rtp, this->monitor_payload_type);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
