/*
 * audio_circular_buffer.c
 * 
 * Linux userspace application for managing audio circular buffer
 * and communicating with C7x DSP cores via rpmsg on BeagleY-AI
 * 
 * Uses actual device tree memory regions - no XDC dependency
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/rpmsg.h>
#include <pthread.h>
#include <math.h>
#include <signal.h>

#include "shared_memory.h"

/* RPMsg configuration */
#define RPMSG_SERVICE_NAME  "rpmsg-audio"
#define MAX_RPMSG_BUFF_SIZE 512
#define TONE_FREQUENCY      440.0f  /* 440 Hz test tone */

/* Message types */
typedef enum {
    MSG_PROCESS_CHUNK = 1,
    MSG_CHUNK_COMPLETE = 2,
    MSG_INIT = 3,
    MSG_STATUS = 4
} msg_type_t;

/* Message structures */
typedef struct {
    uint32_t msg_type;
    uint32_t chunk_id;
    uint32_t chunk_size;
    uint64_t timestamp;
} audio_msg_t;

typedef struct {
    uint32_t msg_type;
    uint32_t chunk_id;
    uint32_t status;  /* 0 = success, 1 = error */
    uint64_t processing_time_us;
} response_msg_t;

/* Global variables */
static shared_memory_ctx_t shared_ctx;
static int rpmsg_fd = -1;
static volatile int running = 1;
static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Function prototypes */
static int init_rpmsg_connection(void);
static void cleanup(void);
static void generate_test_tone(int16_t *buffer, uint32_t samples, double *phase);
static void *audio_generator_thread(void *arg);
static void *rpmsg_listener_thread(void *arg);
static int send_chunk_to_dsp(uint32_t chunk_id);
static void handle_dsp_response(response_msg_t *response);
static uint64_t get_timestamp_us(void);
static void signal_handler(int sig);

/* Get current timestamp in microseconds */
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* Find and open RPMsg device */
static int find_rpmsg_device(const char *service_name) {
    char dev_path[256];
    int fd = -1;
    
    /* Try different RPMsg device naming conventions */
    const char *rpmsg_patterns[] = {
        "/dev/rpmsg_ctrl%d",
        "/dev/rpmsg-ctrl%d", 
        "/dev/rpmsg%d"
    };
    
    for (int pattern = 0; pattern < 3; pattern++) {
        for (int i = 0; i < 32; i++) {
            snprintf(dev_path, sizeof(dev_path), rpmsg_patterns[pattern], i);
            fd = open(dev_path, O_RDWR | O_NONBLOCK);
            if (fd >= 0) {
                printf("Found RPMsg device: %s\n", dev_path);
                return fd;
            }
        }
    }
    
    fprintf(stderr, "No RPMsg device found\n");
    return -1;
}

/* Initialize RPMsg connection to C7x DSP */
static int init_rpmsg_connection(void) {
    /* Find and open RPMsg device */
    rpmsg_fd = find_rpmsg_device(RPMSG_SERVICE_NAME);
    if (rpmsg_fd < 0) {
        fprintf(stderr, "Failed to open RPMsg device\n");
        fprintf(stderr, "Make sure:\n");
        fprintf(stderr, "1. DSP firmware is loaded and running\n");
        fprintf(stderr, "2. RPMsg modules are loaded (rpmsg_char, rpmsg_ns)\n");
        fprintf(stderr, "3. You have permission to access /dev/rpmsg*\n");
        return -1;
    }
    
    printf("RPMsg connection established (fd=%d)\n", rpmsg_fd);
    return 0;
}

/* Generate 440Hz test tone */
static void generate_test_tone(int16_t *buffer, uint32_t samples, double *phase) {
    const double phase_increment = 2.0 * M_PI * TONE_FREQUENCY / SAMPLE_RATE;
    const int16_t amplitude = 16000;  /* ~50% of max 16-bit value */
    
    for (uint32_t i = 0; i < samples; i++) {
        buffer[i] = (int16_t)(amplitude * sin(*phase));
        *phase += phase_increment;
        if (*phase >= 2.0 * M_PI) {
            *phase -= 2.0 * M_PI;
        }
    }
}

/* Send audio chunk to DSP for processing */
static int send_chunk_to_dsp(uint32_t chunk_id) {
    if (chunk_id >= NUM_CHUNKS) {
        fprintf(stderr, "Invalid chunk ID: %d\n", chunk_id);
        return -1;
    }
    
    audio_msg_t msg;
    msg.msg_type = MSG_PROCESS_CHUNK;
    msg.chunk_id = chunk_id;
    msg.chunk_size = CHUNK_SIZE;
    msg.timestamp = get_timestamp_us();
    
    ssize_t bytes_sent = write(rpmsg_fd, &msg, sizeof(msg));
    if (bytes_sent != sizeof(msg)) {
        if (bytes_sent < 0) {
            perror("Failed to send message to DSP");
        } else {
            fprintf(stderr, "Partial write: sent %zd bytes instead of %zu\n", 
                    bytes_sent, sizeof(msg));
        }
        return -1;
    }
    
    printf("Sent chunk %d to DSP for processing\n", chunk_id);
    return 0;
}

/* Handle response from DSP */
static void handle_dsp_response(response_msg_t *response) {
    printf("DSP completed chunk %d: status=%d, time=%lu μs\n",
           response->chunk_id, response->status, response->processing_time_us);
    
    if (response->chunk_id < NUM_CHUNKS) {
        shared_memory_set_chunk_status(&shared_ctx, response->chunk_id, 3);  /* Mark as done */
        
        /* Update shared memory statistics */
        if (shared_ctx.audio_mem && response->status == 0) {
            shared_ctx.audio_mem->stats.chunks_processed++;
            shared_ctx.audio_mem->stats.total_processing_time_us += response->processing_time_us;
            
            if (response->processing_time_us < shared_ctx.audio_mem->stats.min_processing_time_us) {
                shared_ctx.audio_mem->stats.min_processing_time_us = response->processing_time_us;
            }
            
            if (response->processing_time_us > shared_ctx.audio_mem->stats.max_processing_time_us) {
                shared_ctx.audio_mem->stats.max_processing_time_us = response->processing_time_us;
            }
        } else if (response->status != 0) {
            shared_ctx.audio_mem->stats.processing_errors++;
        }
    }
}

/* Audio generator thread */
static void *audio_generator_thread(void *arg) {
    int16_t chunk_buffer[CHUNK_SIZE];
    double phase = 0.0;
    uint32_t current_chunk = 0;
    
    printf("Audio generator thread started\n");
    
    while (running) {
        /* Generate a chunk of test audio */
        generate_test_tone(chunk_buffer, CHUNK_SIZE, &phase);
        
        /* Write to shared memory */
        pthread_mutex_lock(&audio_mutex);
        
        /* Write samples to the circular buffer */
        shared_memory_write_samples(&shared_ctx, chunk_buffer, CHUNK_SIZE);
        
        /* Check if this chunk is ready for processing */
        uint32_t chunk_status = shared_memory_get_chunk_status(&shared_ctx, current_chunk);
        
        if (chunk_status == 0) {  /* Chunk is free */
            /* Copy audio data to the specific chunk location */
            int16_t *chunk_ptr = shared_memory_get_chunk_ptr(&shared_ctx, current_chunk);
            if (chunk_ptr) {
                memcpy(chunk_ptr, chunk_buffer, CHUNK_SIZE * sizeof(int16_t));
                
                /* Mark chunk as ready for processing */
                shared_memory_set_chunk_status(&shared_ctx, current_chunk, 1);
                
                pthread_mutex_unlock(&audio_mutex);
                
                /* Send chunk to DSP */
                shared_memory_set_chunk_status(&shared_ctx, current_chunk, 2);  /* Mark as processing */
                send_chunk_to_dsp(current_chunk);
                
                /* Move to next chunk */
                current_chunk = (current_chunk + 1) % NUM_CHUNKS;
            } else {
                pthread_mutex_unlock(&audio_mutex);
            }
        } else {
            pthread_mutex_unlock(&audio_mutex);
            printf("Chunk %d not ready (status=%d), skipping...\n", current_chunk, chunk_status);
        }
        
        /* Sleep for chunk duration (~125ms for 6000 samples at 48kHz) */
        usleep(125000);
    }
    
    printf("Audio generator thread stopped\n");
    return NULL;
}

/* RPMsg listener thread */
static void *rpmsg_listener_thread(void *arg) {
    response_msg_t response;
    ssize_t bytes_received;
    
    printf("RPMsg listener thread started\n");
    
    while (running) {
        bytes_received = read(rpmsg_fd, &response, sizeof(response));
        
        if (bytes_received == sizeof(response)) {
            handle_dsp_response(&response);
        } else if (bytes_received > 0) {
            printf("Received partial message: %zd bytes\n", bytes_received);
        } else if (bytes_received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Error reading from RPMsg");
                break;
            }
        }
        
        /* Small delay to prevent busy waiting */
        usleep(10000);  /* 10ms */
    }
    
    printf("RPMsg listener thread stopped\n");
    return NULL;
}

/* Cleanup resources */
static void cleanup(void) {
    running = 0;
    
    if (rpmsg_fd >= 0) {
        close(rpmsg_fd);
        rpmsg_fd = -1;
    }
    
    shared_memory_cleanup(&shared_ctx);
    
    printf("Cleanup completed\n");
}

/* Signal handler */
static void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down gracefully...\n", sig);
    running = 0;
}

/* Print usage information */
static void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -s, --stats    Print statistics every 5 seconds\n");
    printf("  -v, --verbose  Enable verbose output\n");
    printf("\n");
    printf("This application generates a 440Hz test tone and sends audio chunks\n");
    printf("to the C7x DSP cores for processing via shared memory and RPMsg.\n");
}

/* Main function */
int main(int argc, char *argv[]) {
    pthread_t audio_thread, rpmsg_thread;
    int show_stats = 0;
    int verbose = 0;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stats") == 0) {
            show_stats = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        }
    }
    
    printf("=== Audio Circular Buffer Application for BeagleY-AI ===\n");
    printf("Sample Rate: %d Hz\n", SAMPLE_RATE);
    printf("Buffer: %d samples (%d chunks of %d samples each)\n", 
           BUFFER_SIZE, NUM_CHUNKS, CHUNK_SIZE);
    printf("Shared Memory: Physical 0x%lx, Size %d KB\n", 
           SHARED_AUDIO_BASE, SHARED_AUDIO_SIZE / 1024);
    printf("Test Tone: %.1f Hz\n", TONE_FREQUENCY);
    
    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize shared memory */
    if (shared_memory_init(&shared_ctx) < 0) {
        fprintf(stderr, "Failed to initialize shared memory\n");
        return 1;
    }
    
    /* Initialize RPMsg connection */
    if (init_rpmsg_connection() < 0) {
        fprintf(stderr, "Failed to initialize RPMsg connection\n");
        cleanup();
        return 1;
    }
    
    /* Send initialization message to DSP */
    audio_msg_t init_msg;
    init_msg.msg_type = MSG_INIT;
    init_msg.chunk_id = 0;
    init_msg.chunk_size = CHUNK_SIZE;
    init_msg.timestamp = get_timestamp_us();
    
    if (write(rpmsg_fd, &init_msg, sizeof(init_msg)) != sizeof(init_msg)) {
        fprintf(stderr, "Failed to send initialization message\n");
        cleanup();
        return 1;
    }
    
    printf("Sent initialization message to DSP\n");
    
    /* Wait a moment for DSP to initialize */
    sleep(1);
    
    /* Mark DSP as ready in shared memory */
    if (shared_ctx.audio_mem) {
        shared_ctx.audio_mem->control.dsp_ready = 1;
    }
    
    /* Create threads */
    if (pthread_create(&audio_thread, NULL, audio_generator_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create audio thread\n");
        cleanup();
        return 1;
    }
    
    if (pthread_create(&rpmsg_thread, NULL, rpmsg_listener_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create RPMsg thread\n");
        cleanup();
        return 1;
    }
    
    printf("\nApplication running... Press Ctrl+C to stop\n");
    if (show_stats) {
        printf("Statistics will be displayed every 5 seconds\n");
    }
    
    /* Main loop - optionally show statistics */
    while (running) {
        sleep(5);
        
        if (show_stats && shared_ctx.audio_mem) {
            shared_memory_print_stats(&shared_ctx);
        }
    }
    
    /* Wait for threads to complete */
    printf("Waiting for threads to finish...\n");
    pthread_join(audio_thread, NULL);
    pthread_join(rpmsg_thread, NULL);
    
    /* Final statistics */
    if (shared_ctx.audio_mem) {
        shared_memory_print_stats(&shared_ctx);
    }
    
    cleanup();
    printf("Application terminated\n");
    
    return 0;
}
