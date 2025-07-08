/*
 * shared_memory.h
 * 
 * Shared memory management for audio circular buffer between
 * Linux userspace and C7x DSP cores on BeagleY-AI
 * Uses actual device tree memory regions
 */

#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <stdint.h>

/* Memory layout from BeagleY-AI device tree */
#define C7X_0_DMA_BASE          0xa3000000  /* c7x_0_dma_memory_region */
#define C7X_0_DMA_SIZE          0x100000    /* 1MB */
#define C7X_1_DMA_BASE          0xa4000000  /* c7x_1_dma_memory_region */
#define C7X_1_DMA_SIZE          0x100000    /* 1MB */
#define RTOS_IPC_BASE           0xa5000000  /* rtos_ipc_memory_region */
#define RTOS_IPC_SIZE           0x1c00000   /* 28MB */

/* Use part of C7x_0 DMA region for shared audio buffer */
#define SHARED_AUDIO_BASE       (C7X_0_DMA_BASE + 0x80000)  /* Use upper 512KB */
#define SHARED_AUDIO_SIZE       0x80000     /* 512KB for audio buffer */

/* Audio configuration */
#define SAMPLE_RATE             48000
#define CHUNK_SIZE              6000        /* 6000 samples per chunk */
#define NUM_CHUNKS              8
#define BUFFER_SIZE             (CHUNK_SIZE * NUM_CHUNKS)  /* Total samples */

/* Shared memory structure - fits in 512KB */
typedef struct {
    /* Audio circular buffer - 16-bit samples */
    int16_t audio_buffer[BUFFER_SIZE];  /* ~96KB for audio data */
    
    /* Control structure */
    struct {
        volatile uint32_t write_index;      /* Current write position */
        volatile uint32_t chunk_status[NUM_CHUNKS]; /* 0=free, 1=ready, 2=processing, 3=done */
        uint32_t sample_rate;
        uint32_t chunk_size;
        uint32_t num_chunks;
        volatile uint64_t total_samples;
        uint32_t magic;                     /* Validation magic number */
        volatile uint32_t dsp_ready;        /* DSP initialization status */
    } control;
    
    /* Processing statistics */
    struct {
        volatile uint64_t chunks_processed;
        volatile uint64_t total_processing_time_us;
        volatile uint64_t min_processing_time_us;
        volatile uint64_t max_processing_time_us;
        volatile uint32_t processing_errors;
    } stats;
    
} shared_audio_memory_t;

#define SHARED_MEMORY_MAGIC 0xABCD1234

/* Linux userspace functions */
#ifdef __linux__

#include <sys/mman.h>
#include <fcntl.h>

typedef struct {
    void *mapped_addr;
    int fd;
    size_t size;
    shared_audio_memory_t *audio_mem;
} shared_memory_ctx_t;

/* Function prototypes */
int shared_memory_init(shared_memory_ctx_t *ctx);
void shared_memory_cleanup(shared_memory_ctx_t *ctx);
int shared_memory_write_samples(shared_memory_ctx_t *ctx, int16_t *samples, uint32_t count);
uint32_t shared_memory_get_chunk_status(shared_memory_ctx_t *ctx, uint32_t chunk_id);
void shared_memory_set_chunk_status(shared_memory_ctx_t *ctx, uint32_t chunk_id, uint32_t status);
int16_t* shared_memory_get_chunk_ptr(shared_memory_ctx_t *ctx, uint32_t chunk_id);
void shared_memory_print_stats(shared_memory_ctx_t *ctx);

#endif /* __linux__ */

#endif /* SHARED_MEMORY_H */
