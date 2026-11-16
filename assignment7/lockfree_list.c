#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include "calclock.h"

MODULE_LICENSE("GPL");

#define NUM_THREADS 4
#define NODES_PER_THREAD 250000

struct animal {
    int total;
    struct list_head entry;
    atomic_t removed;
    struct list_head gc_entry;
};

struct cat {
    int var;
    struct list_head entry;
    atomic_t removed;
    struct list_head gc_entry;
};

static struct animal *head;
static struct task_struct *threads[NUM_THREADS];
static int thread_ids[NUM_THREADS];

static unsigned long long insert_time, insert_count;
static unsigned long long search_time, search_count;
static unsigned long long delete_time, delete_count;

static inline void INIT_LF_LIST_HEAD(struct list_head *list)
{
    list->next = NULL;
    list->prev = list;
}

static inline void lf_list_add_tail(struct list_head *entry, struct list_head *head)
{
    entry->prev = __sync_lock_test_and_set(&head->prev, entry);
    if (entry->prev == NULL)
        head = entry;
    else
        entry->prev->next = entry;
}

static inline void __gc_list_del(struct list_head *prev, struct list_head *next, struct list_head *head)
{
    if (next)
        next->prev = prev;
    else
        head->prev = prev;
    prev->next = next;
}

static inline void gc_list_del(struct list_head *entry, struct list_head *head)
{
    __gc_list_del(entry->prev, entry->next, head);
}

static void set_iter_range(int thread_id, int range_bound[])
{
    range_bound[0] = (thread_id - 1) * NODES_PER_THREAD;
    range_bound[1] = range_bound[0] + NODES_PER_THREAD - 1; 
}

static int select_target_index(int range_bound[])
{
    return range_bound[0] + (NODES_PER_THREAD / 2);
}

static void add_to_list(int thread_id, int range_bound[])
{
    struct timespec localclock[2];
    struct cat *new, *first = NULL;
    int i;

    for (i = range_bound[0]; i <= range_bound[1]; i++) {
        getrawmonotonic(&localclock[0]);

        new = kmalloc(sizeof(struct cat), GFP_KERNEL);
        new->var = i;
        INIT_LF_LIST_HEAD(&new->entry);
        INIT_LF_LIST_HEAD(&new->gc_entry);
        atomic_set(&new->removed, 0);

        if (!first)
            first = new;

        lf_list_add_tail(&new->entry, &head->entry);
        __sync_fetch_and_add(&head->total, 1);

        getrawmonotonic(&localclock[1]);
        calclock(localclock, &insert_time, &insert_count);
    }

    printk(KERN_INFO "thread #%d: inserted cat #%d-%d to the list, total: %u cats\n",
           thread_id, first->var, new->var, head->total);
}

static int search_list(int thread_id, int range_bound[])
{
    struct list_head *entry, *iter = &head->entry;
    struct cat *cur;
    struct timespec localclock[2];
    int target_idx = select_target_index(range_bound);

    while ((entry = iter) != NULL) {
        getrawmonotonic(&localclock[0]);

        if (__sync_val_compare_and_swap(&iter, entry, iter->next) != entry)
            continue;

        if (!entry->prev->next)
            continue;

        cur = list_entry(entry, struct cat, entry);

        if (atomic_read(&cur->removed))
            continue;

        getrawmonotonic(&localclock[1]);
        calclock(localclock, &search_time, &search_count);

        if (cur->var == target_idx) {
            printk(KERN_INFO "thread #%d: found cat #%d\n", thread_id, target_idx);
            return 0;
        }
    }

    return -ENODATA;
}

static int delete_from_list(int thread_id, int range_bound[])
{
    struct list_head *entry, *iter = &head->entry;
    struct cat *cur;
    struct timespec localclock[2];
    int start_idx = select_target_index(range_bound);
    int end_idx = range_bound[1];
    int del_target_count = (NODES_PER_THREAD / 2);
    int del_count = 0;

    while ((entry = iter) != NULL) {
        getrawmonotonic(&localclock[0]);

        if (__sync_val_compare_and_swap(&iter, entry, iter->next) != entry)
            continue;

        if (!entry->prev->next)
            continue;

        cur = list_entry(entry, struct cat, entry);

        if (atomic_read(&cur->removed))
            continue;

        if (cur->var >= start_idx && cur->var <= end_idx) {
            atomic_set(&cur->removed, 1);
            lf_list_add_tail(&cur->gc_entry, &head->gc_entry);
            __sync_fetch_and_sub(&head->total, 1);
            del_count++;
        }

        getrawmonotonic(&localclock[1]);
        calclock(localclock, &delete_time, &delete_count);

        if (del_count == del_target_count)
            break;
    }

    printk(KERN_INFO "thread #%d: marked cat #%d-%d as deleted, total: %d cats\n",
           thread_id, start_idx, end_idx, head->total);

    return 0;
}

static int work_fn(void *data)
{
    int range_bound[2];
    int err, thread_id = *(int*) data;

    set_iter_range(thread_id, range_bound);
    add_to_list(thread_id, range_bound);
    err = search_list(thread_id, range_bound);
    if (!err)
        delete_from_list(thread_id, range_bound);

    while (!kthread_should_stop()) {
        msleep(500);
    }
    printk(KERN_INFO "thread #%d stopped!\n", thread_id);

    return 0;
}

static int empty_garbage_list(void)
{
    struct cat *cur;
    struct list_head *entry, *iter = &head->gc_entry;
    unsigned int counter = 0;

    while ((entry = iter) != NULL) {
        iter = iter->next;
        if (!entry->prev->next)
            continue;

        cur = list_entry(entry, struct cat, gc_entry);

        gc_list_del(&cur->entry, &head->entry);
        gc_list_del(&cur->gc_entry, &head->gc_entry);

        kfree(cur);
        counter++;
    }
    printk(KERN_INFO "%s: freed %u cats\n", __func__, counter);

    return 0;
}

static void destroy_list(void)
{
    struct cat *cur;
    struct list_head *entry, *iter = &head->entry;

    while ((entry = iter) != NULL) {
        iter = iter->next;
        if (!entry->prev->next)
            continue;
        cur = list_entry(entry, struct cat, entry);
        gc_list_del(&cur->entry, &head->entry);
        kfree(cur);
    }
    head->total = 0;
}

static int __init lockfree_module_init(void)
{
    int i;

    printk("%s: Entering Lock-free Module!\n", __func__);

    head = kmalloc(sizeof(struct animal), GFP_KERNEL);
    if (!head)
        return -ENOMEM;

    INIT_LF_LIST_HEAD(&head->entry);
    INIT_LF_LIST_HEAD(&head->gc_entry);
    head->total = 0;
    atomic_set(&head->removed, 0);

    for (i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i + 1;
        threads[i] = kthread_run(work_fn, &thread_ids[i], "lockfree_thread%d", i + 1);
    }

    return 0;
}

static void __exit lockfree_module_cleanup(void)
{
    int i;

    printk("%s: Lock-free linked list insert time: %llu ns, count: %llu\n",
           __func__, insert_time, insert_count);
    printk("%s: Lock-free linked list search time: %llu ns, count: %llu\n",
           __func__, search_time, search_count);
    printk("%s: Lock-free linked list delete time: %llu ns, count: %llu\n",
           __func__, delete_time, delete_count);

    for (i = 0; i < NUM_THREADS; i++) {
        kthread_stop(threads[i]);
    }

    empty_garbage_list();
    printk("After gc: total %d cats\n", head->total);

    destroy_list();
    printk("After destroyed list: total %d cats\n", head->total);
    kfree(head);

    printk("%s: Exiting Lock-free Module!\n", __func__);
}

module_init(lockfree_module_init);
module_exit(lockfree_module_cleanup);
