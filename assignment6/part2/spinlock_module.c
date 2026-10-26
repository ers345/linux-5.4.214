#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "calclock.h"

MODULE_LICENSE("GPL");

#define NUM_THREADS 4
#define NODES_PER_THREAD 250000

struct my_node {
    int data;
    struct list_head list;
};

static struct list_head my_list;
static DEFINE_SPINLOCK(my_lock);
static struct task_struct *threads[NUM_THREADS];
static int thread_ids[NUM_THREADS];

static unsigned long long insert_time, insert_count;
static unsigned long long search_time, search_count;
static unsigned long long delete_time, delete_count;

static void set_iter_range(int thread_id, int range_bound[]) {
    range_bound[0] = (thread_id - 1) * NODES_PER_THREAD;
    range_bound[1] = thread_id * NODES_PER_THREAD - 1;
}

static void *add_to_list(int thread_id, int range_bound[]) {
    struct timespec localclock[2];
    struct my_node *new, *first = NULL;
    int i;

    printk(KERN_INFO "thread #%d range: %d ~ %d\n", thread_id, range_bound[0], range_bound[1]);
    for (i = range_bound[0]; i <= range_bound[1]; i++) {
        new = kmalloc(sizeof(struct my_node), GFP_KERNEL);
        new->data = i;
        if (!first)
            first = new;

        getrawmonotonic(&localclock[0]);
        spin_lock(&my_lock);
        list_add_tail(&new->list, &my_list);
        spin_unlock(&my_lock);
        getrawmonotonic(&localclock[1]);
        calclock(localclock, &insert_time, &insert_count);
    }
    return first;
}

static int search_list(int thread_id, void *data, int range_bound[]) {
    struct timespec localclock[2];
    struct my_node *cur = (struct my_node *) data;
    int i;

    for (i = range_bound[0]; i <= range_bound[1]; i++) {
        getrawmonotonic(&localclock[0]);
        spin_lock(&my_lock);
        while (cur->data != i)
            cur = list_entry(cur->list.next, struct my_node, list);
        spin_unlock(&my_lock);
        getrawmonotonic(&localclock[1]);
        calclock(localclock, &search_time, &search_count);
    }
    printk(KERN_INFO "thread #%d searched range: %d ~ %d\n", thread_id, range_bound[0], range_bound[1]);
    return 0;
}

static int delete_from_list(int thread_id, void *data, int range_bound[]) {
    struct timespec localclock[2];
    struct my_node *cur = (struct my_node *) data, *tmp;
    int i;

    for (i = range_bound[0]; i <= range_bound[1]; i++) {
        getrawmonotonic(&localclock[0]);
        spin_lock(&my_lock);
        tmp = cur;
        while (i < range_bound[1] && cur->data != i + 1)
            cur = list_entry(cur->list.next, struct my_node, list);
        list_del(&tmp->list);
        spin_unlock(&my_lock);
        getrawmonotonic(&localclock[1]);
        calclock(localclock, &delete_time, &delete_count);
        kfree(tmp);
    }
    printk(KERN_INFO "thread #%d deleted range: %d ~ %d\n", thread_id, range_bound[0], range_bound[1]);
    return 0;
}

static int work_fn(void *data) {
    int range_bound[2];
    int thread_id = *(int *)data;
    void *ret;

    set_iter_range(thread_id, range_bound);
    ret = add_to_list(thread_id, range_bound);
    search_list(thread_id, ret, range_bound);
    delete_from_list(thread_id, ret, range_bound);

    while (!kthread_should_stop())
        msleep(500);
    printk(KERN_INFO "thread #%d stopped!\n", thread_id);
    return 0;
}

static int __init spinlock_module_init(void) {
    int i;

    printk(KERN_INFO "%s: Entering Spinlock Module!\n", __func__);
    INIT_LIST_HEAD(&my_list);
    for (i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i + 1;
        threads[i] = kthread_run(work_fn, &thread_ids[i], "spin_thread%d", i + 1);
    }
    return 0;
}

static void __exit spinlock_module_cleanup(void) {
    int i;

    printk(KERN_INFO "%s: Spinlock linked list insert time: %llu ns, count: %llu\n", __func__, insert_time, insert_count);
    printk(KERN_INFO "%s: Spinlock linked list search time: %llu ns, count: %llu\n", __func__, search_time, search_count);
    printk(KERN_INFO "%s: Spinlock linked list delete time: %llu ns, count: %llu\n", __func__, delete_time, delete_count);
    for (i = 0; i < NUM_THREADS; i++)
        kthread_stop(threads[i]);
    printk(KERN_INFO "%s: Exiting Spinlock Module!\n", __func__);
}

module_init(spinlock_module_init);
module_exit(spinlock_module_cleanup);
