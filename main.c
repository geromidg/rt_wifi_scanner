/**
  * @file main.c
  * @brief Contains the scheduler and the entry point for the implementation of WifiScanner on BCM2837.
  *
  * @author Dimitrios Panagiotis G. Geromichalos (dgeromichalos)
  * @date August, 2017
  */

/******************************** Inclusions *********************************/

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <sched.h>
#include <pthread.h>
#include <sys/mman.h>

#include "data_types.h"

/***************************** Macro Definitions *****************************/

/** The CPU affinity of the process. */
#define NUM_CPUS (0u)

/** The priority that will be given to the created tasks (threads) from the OS.
  * Since the PRREMPT_RT uses 50 as the priority of kernel tasklets and
  * interrupt handlers by default, the maximum available priority is chosen.
  * The priority of each task should be the same, since the Round-Robin
  * scheduling policy is used and each task is executed with the same time slice.
  */
#define TASK_PRIORITY (49u)

/** This is the maximum size of the stack which is guaranteed safe access without faulting. */
#define MAX_SAFE_STACK (128u * 1024u)

/** The number of nsecs per sec. */
#define NSEC_PER_SEC (1000000000ul)

/** The max size of the SSID. */
#define SSID_SIZE (64u)

/** The size of the SSID buffer. */
#define BUFFER_SIZE (32u)

/***************************** Type Definitions ******************************/

/** The SSID queue for the read/store (producer/consumer) model. */
struct SSIDQueue {
  char ssid_buffer[BUFFER_SIZE][SSID_SIZE];
  f32_t timestamp_buffer[BUFFER_SIZE];

  u32_t head, tail;
  u8_t full, empty;

  pthread_mutex_t mutex;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
};

/***************************** Static Variables ******************************/

/** The cycle time between the task calls. */
static u64_t read_cycle_time;

/** The timers of the tasks. */
static struct timespec read_task_timer;

/** The saved ssids and their timestamp. */
static u64_t ssid_num = 0;
static char** ssids;
static u32_t* num_timestamps;
static f32_t** timestamps;
static f32_t** latencies;

/** The queue to be used by the two tasks. */
static struct SSIDQueue ssid_queue;

/******************** Static General Function Prototypes *********************/

/**
  * @brief Prefault the stack segment that belongs to this process.
  * @return Void.
  */
static void prefaultStack(void);

/**
  * @brief Update the tasks's timer with a new interval.
  * @param task_timer The task's timer to be updated.
  * @param interval The interval needed to perform the update.
  * @return Void.
  */
static void updateInterval(struct timespec* task_timer, u64_t interval);

/**
  * @brief Get the current time.
  * @return The current time.
  */
static f32_t getCurrentTimestamp(void);

/**
  * @brief Add a new SSID and timestamp to the queue.
  * @param ssid The SSID to be added to the queue.
  * @param timestamp The timestamp that corresponds to the SSID.
  * @return Void.
  */
static void queueAdd(char* ssid, f32_t timestamp)

/**
  * @brief Pop an SSID and timestamp from the queue.
  * @param ssid The SSID to be popped from the queue.
  * @param timestamp The timestamp that corresponds to the SSID.
  * @return Void.
  */
static void queuePop(char* ssid, f32_t* timestamp)

/**
  * @brief Run a shell script to read and store the SSIDs to a buffer.
  * @return Void.
  */
static void readSSID(void)

/**
  * @brief Store locally the SSIDs and timestamp from the buffers.
  * @return Void.
  */
static void storeSSIDs(void)

/**
  * @brief Write SSIDs and their timestamps to a file.
  * @return Void.
  */
static void writeToFile(void);

/********************* Static Task Function Prototypes ***********************/

/**
  * @brief The initial task is run before the threads are created.
  * @return Void.
  */
static void INIT_TASK(int argc, char** argv);

/**
  * @brief The read task scans for wifi.
  * @return Void.
  */
static void* READ_TASK(void* ptr);

/**
  * @brief The store task stores scanned data to a file.
  * @return Void.
  */
static void* STORE_TASK(void* ptr);

/**
  * @brief The exit task is run after the threads are joined.
  * @return Void.
  */
static void EXIT_TASK(void);

/************************** Static General Functions *************************/

void prefaultStack(void)
{
        unsigned char dummy[MAX_SAFE_STACK];

        memset(dummy, 0, MAX_SAFE_STACK);

        return;
}

void updateInterval(struct timespec* task_timer, u64_t interval)
{
        task_timer->tv_nsec += interval;

        /* Normalize time (when nsec have overflowed) */
        while (task_timer->tv_nsec >= NSEC_PER_SEC)
        {
                task_timer->tv_nsec -= NSEC_PER_SEC;
                task_timer->tv_sec++;
        }
}


f32_t getCurrentTimestamp(void)
{
        struct timespec current_t;

        clock_gettime(CLOCK_MONOTONIC, &current_t);

        return current_t.tv_sec + (current_t.tv_nsec / (f32_t)1000000000u);
}

void queueAdd(char* ssid, f32_t timestamp)
{
        strcpy(ssid_queue.ssid_buffer[ssid_queue.tail], ssid);
        ssid_queue.timestamp_buffer[ssid_queue.tail] = timestamp;

        ssid_queue.tail++;

        if (ssid_queue.tail == BUFFER_SIZE)
                ssid_queue.tail = 0;
        if (ssid_queue.tail == ssid_queue.head)
                ssid_queue.full = 1;

        ssid_queue.empty = 0;
}

void queuePop(char* ssid, f32_t* timestamp)
{
        strcpy(ssid, ssid_queue.ssid_buffer[ssid_queue.head]);
        *timestamp = ssid_queue.timestamp_buffer[ssid_queue.head];

        ssid_queue.head++;

        if (ssid_queue.head == BUFFER_SIZE)
                ssid_queue.head = 0;
        if (ssid_queue.head == ssid_queue.tail)
                ssid_queue.empty = 1;

        ssid_queue.full = 0;
}

void readSSID(void)
{
        char ssid[SSID_SIZE];

        FILE *file = popen("/bin/bash searchWifi.sh", "r");

        if (file != NULL)
        {
                while (fgets(ssid, sizeof(ssid) - 1, file) != NULL)
                {
                        if (!ssid_queue.full && strncmp(ssid, "x00", 3))  /* skip if SSID is x00* */
                        {
                                queueAdd(ssid, getCurrentTimestamp());
                        }
                }

                pclose(file);
        }
}

void storeSSIDs(void)
{
        u64_t i;
        u8_t ssid_found;
        f32_t timestamp;
        char ssid[SSID_SIZE];

        queuePop(ssid, &timestamp);

        ssid_found = 0;

        for (i = 0; i < ssid_num; i++)
        {
                if (!strcmp(ssids[i], ssid) &&  /* equal */
                    timestamps[i][num_timestamps[i] - 1] != timestamp)
                {
                        num_timestamps[i]++;

                        timestamps[i] = realloc(timestamps[i], sizeof(f32_t) * num_timestamps[i]);
                        timestamps[i][num_timestamps[i] - 1] = timestamp;

                        latencies[i] = realloc(latencies[i], sizeof(f32_t) * num_timestamps[i]);
                        latencies[i][num_timestamps[i] - 1] = getCurrentTimestamp() - timestamp;

                        ssid_found = 1;
                        break;
                }
        }

        if (!ssid_found)
        {
                ssid_num++;

                ssids = realloc(ssids, sizeof(char*) * ssid_num);
                ssids[ssid_num - 1] = malloc(sizeof(char) * SSID_SIZE);
                strcpy(ssids[ssid_num - 1], ssid);

                num_timestamps = realloc(num_timestamps, sizeof(u32_t) * ssid_num);
                num_timestamps[ssid_num - 1] = 1;

                timestamps = realloc(timestamps, sizeof(f32_t*) * ssid_num);
                timestamps[ssid_num - 1] = malloc(sizeof(f32_t));
                timestamps[ssid_num - 1][0] = timestamp;

                latencies = realloc(latencies, sizeof(f32_t*) * ssid_num);
                latencies[ssid_num - 1] = malloc(sizeof(f32_t));
                latencies[ssid_num - 1][0] = getCurrentTimestamp() - timestamp;
        }
}

void writeToFile(void)
{
        u64_t i, j;

        FILE *file = fopen("ssids.txt", "w");

        if (file != NULL)
        {
                fprintf(file, "SSID\n");
                fprintf(file, "    timestamp  (latency)\n");
                fprintf(file, "=========================\n\n");

                for (i = 0; i < ssid_num; i++)
                {
                        fprintf(file, "%s", ssids[i]);

                        for (j = 0; j < num_timestamps[i]; j++)
                        {
                                fprintf(file, "    %.3f", timestamps[i][j]);
                                fprintf(file, "   (%.6f)\n", latencies[i][j]);
                        }

                        fprintf(file, "\n");
                }

                fclose(file);
        }
}

/************************** Static Task Functions ****************************/

void INIT_TASK(int argc, char** argv)
{
        if (argc != 2)
        {
                perror("Wrong number of arguments");
                exit(-4);
        }

        read_cycle_time = strtoul(argv[1], NULL, 0) * NSEC_PER_SEC;

        ssid_queue.empty = 1;
        ssid_queue.full = 0;
        ssid_queue.head = 0;
        ssid_queue.tail = 0;
        pthread_mutex_init(&ssid_queue.mutex, NULL);
        pthread_cond_init(&ssid_queue.not_empty, NULL);
        pthread_cond_init(&ssid_queue.not_full, NULL);
}

void* READ_TASK(void* ptr)
{
        /* Synchronize tasks's timer. */
        clock_gettime(CLOCK_MONOTONIC, &read_task_timer);

        while(1)
        {
                /* Calculate next shot */
                updateInterval(&read_task_timer, read_cycle_time);

                pthread_mutex_lock(&ssid_queue.mutex);
                while (ssid_queue.full)
                        pthread_cond_wait(&ssid_queue.not_full, &ssid_queue.mutex);

                readSSID();
                pthread_mutex_unlock(&ssid_queue.mutex);
                pthread_cond_signal(&ssid_queue.not_empty);

                /* Sleep for the remaining duration */
                (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &read_task_timer, NULL);
        }

        return (void*)NULL;
}

void* STORE_TASK(void* ptr)
{
        while(1)
        {
                pthread_mutex_lock(&ssid_queue.mutex);
                while (ssid_queue.empty)
                        pthread_cond_wait(&ssid_queue.not_empty, &ssid_queue.mutex);

                storeSSIDs();
                pthread_mutex_unlock(&ssid_queue.mutex);  /* TODO: Unlock after the critical section */
                pthread_cond_signal(&ssid_queue.not_full);

                writeToFile();
        }

        return (void*)NULL;
}

void EXIT_TASK(void)
{
        free(ssids);
        free(num_timestamps);
        free(timestamps);

        pthread_mutex_destroy(&ssid_queue.mutex);
        pthread_cond_destroy(&ssid_queue.not_empty);
        pthread_cond_destroy(&ssid_queue.not_full);
}

/********************************** Main Entry *******************************/

s32_t main(int argc, char** argv)
{
        cpu_set_t mask;

        pthread_t thread_1;
        pthread_attr_t attr_1;
        struct sched_param param_1;

        pthread_t thread_2;
        pthread_attr_t attr_2;
        struct sched_param param_2;

        /*********************************************************************/

        /* Lock memory. */
        if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1)
        {
                perror("mlockall failed");
                exit(-2);
        }

        prefaultStack();

        CPU_ZERO(&mask);
        CPU_SET(NUM_CPUS, &mask);
        if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
        {
                perror("Could not set CPU Affinity");
                exit(-3);
        }

        /*********************************************************************/

        INIT_TASK(argc, argv);

        /*********************************************************************/

        pthread_attr_init(&attr_1);
        pthread_attr_getschedparam(&attr_1, &param_1);
        param_1.sched_priority = TASK_PRIORITY;
        pthread_attr_setschedpolicy(&attr_1, SCHED_RR);
        pthread_attr_setschedparam(&attr_1, &param_1);

        (void)pthread_create(&thread_1, &attr_1, (void*)READ_TASK, (void*)NULL);
        pthread_setschedparam(thread_1, SCHED_RR, &param_1);

        /*********************************************************************/

        pthread_attr_init(&attr_2);
        pthread_attr_getschedparam(&attr_2, &param_2);
        param_2.sched_priority = TASK_PRIORITY;
        pthread_attr_setschedpolicy(&attr_2, SCHED_RR);
        pthread_attr_setschedparam(&attr_2, &param_2);

        (void)pthread_create(&thread_2, &attr_2, (void*)STORE_TASK, (void*)NULL);
        pthread_setschedparam(thread_2, SCHED_RR, &param_2);

        /*********************************************************************/

        pthread_join(thread_1, NULL);
        pthread_join(thread_2, NULL);

        EXIT_TASK();

        /*********************************************************************/

        exit(0);
}
