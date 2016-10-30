/*
 * Copyright (c) 2013, TripNDroid Mobile Engineering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>

enum { ASYNC, SYNC };

static const int sync_read_expire  = 1 * HZ;	/* max time before a sync read is submitted. */
static const int sync_write_expire = 1 * HZ;	/* max time before a sync write is submitted. */
static const int async_read_expire  =  2 * HZ;	/* ditto for async, these limits are SOFT! */
static const int async_write_expire = 2 * HZ;	/* ditto for async, these limits are SOFT! */

static const int writes_starved = 1;		/* max times reads can starve a write */
static const int fifo_batch     = 16;		/* # of sequential requests treated as one
						   by the above parameters. For throughput. */

struct tripndroid_data {

	struct list_head fifo_list[2][2];

	unsigned int batched;
	unsigned int starved;

	int fifo_expire[2][2];
	int fifo_batch;
	int writes_starved;
};

static void tripndroid_merged_requests(struct request_queue *q, struct request *rq,
		    struct request *next)
{
	/*
	 * If next expires before rq, assign its expire time to rq
	 * and move into next position (next will be deleted) in fifo.
	 */
	if (!list_empty(&rq->queuelist) && !list_empty(&next->queuelist)) {
		if (time_before(rq_fifo_time(next), rq_fifo_time(rq))) {
			list_move(&rq->queuelist, &next->queuelist);
			rq_set_fifo_time(rq, rq_fifo_time(next));
		}
	}

	rq_fifo_clear(next);
}

static void tripndroid_add_request(struct request_queue *q, struct request *rq)
{
	struct tripndroid_data *td = q->elevator->elevator_data;
	const int sync = rq_is_sync(rq);
	const int data_dir = rq_data_dir(rq);

	rq_set_fifo_time(rq, jiffies + td->fifo_expire[sync][data_dir]);
	list_add(&rq->queuelist, &td->fifo_list[sync][data_dir]);
}

static struct request *tripndroid_expired_request(struct tripndroid_data *td, int sync, int data_dir)
{
	struct list_head *list = &td->fifo_list[sync][data_dir];
	struct request *rq;

	if (list_empty(list))
		return NULL;

	rq = rq_entry_fifo(list->next);

	if (time_after_eq(jiffies, rq_fifo_time(rq)))
		return rq;

	return NULL;
}

static struct request *tripndroid_choose_expired_request(struct tripndroid_data *td)
{
	struct request *rq;

	/* Asynchronous requests have priority over synchronous.
	 * Write requests have priority over read. */

	rq = tripndroid_expired_request(td, ASYNC, WRITE);
	if (rq)
		return rq;
	rq = tripndroid_expired_request(td, ASYNC, READ);
	if (rq)
		return rq;

	rq = tripndroid_expired_request(td, SYNC, WRITE);
	if (rq)
		return rq;
	rq = tripndroid_expired_request(td, SYNC, READ);
	if (rq)
		return rq;

	return NULL;
}

static struct request *tripndroid_choose_request(struct tripndroid_data *td, int data_dir)
{
	struct list_head *sync = td->fifo_list[SYNC];
	struct list_head *async = td->fifo_list[ASYNC];

	if (!list_empty(&sync[data_dir]))
		return rq_entry_fifo(sync[data_dir].next);
	if (!list_empty(&sync[!data_dir]))
		return rq_entry_fifo(sync[!data_dir].next);

	if (!list_empty(&async[data_dir]))
		return rq_entry_fifo(async[data_dir].next);
	if (!list_empty(&async[!data_dir]))
		return rq_entry_fifo(async[!data_dir].next);

	return NULL;
}

static inline void tripndroid_dispatch_request(struct tripndroid_data *td, struct request *rq)
{
	/* Dispatch the request */
	rq_fifo_clear(rq);
	elv_dispatch_add_tail(rq->q, rq);

	td->batched++;

	if (rq_data_dir(rq))
		td->starved = 0;
	else
		td->starved++;
}

static int tripndroid_dispatch_requests(struct request_queue *q, int force)
{
	struct tripndroid_data *td = q->elevator->elevator_data;
	struct request *rq = NULL;
	int data_dir = READ;

	if (td->batched > td->fifo_batch) {
		td->batched = 0;
		rq = tripndroid_choose_expired_request(td);
	}

	if (!rq) {
		if (td->starved > td->writes_starved)
			data_dir = WRITE;

		rq = tripndroid_choose_request(td, data_dir);
		if (!rq)
			return 0;
	}

	tripndroid_dispatch_request(td, rq);

	return 1;
}

static struct request *tripndroid_former_request(struct request_queue *q, struct request *rq)
{
	struct tripndroid_data *td = q->elevator->elevator_data;
	const int sync = rq_is_sync(rq);
	const int data_dir = rq_data_dir(rq);

	if (rq->queuelist.prev == &td->fifo_list[sync][data_dir])
		return NULL;

	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *tripndroid_latter_request(struct request_queue *q, struct request *rq)
{
	struct tripndroid_data *td = q->elevator->elevator_data;
	const int sync = rq_is_sync(rq);
	const int data_dir = rq_data_dir(rq);

	if (rq->queuelist.next == &td->fifo_list[sync][data_dir])
		return NULL;

	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static int tripndroid_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct tripndroid_data *td;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	td = kmalloc_node(sizeof(*td), GFP_KERNEL | __GFP_ZERO, q->node);
	if (!td){
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = td;

	INIT_LIST_HEAD(&td->fifo_list[SYNC][READ]);
	INIT_LIST_HEAD(&td->fifo_list[SYNC][WRITE]);
	INIT_LIST_HEAD(&td->fifo_list[ASYNC][READ]);
	INIT_LIST_HEAD(&td->fifo_list[ASYNC][WRITE]);

	td->batched = 0;
	td->fifo_expire[SYNC][READ] = sync_read_expire;
	td->fifo_expire[SYNC][WRITE] = sync_write_expire;
	td->fifo_expire[ASYNC][READ] = async_read_expire;
	td->fifo_expire[ASYNC][WRITE] = async_write_expire;
	td->fifo_batch = fifo_batch;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

static void tripndroid_exit_queue(struct elevator_queue *e)
{
	struct tripndroid_data *td = e->elevator_data;

	BUG_ON(!list_empty(&td->fifo_list[SYNC][READ]));
	BUG_ON(!list_empty(&td->fifo_list[SYNC][WRITE]));
	BUG_ON(!list_empty(&td->fifo_list[ASYNC][READ]));
	BUG_ON(!list_empty(&td->fifo_list[ASYNC][WRITE]));

	kfree(td);
}

/* Sysfs */
static ssize_t
tripndroid_var_show(int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t
tripndroid_var_store(int *var, const char *page, size_t count)
{
	*var = simple_strtol(page, NULL, 10);
	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV) \
static ssize_t __FUNC(struct elevator_queue *e, char *page) \
{ \
	struct tripndroid_data *td = e->elevator_data; \
	int __data = __VAR; \
	if (__CONV) \
		__data = jiffies_to_msecs(__data); \
		return tripndroid_var_show(__data, (page)); \
}
SHOW_FUNCTION(tripndroid_sync_read_expire_show, td->fifo_expire[SYNC][READ], 1);
SHOW_FUNCTION(tripndroid_sync_write_expire_show, td->fifo_expire[SYNC][WRITE], 1);
SHOW_FUNCTION(tripndroid_async_read_expire_show, td->fifo_expire[ASYNC][READ], 1);
SHOW_FUNCTION(tripndroid_async_write_expire_show, td->fifo_expire[ASYNC][WRITE], 1);
SHOW_FUNCTION(tripndroid_fifo_batch_show, td->fifo_batch, 0);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV) \
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count) \
{ \
	struct tripndroid_data *td = e->elevator_data; \
	int __data; \
	int ret = tripndroid_var_store(&__data, (page), count); \
	if (__data < (MIN)) \
		__data = (MIN); \
	else if (__data > (MAX)) \
		__data = (MAX); \
	if (__CONV) \
		*(__PTR) = msecs_to_jiffies(__data); \
	else \
		*(__PTR) = __data; \
	return ret; \
}
STORE_FUNCTION(tripndroid_sync_read_expire_store, &td->fifo_expire[SYNC][READ], 0, INT_MAX, 1);
STORE_FUNCTION(tripndroid_sync_write_expire_store, &td->fifo_expire[SYNC][WRITE], 0, INT_MAX, 1);
STORE_FUNCTION(tripndroid_async_read_expire_store, &td->fifo_expire[ASYNC][READ], 0, INT_MAX, 1);
STORE_FUNCTION(tripndroid_async_write_expire_store, &td->fifo_expire[ASYNC][WRITE], 0, INT_MAX, 1);
STORE_FUNCTION(tripndroid_fifo_batch_store, &td->fifo_batch, 0, INT_MAX, 0);
#undef STORE_FUNCTION

#define DD_ATTR(name) \
        __ATTR(name, S_IRUGO|S_IWUSR, tripndroid_##name##_show, \
                                      tripndroid_##name##_store)

static struct elv_fs_entry tripndroid_attrs[] = {
        DD_ATTR(sync_read_expire),
        DD_ATTR(sync_write_expire),
        DD_ATTR(async_read_expire),
        DD_ATTR(async_write_expire),
        DD_ATTR(fifo_batch),
        __ATTR_NULL
};

static struct elevator_type iosched_tripndroid = {
	.ops = {
		.elevator_merge_req_fn		= tripndroid_merged_requests,
		.elevator_dispatch_fn		= tripndroid_dispatch_requests,
		.elevator_add_req_fn		= tripndroid_add_request,
		.elevator_former_req_fn		= tripndroid_former_request,
		.elevator_latter_req_fn		= tripndroid_latter_request,
		.elevator_init_fn		= tripndroid_init_queue,
		.elevator_exit_fn		= tripndroid_exit_queue,
	},
	.elevator_attrs = tripndroid_attrs,
	.elevator_name = "tripndroid",
	.elevator_owner = THIS_MODULE,
};

static int __init tripndroid_init(void)
{
	elv_register(&iosched_tripndroid);
	return 0;
}

static void __exit tripndroid_exit(void)
{
	elv_unregister(&iosched_tripndroid);
}

module_init(tripndroid_init);
module_exit(tripndroid_exit);

MODULE_AUTHOR("TripNRaVeR");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TripNDroid IO Scheduler");