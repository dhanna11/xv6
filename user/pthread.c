#include "kernel/types.h"
#include "user/user.h"

int pthread_create(int *thread, void * start_routine, void * arg) {
    void * stack = malloc(4096);
    int pid = clone(start_routine, arg, stack); 
    if (pid < 0)
        return -1;

    *thread = pid;

    printf("pthread_create cloned process");    
    sleep(100);
    return 0;

}
