/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MESSENGER_H_
#define MESSENGER_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"

typedef struct _Messenger Messenger;
typedef struct _MessengerClass MessengerClass;

#define MESSENGER_TYPE             (messenger_get_type())
#define MESSENGER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MESSENGER_TYPE,Messenger))
#define MESSENGER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MESSENGER_TYPE,MessengerClass))
#define MESSENGER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MESSENGER_TYPE))
#define MESSENGER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MESSENGER_TYPE))
#define MESSENGER_CAST(src)        ((Messenger *)(src))



typedef void (*MessengerItemShaper)(gpointer result,gpointer udata);

struct _Messenger
{
  GObject             object;
  GMutex              mutex;
  GQueue*             messages;
  GQueue*             recycle;
  GCond               cond;
  MessengerItemShaper shaper;
  gsize               block_size;
};

struct _MessengerClass{
  GObjectClass parent_class;
};


GType messenger_get_type (void);

Messenger *make_messenger(gsize block_size);
gpointer messenger_pop(Messenger *this);
gpointer messenger_try_pop(Messenger *this);
gpointer messenger_pop_with_timeout (Messenger *this, gint64 microseconds);
void messenger_push(Messenger* this, gpointer message);
void messenger_throw_block(Messenger* this, gpointer message);
gpointer messenger_retrieve_block(Messenger *this);

#endif /* MESSENGER_H_ */
