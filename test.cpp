#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "mpsc.h"
#include <time.h>
size_t produce_failed_count = 0;
struct X {
        clock_t start;
        X(){
                start = clock();
        }
        ~X(){
                char buf[32];
                snprintf(buf, 32,"%f %f",(double)(clock()-start), double(produce_failed_count));
                perror(buf);
        }
}timer;
__thread int gindex = 0;

#define BUF_STRING2(x) #x
#define BUF_STRING(x) BUF_STRING2(x)
#define LOG_PATTERN  "File: " __FILE__ ", line: " BUF_STRING(__LINE__) ", "
bool produce(char* dest, unsigned int length){
   snprintf(dest, length,  LOG_PATTERN "|%d\r\n", gindex++);
   return true;
}

 void consume(char* dest, unsigned int length){
   printf(dest);
}
/*
void consume(char* dest, unsigned int length){
   write(1, dest, length);
   memset(dest, ' ', length);
}
*/

mpsc_queue mpsc_er(1<<12, 18);
void* producer(void*){
  while(gindex < 1000000){
	if(!mpsc_er.produce(produce, 64)){
		++produce_failed_count;
	}
  }
  return 0;
}
volatile bool bisexit = false;
void* consumer(void*){
  while(!bisexit){
    mpsc_er.consume(consume);
  }
  return 0;
}

#include <pthread.h>

int main(){
	const int max_threads = 18;
    pthread_t ntid[max_threads + 1];
    pthread_create(ntid, NULL, consumer, NULL);
	
	for(int i = 1; i<=max_threads;++i){
		pthread_create(ntid + i, NULL, producer, NULL);
	}
    
	for(int i = 1; i<=max_threads;++i){
		pthread_join(ntid[i], NULL);
	}

    bisexit = true;
    pthread_join(ntid[0], NULL);
    return 0;
}
