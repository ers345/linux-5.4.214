#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "calclock.h"

MODULE_LICENSE("GPL");

#define NUM_THREADS 4
#define NODES_PER_THREAD 250000

struct animal {
    int total;
    struct list_head entry;
    spinlock_t list_lock;
};

struct cat {
    int var;
    struct list_head entry;
};

static struct animal *head;
static struct task_struct *threads[NUM_THREADS];
static int thread_ids[NUM_THREADS];

static unsigned long long insert_time, insert_count;
static unsigned long long search_time, search_count;
static unsigned long long delete_time, delete_count;

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
        INIT_LIST_HEAD(&new->entry);

        if (!first)
            first = new;

        spin_lock(&head->list_lock);
        list_add_tail(&new->entry, &head->entry);
        head->total++;
        spin_unlock(&head->list_lock);

        getrawmonotonic(&localclock[1]);
        calclock(localclock, &insert_time, &insert_count);
    }

    printk(KERN_INFO "thread #%d: inserted cat #%d-%d to the list, total: %u cats\n",
           thread_id, first->var, new->var, head->total);
}

static int search_list(int thread_id, int range_bound[])
{
    struct list_head *iter;
    struct cat *cur;
    struct timespec localclock[2];
    int target_idx = select_target_index(range_bound);
    int found = 0;

    spin_lock(&head->list_lock);
    iter = head->entry.next;
    while (iter != &head->entry) {
        getrawmonotonic(&localclock[0]);

        cur = list_entry(iter, struct cat, entry);

        if (cur->var == target_idx) {
            getrawmonotonic(&localclock[1]);
            calclock(localclock, &search_time, &search_count);
            found = 1;
            break;
        }

        iter = iter->next;

        getrawmonotonic(&localclock[1]);
        calclock(localclock, &search_time, &search_count);
    }
    spin_unlock(&head->list_lock);

    if (found) {
        printk(KERN_INFO "thread #%d: found cat #%d\n", thread_id, target_idx);
        return 0;
    }

    return -ENODATA;
}

static int delete_from_list(int thread_id, int range_bound[])
{
    struct list_head *iter;
    struct cat *cur;
    struct timespec localclock[2];
    int start_idx = select_target_index(range_bound);
    int end_idx = range_bound[1];
    int del_target_count = (NODES_PER_THREAD / 2);
    int del_count = 0;

    spin_lock(&head->list_lock);
    iter = head->entry.next;
    while (iter != &head->entry) {
        getrawmonotonic(&localclock[0]);

        cur = list_entry(iter, struct cat, entry);
        iter = iter->next;

        if (cur->var >= start_idx && cur->var <= end_idx) {
            list_del(&cur->entry);
            kfree(cur);
            head->total--;
            del_count++;
        }

        getrawmonotonic(&localclock[1]);
        calclock(localclock, &delete_time, &delete_count);

        if (del_count == del_target_count)
            break;
    }
    spin_unlock(&head->list_lock);

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

static void destroy_list(void)
{
    struct list_head *iter;
    struct cat *cur;

    iter = head->entry.next;
    while (iter != &head->entry) {
        cur = list_entry(iter, struct cat, entry);
        iter = iter->next;

        list_del(&cur->entry);
        kfree(cur);
    }
    head->total = 0;
}

static int __init spinlock_module_init(void)
{
    int i;

    printk(KERN_INFO "%s: Entering Spinlock Module!\n", __func__);

    head = kmalloc(sizeof(struct animal), GFP_KERNEL);
    if (!head)
        return -ENOMEM;

    INIT_LIST_HEAD(&head->entry);
    spin_lock_init(&head->list_lock);
    head->total = 0;

    for (i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i + 1;
        threads[i] = kthread_run(work_fn, &thread_ids[i], "spinlock_thread%d", i + 1);
    }

    return 0;
}

static void __exit spinlock_module_cleanup(void)
{
    int i;

    printk(KERN_INFO "%s: Spin-locked linked list insert time: %llu ns, count: %llu\n",
           __func__, insert_time, insert_count);
    printk(KERN_INFO "%s: Spin-locked linked list search time: %llu ns, count: %llu\n",
           __func__, search_time, search_count);
    printk(KERN_INFO "%s: Spin-locked linked list delete time: %llu ns, count: %llu\n",
           __func__, delete_time, delete_count);

    for (i = 0; i < NUM_THREADS; i++) {
        kthread_stop(threads[i]);
    }

    destroy_list();
    printk(KERN_INFO "After destroyed list: total %d cats\n", head->total);
    kfree(head);

    printk(KERN_INFO "%s: Exiting Spinlock Module!\n", __func__);
}

module_init(spinlock_module_init);
module_exit(spinlock_module_cleanup);
