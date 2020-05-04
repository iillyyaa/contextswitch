/*
 * FILE: measureSwitch.c
 * DESCRIPTION: Two processes communication via a pipe.
 * OUTPUT:	time2 = Overhead of traversing through array + pipe overhead 
 * 		        + context switch overhead
 *		
 * 		use measureSingle to get time1
 *		context switch cost = time2 - time1
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/time.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <linux/unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "util.h"

void showUsage()
{
  fprintf( stderr, "Usage:\nmeasureSwitch <options>\n\
    -n   <number>  size of the array to work on (in byte). default 0.\n\
    -s   <number>  access stride size (in byte). default 0\n");
}


void measureSwitch1(register int array_size, register int stride,  
	register int *p1, register int *p2, register char *msg,  
	register double *f){

    register int i, j, m;

    for ( i=0; i<LOOP; i++) {
        read(p2[0], msg, 1);
        for ( m=0; m<stride; m++)
            for ( j=m; j<array_size; j=j+stride)
                f[j]++;
        write(p1[1], msg, 1);
    }
}

void measureSwitch2(register int array_size, register int stride,  
	register int *p1, register int *p2, register char *msg, 
	register double *f){

    register int i, j, m;

    for ( i=0; i<LOOP; i++) {
        for ( m=0; m<stride; m++)
            for ( j=m; j<array_size; j=j+stride)
                f[j]++;
        write(p2[1], msg, 1);
        read(p1[0], msg, 1);
    }

}

/** Args that need to be shared between threads **/
typedef struct {
    int array_size;
    int stride;
    int p1[2];
    int p2[2];
} args_t;

void* measureSwitch1ThreadStart(void* vargs)
{
    args_t *args = (args_t*)vargs;
    double* f = (double*) malloc(args->array_size*sizeof(double));
    char message;
    if (f==NULL) {
        perror("malloc fails");
        exit (1);
    }
    memset((void *)f, 0x00, args->array_size*sizeof(double));
    measureSwitch1(args->array_size, args->stride, args->p1, args->p2, &message, f);
    sleep(1);
    memdump(f, sizeof(double)*args->array_size);
    free(f);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    int i, j, len, ret;
    args_t args;
    double *f, start_time, time1, min2=LARGE;
    char message, ch;
    short round;
    pid_t pid, p = 0;
    unsigned long new_mask = 2;
    struct sched_param sp;

    len = sizeof(new_mask);
#ifdef MULTIPROCESSOR
    ret = sched_setaffinity(p, len, &new_mask);
    if(ret==-1){
	perror("sched_setaffinity 1");
        exit(1);
    }
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    ret=sched_setscheduler(0, SCHED_FIFO, &sp);
    if(ret==-1){
	perror("sched_setscheduler 1");
        exit(1);
    }
#endif

    while ((ch = getopt(argc, argv, "s:n:")) != EOF) {
        switch (ch) {
            case 'n': /* number of doubles in the array */
                args.array_size=atoi(optarg);
                args.array_size=args.array_size/sizeof(double);
                break;
            case 's': 
		args.stride=atoi(optarg);
		args.stride=args.stride/sizeof(double);
                break;
            default:
                fprintf(stderr, "Unknown option character.\n");
                showUsage();
                exit(1);
        }
    }
    if (args.stride > args.array_size){
        printf("Warning: stride is bigger than array_size. "
               "Sequential access. \n");
    }

    /* create two pipes: p1[0], p2[0] for read; p1[1], p2[1] for write */
    if (pipe (args.p1) < 0) {
        perror ("create pipe1");
        return -1;
    }
    if (pipe (args.p2) < 0) {
        perror ("create pipe2");
        return -1;
    }
    /* communicate between two threads */
    // fork
    printf("time2 with context swith: \t");
    fflush(stdout);
    for(round=0; round<N; round++){
	
	flushCache();
        pthread_t tid;
        int s = pthread_create(&tid, NULL, &measureSwitch1ThreadStart, &args);
        if (s < 0) {
            perror("malloc fails");
            exit (1);
        }
            
        {
            // parent thread
            f = (double*) malloc(args.array_size*sizeof(double));
            if (f==NULL) {
                perror("malloc fails");
                exit (1);
            }
	    memset((void *)f, 0x00, args.array_size*sizeof(double));
	    sleep(1);
            start_time = gethrtime_x86();
            measureSwitch2(args.array_size, args.stride, args.p1, args.p2, &message, f);
            time1 = gethrtime_x86()-start_time;
	    time1 = time1/(2*LOOP)*MILLION;
            if(min2 > time1)
                min2 = time1;
            printf("%f\t", time1);
            fflush(stdout);
            pthread_join(tid, NULL);
            memdump(f, sizeof(double)*args.array_size);
            free(f);
        }
	sleep(1);
    }
    printf("\nmeasureSwitch: array_size = %lu, stride = %d, min time2 = %.15f\n", 
       args.array_size*sizeof(double), (int)(args.stride*sizeof(double)), min2);
    return 0;
}

