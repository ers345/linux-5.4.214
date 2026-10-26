#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

#define NUM_THREADS 4

static int counter = 0;
static int lock = 0;
static struct task_struct *threads[NUM_THREADS];

static int test_and_set_function(void *data) {
    int ret;

    while (!kthread_should_stop()) {
        while (__sync_lock_test_and_set(&lock, 1));
        ret = counter++;
        __sync_lock_test_and_set(&lock, 0);

        printk(KERN_INFO "pid[%d] %s: counter: %d\n", current->pid, __func__, ret);
        msleep(500);
    }
    return 0;
}

static int __init test_and_set_module_init(void) {
    int i;

    printk(KERN_INFO "%s: Entering Test and Set Module!\n", __func__);
    for (i = 0; i < NUM_THREADS; i++)
        threads[i] = kthread_run(test_and_set_function, NULL, "tas_thread%d", i);
    return 0;
}

static void __exit test_and_set_module_cleanup(void) {
    int i;

    for (i = 0; i < NUM_THREADS; i++)
        kthread_stop(threads[i]);
    printk(KERN_INFO "%s: Exiting Test and Set Module!\n", __func__);
}

module_init(test_and_set_module_init);
module_exit(test_and_set_module_cleanup);
