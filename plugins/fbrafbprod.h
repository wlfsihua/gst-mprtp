/*
 * fbrafbproducer.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FBRAFBPRODUCER_H_
#define FBRAFBPRODUCER_H_

#include <gst/gst.h>
#include "gstmprtcpbuffer.h"
#include "reportprod.h"
#include "lib_swplugins.h"

typedef struct _FBRAFBProducer FBRAFBProducer;
typedef struct _FBRAFBProducerClass FBRAFBProducerClass;

#define FBRAFBPRODUCER_TYPE             (fbrafbproducer_get_type())
#define FBRAFBPRODUCER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FBRAFBPRODUCER_TYPE,FBRAFBProducer))
#define FBRAFBPRODUCER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FBRAFBPRODUCER_TYPE,FBRAFBProducerClass))
#define FBRAFBPRODUCER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FBRAFBPRODUCER_TYPE))
#define FBRAFBPRODUCER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FBRAFBPRODUCER_TYPE))
#define FBRAFBPRODUCER_CAST(src)        ((FBRAFBProducer *)(src))

typedef struct _CorrBlock CorrBlock;


struct _FBRAFBProducer
{
  GObject                  object;
  GstClock*                sysclock;

  guint16                  cycle_num;
  gboolean                 initialized;

  RcvSubflow*              subflow;
  RcvTracker*              tracker;

  guint16                  begin_seq;
  guint16                  end_seq;
  gboolean*                vector;
  guint                    vector_length;

  SlidingWindow*           owds_sw;
  SlidingWindow*           rle_sw;

  GstClockTime             last_fb;
  gint                     rcved_packets;

  GstClockTime             median_delay;
  GstClockTime             min_delay;
  GstClockTime             max_delay;

  struct{
    gint counter;
    gint sum;
  }tendency;
  gint32                   received_bytes;


};

struct _FBRAFBProducerClass{
  GObjectClass parent_class;

};

GType fbrafbproducer_get_type (void);
FBRAFBProducer *make_fbrafbproducer(RcvSubflow* subflow, RcvTracker *tracker);
void fbrafbproducer_reset(FBRAFBProducer *this);
void fbrafbproducer_set_owd_treshold(FBRAFBProducer *this, GstClockTime treshold);


#endif /* FBRAFBPRODUCER_H_ */
