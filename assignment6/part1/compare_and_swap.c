#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

#define NUM_THREADS 4

static int counter = 0;
static int lock = 0;
static struct task_struct *threads[NUM_THREADS];

static int compare_and_swap_function(void *data) {
    int ret;

    while (!kthread_should_stop()) {
        while (__sync_val_compare_and_swap(&lock, 0, 1));
        ret = counter++;
        __sync_lock_test_and_set(&lock, 0);

        printk(KERN_INFO "pid[%d] %s: counter: %d\n", current->pid, __func__, ret);
        msleep(500);
    }
    return 0;
}

static int __init compare_and_swap_module_init(void) {
    int i;

    printk(KERN_INFO "%s: Entering Compare and Swap Module!\n", __func__);
    for (i = 0; i < NUM_THREADS; i++)
        threads[i] = kthread_run(compare_and_swap_function, NULL, "cas_thread%d", i);
    return 0;
}

static void __exit compare_and_swap_module_cleanup(void) {
    int i;

    for (i = 0; i < NUM_THREADS; i++)
        kthread_stop(threads[i]);
    printk(KERN_INFO "%s: Exiting Compare and Swap Module!\n", __func__);
}

module_init(compare_and_swap_module_init);
module_exit(compare_and_swap_module_cleanup);
