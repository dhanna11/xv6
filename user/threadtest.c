#include "kernel/types.h"
#include "user/pthread.h"
#include "user/user.h"

void print100() {

    int j = 1; 
    for (int i = 0; i < 1000000; i++) {
       j*=5;  
    }
    exit(0);
} 
 
int main() {
    int thread = 0;
    pthread_create(&thread, print100,0);
    sleep(100); 
    exit(0);    
}
