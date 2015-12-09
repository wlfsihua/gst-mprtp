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
#include "percentiletracker.h"
#include <math.h>
#include <string.h>

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)


GST_DEBUG_CATEGORY_STATIC (percentiletracker_debug_category);
#define GST_CAT_DEFAULT percentiletracker_debug_category

G_DEFINE_TYPE (PercentileTracker, percentiletracker, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void percentiletracker_finalize (GObject * object);
static void
_add_value(PercentileTracker *this, guint64 value);
static void
_obsolate (PercentileTracker * this);
static void
_balancing_trees (PercentileTracker * this);
static gint
_cmp_for_max (guint64 x, guint64 y);
static gint
_cmp_for_min (guint64 x, guint64 y);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
percentiletracker_class_init (PercentileTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = percentiletracker_finalize;

  GST_DEBUG_CATEGORY_INIT (percentiletracker_debug_category, "percentiletracker", 0,
      "PercentileTracker");

}

void
percentiletracker_finalize (GObject * object)
{
  PercentileTracker *this;
  this = PERCENTILETRACKER(object);
  g_object_unref(this->maxtree);
  g_object_unref(this->mintree);
  g_object_unref(this->sysclock);
  g_free(this->items);
}

void
percentiletracker_init (PercentileTracker * this)
{
  g_rw_lock_init (&this->rwmutex);
}

PercentileTracker *make_percentiletracker(
                                  guint32 length,
                                  guint percentile)
{
    return make_percentiletracker_full(_cmp_for_min, _cmp_for_max, length, percentile);
}

PercentileTracker *make_percentiletracker_full(BinTreeCmpFunc cmp_min,
                                  BinTreeCmpFunc cmp_max,
                                  guint32 length,
                                  guint percentile)
{
  PercentileTracker *result;
  result = g_object_new (PERCENTILETRACKER_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->maxtree = make_bintree(cmp_max);
  result->mintree = make_bintree(cmp_min);
  result->max_multiplier = (gdouble)(100 - percentile) / (gdouble)percentile;
  result->items = g_malloc0(sizeof(PercentileTrackerItem)*length);
  result->sum = 0;
  result->length = length;
  result->sysclock = gst_system_clock_obtain();
  result->treshold = GST_SECOND;
  THIS_WRITEUNLOCK (result);

  return result;
}

void percentiletracker_test(void)
{
  PercentileTracker *tracker;
  tracker = make_percentiletracker(10, 40);
  percentiletracker_add(tracker, 7);
  percentiletracker_add(tracker, 1);
  percentiletracker_add(tracker, 3);
  percentiletracker_add(tracker, 8);
  percentiletracker_add(tracker, 2);
  percentiletracker_add(tracker, 6);
  percentiletracker_add(tracker, 4);
  percentiletracker_add(tracker, 5);
  percentiletracker_add(tracker, 9);
  percentiletracker_add(tracker, 10);

  {
    guint64 min,max,perc;
    perc = percentiletracker_get_stats(tracker, &min, &max, NULL);
    g_print("PercentileTracker test for 40th percentile\n"
            "Min: %lu, 40th percentile: %lu Max: %lu\n", min, perc, max);
  }

}

void percentiletracker_reset(PercentileTracker *this)
{
  THIS_WRITELOCK (this);
  bintree_reset(this->maxtree);
  bintree_reset(this->mintree);
  memset(this->items, 0, sizeof(PercentileTrackerItem) * this->length);
  this->write_index = this->read_index = 0;
  this->sum = 0;
  THIS_WRITEUNLOCK (this);
}

void percentiletracker_add(PercentileTracker *this, guint64 value)
{
  THIS_WRITELOCK (this);
  _add_value(this, value);
  _balancing_trees(this);
  THIS_WRITEUNLOCK (this);
}

void percentiletracker_set_treshold(PercentileTracker *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->treshold = treshold;
  THIS_WRITEUNLOCK (this);
}

guint32 percentiletracker_get_num(PercentileTracker *this)
{
  guint32 result;
  THIS_READLOCK(this);
  if(this->read_index < this->write_index)
    result = this->write_index - this->read_index;
  else
    result = this->length - this->read_index + this->write_index;
//  result = bintree_get_num(this->maxtree) + bintree_get_num(this->mintree);
  THIS_READUNLOCK(this);
  return result;
}

guint64 percentiletracker_get_last(PercentileTracker *this)
{
  guint64 result;
  THIS_READLOCK(this);
  if(this->read_index == this->write_index) result = 0;
  else if(this->write_index == 0) result = this->items[this->length-1].value;
  else result = this->items[this->write_index-1].value;
  THIS_READUNLOCK(this);
  return result;
}


guint64
percentiletracker_get_stats (PercentileTracker * this,
                         guint64 *min,
                         guint64 *max,
                         guint64 *sum)
{
  guint64 result = 0;
  gint32 max_count, min_count, diff;
//  g_print("mprtpr_path_get_delay_median begin\n");
  THIS_READLOCK (this);
  if(sum) *sum = this->sum;
  if(min) *min = 0;
  if(max) *max = 0;
  min_count = bintree_get_num(this->mintree);
  max_count = (gdouble)bintree_get_num(this->maxtree) * this->max_multiplier + .5;
  diff = max_count - min_count;

  if(min_count + max_count < 1)
    goto done;
  if(0 < diff)
    result = bintree_get_top_value(this->maxtree);
  else if(diff < 0)
    result = bintree_get_top_value(this->mintree);
  else{
    result = (bintree_get_top_value(this->maxtree) +
              bintree_get_top_value(this->mintree))>>1;
  }
  if(min) *min = bintree_get_bottom_value(this->maxtree);
  if(max) *max = bintree_get_bottom_value(this->mintree);
//  g_print("%d-%d\n", min_count, max_count);
done:
THIS_READUNLOCK (this);
//  g_print("mprtpr_path_get_delay_median end\n");
  return result;
}



void
percentiletracker_obsolate (PercentileTracker * this)
{
  THIS_READLOCK (this);
  _obsolate(this);
  THIS_READUNLOCK (this);
}




void _add_value(PercentileTracker *this, guint64 value)
{
  GstClockTime now;
  now = gst_clock_get_time(this->sysclock);
  //add new one
  this->sum += value;
  this->items[this->write_index].value = value;
  this->items[this->write_index].added = now;

  if(this->items[this->write_index].value <= bintree_get_top_value(this->maxtree))
    bintree_insert_value(this->maxtree, this->items[this->write_index].value);
  else
    bintree_insert_value(this->mintree, this->items[this->write_index].value);

  if(++this->write_index == this->length){
      this->write_index=0;
  }

  _obsolate(this);
}

void
_obsolate (PercentileTracker * this)
{
  GstClockTime treshold,now;
  now = gst_clock_get_time(this->sysclock);
  treshold = now - this->treshold;
again:
  if(this->write_index == this->read_index) goto elliminate;
  else if(this->items[this->read_index].added < treshold) goto elliminate;
  else goto done;
elliminate:
  if(this->items[this->read_index].value <= bintree_get_top_value(this->maxtree))
    bintree_delete_value(this->maxtree, this->items[this->read_index].value);
  else
    bintree_delete_value(this->mintree, this->items[this->read_index].value);
  this->sum -= this->items[this->read_index].value;
  this->items[this->read_index].value = 0;
  this->items[this->read_index].added = 0;
  if(++this->read_index == this->length){
      this->read_index=0;
  }
  goto again;
done:
  return;
}

void
_balancing_trees (PercentileTracker * this)
{
  gint32 max_count, min_count;
  gint32 diff;
  BinTreeNode *top;


balancing:
  min_count = bintree_get_num(this->mintree);
  max_count = (gdouble)bintree_get_num(this->maxtree) * this->max_multiplier + .5;
  diff = max_count - min_count;
//  g_print("%p:%f max_tree_num: %d, min_tree_num: %d\n",
//          this,
//          this->max_multiplier,
//          max_count,
//          min_count);
  if (-2 < diff && diff < 2) {
    goto done;
  }
  if (diff < -1) {
    top = bintree_pop_top_node(this->mintree);
    bintree_insert_node(this->maxtree, top);
  } else if (1 < diff) {
    top = bintree_pop_top_node(this->maxtree);
    bintree_insert_node(this->mintree, top);
  }
  goto balancing;

done:
  return;
}

gint
_cmp_for_max (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? -1 : 1;
}

gint
_cmp_for_min (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? 1 : -1;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
