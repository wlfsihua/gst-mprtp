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
#include <math.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include "sndtracker.h"

GST_DEBUG_CATEGORY_STATIC (sndtracker_debug_category);
#define GST_CAT_DEFAULT sndtracker_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)
#define _priv(this) ((Private*)(this->priv))
//#define _get_subflow(this, subflow_id) ((Subflow*)(_priv(this)->subflows + subflow_id))

G_DEFINE_TYPE (SndTracker, sndtracker, G_TYPE_OBJECT);

typedef struct{
  void     (*callback)(gpointer udata, SndTrackerStat* stat);
  gpointer   udata;
}SubflowNotifier;

typedef struct _Subflow{
  gboolean            init;
  SndTrackerStat      stat;
  RTPPacket*          sent_packets[65536];
  Observer*           on_packet_sent;
}Subflow;

typedef struct _Priv{
  Subflow subflows[256];
}Private;

//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static void
sndtracker_finalize (
    GObject * object);

static void
_sent_packets_rem_pipe(SndTracker* this, RTPPacket* packet);

static void
_acked_packets_rem_pipe(SndTracker* this, RTPPacket* packet);

static void
_fec_rem_pipe(SndTracker* this, FECEncoderResponse* response);

static Private*
_priv_ctor(void);

static void
_priv_dtor(
    Private *priv);

static void
_on_subflow_joined(SndTracker* this, SndSubflow* subflow);

static void
_on_subflow_detached(SndTracker* this, SndSubflow* subflow);

static Subflow* _get_subflow(
    SndTracker *this,
    guint8 subflow_id);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
sndtracker_class_init (SndTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndtracker_finalize;

  GST_DEBUG_CATEGORY_INIT (sndtracker_debug_category, "sndtracker", 0,
      "MpRTP Sending Rate Distributor");
}

void
sndtracker_finalize (GObject * object)
{
  SndTracker * this;
  this = SNDTRACKER(object);
  g_object_unref(this->sysclock);
  g_object_unref(this->sent_sw);
  g_object_unref(this->acked_sw);
  g_object_unref(this->fec_sw);

  g_object_unref(this->subflows_db);
  _priv_dtor(this->priv);
}


void
sndtracker_init (SndTracker * this)
{
  this->sysclock = gst_system_clock_obtain();
  this->sent_sw  = make_slidingwindow(1000, GST_SECOND);
  this->acked_sw = make_slidingwindow(1000, GST_SECOND);
  this->fec_sw   = make_slidingwindow(500, GST_SECOND);
  this->priv = _priv_ctor();

  slidingwindow_add_on_rem_item_cb(this->sent_sw, (NotifierFunc) _sent_packets_rem_pipe, this);
  slidingwindow_add_on_rem_item_cb(this->fec_sw, (NotifierFunc) _fec_rem_pipe, this);
  slidingwindow_add_on_rem_item_cb(this->acked_sw, (NotifierFunc) _acked_packets_rem_pipe, this);
}

SndTracker *make_sndtracker(SndSubflows* subflows_db)
{
  SndTracker* this;
  this = g_object_new(SNDTRACKER_TYPE, NULL);
  this->subflows_db = g_object_ref(subflows_db);

  sndsubflows_add_on_subflow_joined_cb(this->subflows_db, (NotifierFunc) _on_subflow_joined, this);
  sndsubflows_add_on_subflow_detached_cb(this->subflows_db, (NotifierFunc) _on_subflow_detached, this);

  return this;
}

void sndtracker_refresh(SndTracker * this)
{
  slidingwindow_refresh(this->sent_sw);
  slidingwindow_refresh(this->acked_sw);
}

SndTrackerStat* sndtracker_get_stat(SndTracker * this)
{
  return &this->stat;
}

SndTrackerStat* sndtracker_get_subflow_stat(SndTracker * this, guint8 subflow_id)
{
  Subflow *subflow = _get_subflow(this, subflow_id);
  return &subflow->stat;
}

void sndtracker_packet_sent(SndTracker * this, RTPPacket* packet)
{
  this->stat.bytes_in_flight += packet->payload_size;
  ++this->stat.packets_in_flight;

  this->stat.sent_bytes_in_1s += packet->payload_size;
  ++this->stat.sent_packets_in_1s;

  this->stat.total_sent_bytes += packet->payload_size;
  ++this->stat.total_sent_packets;

  if(packet->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, packet->subflow_id);

    subflow->stat.bytes_in_flight += packet->payload_size;
    ++subflow->stat.packets_in_flight;

    subflow->stat.sent_bytes_in_1s += packet->payload_size;
    ++subflow->stat.sent_packets_in_1s;

    subflow->stat.total_sent_bytes += packet->payload_size;
    ++subflow->stat.total_sent_packets;

    subflow->sent_packets[packet->subflow_seq] = packet;

    observer_notify(subflow->on_packet_sent, packet);
  }

  rtppackets_packet_ref(packet);
  slidingwindow_add_data(this->sent_sw, packet);
}

RTPPacket* sndtracker_retrieve_sent_packet(SndTracker * this, guint8 subflow_id, guint16 subflow_seq)
{
  Subflow* subflow = _get_subflow(this, subflow_id);
  RTPPacket* result = subflow->sent_packets[subflow_seq];
  return result;
}


void sndtracker_packet_acked(SndTracker * this, RTPPacket* packet)
{
  this->stat.bytes_in_flight -= packet->payload_size;
  --this->stat.packets_in_flight;

  this->stat.acked_bytes_in_1s += packet->payload_size;
  ++this->stat.acked_packets_in_1s;

  this->stat.total_acked_bytes += packet->payload_size;
  ++this->stat.total_acked_packets;

  if(!packet->onsending_info.lost){
    this->stat.received_bytes_in_1s += packet->payload_size;
    ++this->stat.received_packets_in_1s;

    this->stat.total_received_bytes += packet->payload_size;
    ++this->stat.total_received_packets;
  }

  if(packet->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, packet->subflow_id);

    subflow->stat.bytes_in_flight -= packet->payload_size;
    --subflow->stat.packets_in_flight;

    subflow->stat.acked_bytes_in_1s += packet->payload_size;
    ++subflow->stat.acked_packets_in_1s;

    subflow->stat.total_acked_bytes += packet->payload_size;
    ++subflow->stat.total_acked_packets;

    if(!packet->onsending_info.lost){
      this->stat.received_bytes_in_1s += packet->payload_size;
      ++this->stat.received_packets_in_1s;

      subflow->stat.total_received_bytes += packet->payload_size;
      ++subflow->stat.total_received_packets;
    }

    subflow->sent_packets[packet->subflow_seq] = NULL;
  }

  rtppackets_packet_ref(packet);
  slidingwindow_add_data(this->acked_sw, packet);
}

void sndtracker_add_fec_response(SndTracker * this, FECEncoderResponse *fec_response)
{
  this->stat.sent_fec_bytes_in_1s += fec_response->payload_size;
  ++this->stat.sent_fec_packets_in_1s;

  if(fec_response->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, fec_response->subflow_id);
    subflow->stat.sent_fec_bytes_in_1s += fec_response->payload_size;
    ++subflow->stat.sent_fec_packets_in_1s;
  }

  fecencoder_ref_response(fec_response);
  slidingwindow_add_data(this->fec_sw, fec_response);
}

void sndtracker_add_on_packet_sent(SndTracker * this, guint8 subflow_id, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(_get_subflow(this, subflow_id)->on_packet_sent, callback, udata);
}

void _sent_packets_rem_pipe(SndTracker* this, RTPPacket* packet)
{
  this->stat.sent_bytes_in_1s -= packet->payload_size;
  --this->stat.sent_packets_in_1s;

  if(packet->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, packet->subflow_id);
    subflow->stat.sent_bytes_in_1s -= packet->payload_size;
    --subflow->stat.sent_packets_in_1s;
  }

  rtppackets_packet_unref(packet);
}

void _acked_packets_rem_pipe(SndTracker* this, RTPPacket* packet)
{
  this->stat.acked_bytes_in_1s -= packet->payload_size;
  --this->stat.acked_packets_in_1s;

  this->stat.total_acked_bytes -= packet->payload_size;
  --this->stat.total_acked_packets;

  if(!packet->onsending_info.lost){
    this->stat.received_bytes_in_1s -= packet->payload_size;
    --this->stat.received_packets_in_1s;
  }

  if(packet->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, packet->subflow_id);
    subflow->stat.acked_bytes_in_1s -= packet->payload_size;
    --subflow->stat.acked_packets_in_1s;

    subflow->stat.total_acked_bytes -= packet->payload_size;
    --subflow->stat.total_acked_packets;

    if(!packet->onsending_info.lost){
      subflow->stat.received_bytes_in_1s -= packet->payload_size;
      --subflow->stat.total_received_packets;
    }
  }

  rtppackets_packet_unref(packet);
}


void _fec_rem_pipe(SndTracker* this, FECEncoderResponse* response)
{
  this->stat.sent_fec_bytes_in_1s -= response->payload_size;
  --this->stat.sent_fec_packets_in_1s;

  if(response->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, response->subflow_id);
    subflow->stat.sent_fec_bytes_in_1s -= response->payload_size;
    --subflow->stat.sent_fec_packets_in_1s;

  }

  fecencoder_unref_response(response);
}

static Private* _priv_ctor(void)
{
  Private* result = g_malloc0(sizeof(Private));
  return result;
}

static void _priv_dtor(Private *priv)
{
  gint i;
  Subflow* subflow;
  for(i = 0; i < 256; ++i){
    subflow = priv->subflows + i;
    if(subflow->init){
      g_object_unref(subflow->on_packet_sent);
    }
  }
}

void
_on_subflow_joined(SndTracker* this, SndSubflow* sndsubflow)
{
  Subflow *subflow = _get_subflow(this, sndsubflow->id);
  if(subflow->init){
    return;
  }
  subflow->init = TRUE;
  subflow->on_packet_sent = make_observer();
}

void
_on_subflow_detached(SndTracker* this, SndSubflow* subflow)
{

}

Subflow* _get_subflow(SndTracker *this, guint8 subflow_id)
{
  Subflow *result;
  result = _priv(this)->subflows + subflow_id;
  return result;
}
