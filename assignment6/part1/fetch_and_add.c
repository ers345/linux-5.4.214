#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

#define NUM_THREADS 4

static int counter = 0;
static struct task_struct *threads[NUM_THREADS];

static int fetch_and_add_function(void *data) {
    int ret;

    while (!kthread_should_stop()) {
        ret = __sync_fetch_and_add(&counter, 1);
        printk(KERN_INFO "pid[%d] %s: counter: %d\n", current->pid, __func__, ret);
        msleep(500);
    }
    return 0;
}

static int __init fetch_and_add_module_init(void) {
    int i;

    printk(KERN_INFO "%s: Entering Fetch and Add Module!\n", __func__);
    for (i = 0; i < NUM_THREADS; i++)
        threads[i] = kthread_run(fetch_and_add_function, NULL, "faa_thread%d", i);
    return 0;
}

static void __exit fetch_and_add_module_cleanup(void) {
    int i;

    for (i = 0; i < NUM_THREADS; i++)
        kthread_stop(threads[i]);
    printk(KERN_INFO "%s: Exiting Fetch and Add Module!\n", __func__);
}

module_init(fetch_and_add_module_init);
module_exit(fetch_and_add_module_cleanup);
