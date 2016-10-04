/* GStreamer Scheduling tree
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
#include "rtppackets.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "rtpfecbuffer.h"
#include "lib_swplugins.h"

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (rtppackets_debug_category);
#define GST_CAT_DEFAULT rtppackets_debug_category

G_DEFINE_TYPE (RTPPackets, rtppackets, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void rtppackets_finalize (GObject * object);
static void _check_buffer_meta_data(RTPPackets * this, RTPPacket *packet);
static void _extract_mprtp_info(RTPPackets* this, RTPPacket* packet, GstRTPBuffer *rtp);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
rtppackets_class_init (RTPPacketsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rtppackets_finalize;

  GST_DEBUG_CATEGORY_INIT (rtppackets_debug_category, "rtppackets", 0,
      "MpRTP Manual Sending Controller");

}

void
rtppackets_finalize (GObject * object)
{
  RTPPackets *this;
  this = RTPPACKETS(object);
  g_object_unref(this->on_stalled_packets);

  g_object_unref(this->sysclock);
  g_free(this->packets);

}

RTPPackets* make_rtppackets(void)
{
  RTPPackets* this;
  this = g_object_new (RTPPACKETS_TYPE, NULL);
  this->on_stalled_packets = make_observer();

  this->mprtp_ext_header_id      = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->abs_time_ext_header_id   = ABS_TIME_DEFAULT_EXTENSION_HEADER_ID;
  return this;
}


void
rtppackets_init (RTPPackets * this)
{
  this->sysclock = gst_system_clock_obtain();
  this->packets = g_malloc0(sizeof(RTPPacket) * 65536);
}


void rtppackets_reset(RTPPackets* this)
{
  memset((gpointer)this->packets, 0, sizeof(RTPPackets) * 65536);
}



void rtppackets_add_stalled_packet_cb(RTPPackets* this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_stalled_packets, callback, udata);
}


static void _init_rtppacket(RTPPackets* this, RTPPacket* packet, GstRTPBuffer* rtp)
{
  packet->base_db      = this;
  packet->buffer       = gst_buffer_ref(rtp->buffer);
  packet->created      = _now(this);
  packet->abs_seq      = gst_rtp_buffer_get_seq(rtp);
  packet->timestamp    = gst_rtp_buffer_get_timestamp(rtp);
  packet->ssrc         = gst_rtp_buffer_get_ssrc(rtp);
  packet->payload_size = gst_rtp_buffer_get_payload_len(rtp);
  packet->payload_type = gst_rtp_buffer_get_payload_type(rtp);
  packet->header_size  = gst_rtp_buffer_get_header_len(rtp);
  packet->ref          = 1;

  this->pivot_address_subflow_id = 0;
  this->pivot_address            = NULL;
}

static gboolean _do_reset_packet(RTPPackets* this, RTPPacket* packet)
{
  if(packet->ref < 1){
    return TRUE; //The packet is never used or appropriately unrefed
  }
  if(!packet->forwarded){
    return FALSE; //The packet is not forwarded but created before, so now its used.
  }

  //At this point we know sg is wrong!
  //The packet is marked as forwarded, so it went through all of the process
  //and still a reference is greater then 0. So notify debug functions
  //or any other kind of marvelous function needs to be notified.
  observer_notify(this->on_stalled_packets, packet);
  //and reset the packet
  return TRUE;
}

static RTPPacket* _retrieve_packet(RTPPackets* this, GstRTPBuffer* rtp)
{

  RTPPacket* result;
  result  = this->packets +  gst_rtp_buffer_get_seq(rtp);

  if(!_do_reset_packet(this, result)){
    goto done;
  }

  memset(result, 0, sizeof(RTPPacket));
  _init_rtppacket(this, result, rtp);
done:
  return result;
}

RTPPacket* rtppackets_retrieve_packet_for_sending(RTPPackets* this, GstBuffer *buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  RTPPacket* result;
  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  result = _retrieve_packet(this, &rtp);
  gst_rtp_buffer_unmap(&rtp);
  result->position = RTP_PACKET_POSITION_ONSENDING;
  return result;
}


RTPPacket* rtppackets_retrieve_packet_at_receiving(RTPPackets* this, GstBuffer *buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  RTPPacket* result;

  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  result = _retrieve_packet(this, &rtp);
  _extract_mprtp_info(this, result, &rtp);
  _check_buffer_meta_data(this, result);
  gst_rtp_buffer_unmap(&rtp);
  result->position = RTP_PACKET_POSITION_RECEIVED;
  return result;
}


void rtppackets_packet_forwarded(RTPPackets* this, RTPPacket *packet)
{
  packet->buffer    = NULL;
  packet->forwarded = _now(this);

  if(0 < packet->ref) {
    --packet->ref;
  }
}

RTPPacket* rtppackets_get_by_abs_seq(RTPPackets* this, guint16 abs_seq)
{
  return this->packets + abs_seq;
}

void rtppackets_set_abs_time_ext_header_id(RTPPackets* this, guint8 abs_time_ext_header_id)
{
  this->abs_time_ext_header_id = abs_time_ext_header_id;
}

guint8 rtppackets_get_abs_time_ext_header_id(RTPPackets* this)
{
  return this->abs_time_ext_header_id;
}

void rtppackets_set_mprtp_ext_header_id(RTPPackets* this, guint8 mprtp_ext_header_id)
{
  this->mprtp_ext_header_id = mprtp_ext_header_id;
}

guint8 rtppackets_get_mprtp_ext_header_id(RTPPackets* this)
{
  return this->mprtp_ext_header_id;
}

void rtppackets_packet_unref(RTPPacket *packet)
{
  --packet->ref;
}

void rtppackets_packet_ref(RTPPacket *packet)
{
  ++packet->ref;
}

void rtppacket_setup_mprtp(RTPPacket *packet, SndSubflow* subflow)
{
  guint8  mprtp_ext_header_id = rtppackets_get_mprtp_ext_header_id(packet->base_db);
  guint16 subflow_seq = sndsubflow_get_next_subflow_seq(subflow);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  packet->buffer = gst_buffer_make_writable(packet->buffer);

  gst_rtp_buffer_map(packet->buffer, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_mprtp_extension(&rtp, mprtp_ext_header_id, subflow->id, subflow_seq);
  gst_rtp_buffer_unmap(&rtp);

  packet->subflow_id = subflow->id;
  packet->subflow_seq = subflow_seq;

}


void rtppacket_setup_abs_time_extension(RTPPacket* packet)
{
  guint8 abs_time_header_ext = rtppackets_get_abs_time_ext_header_id(packet->base_db);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  packet->buffer = gst_buffer_make_writable(packet->buffer);

  gst_rtp_buffer_map(packet->buffer, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_abs_time_extension(&rtp, abs_time_header_ext);
  gst_rtp_buffer_unmap(&rtp);
}

void _check_buffer_meta_data(RTPPackets * this, RTPPacket *packet)
{
  GstNetAddressMeta *meta;
  //to avoid the check_collision problem in rtpsession.
  meta = gst_buffer_get_net_address_meta (packet->buffer);
  if (meta) {
    if (!this->pivot_address) {
      this->pivot_address_subflow_id = packet->subflow_id;
      this->pivot_address = G_SOCKET_ADDRESS (g_object_ref (meta->addr));
    } else if (packet->subflow_seq != this->pivot_address_subflow_id) {
      if(gst_buffer_is_writable(packet->buffer))
        gst_buffer_add_net_address_meta (packet->buffer, this->pivot_address);
      else{
        packet->buffer = gst_buffer_make_writable(packet->buffer);
        gst_buffer_add_net_address_meta (packet->buffer, this->pivot_address);
      }
    }
  }
}

void _extract_mprtp_info(RTPPackets* this, RTPPacket* packet, GstRTPBuffer *rtp)
{

  guint8 mprtp_ext_header_id = rtppackets_get_mprtp_ext_header_id(this);

  packet->received_info.abs_snd_ntp_time =
      gst_rtp_buffer_get_abs_time_extension(rtp, this->abs_time_ext_header_id);

  packet->received_info.delay =
      get_epoch_time_from_ntp_in_ns(NTP_NOW - packet->received_info.abs_snd_ntp_time);

  gst_rtp_buffer_get_mprtp_extension(rtp, mprtp_ext_header_id, &packet->subflow_id, &packet->subflow_seq);

}
