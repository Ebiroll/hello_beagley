#ifdef __linux__
/*
 * shared_memory.c - Linux implementation
 */

#include "shared_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* Initialize shared memory mapping */
int shared_memory_init(shared_memory_ctx_t *ctx) {
    if (!ctx) {
        fprintf(stderr, "Invalid context pointer\n");
        return -1;
    }
    
    /* Open /dev/mem for direct physical memory access */
    ctx->fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (ctx->fd < 0) {
        perror("Failed to open /dev/mem");
        fprintf(stderr, "Note: You need to run as root to access /dev/mem\n");
        return -1;
    }
    
    /* Map the shared audio memory region */
    ctx->size = SHARED_AUDIO_SIZE;
    ctx->mapped_addr = mmap(NULL, ctx->size, PROT_READ | PROT_WRITE, MAP_SHARED, 
                           ctx->fd, SHARED_AUDIO_BASE);
    
    if (ctx->mapped_addr == MAP_FAILED) {
        perror("Failed to map shared memory");
        close(ctx->fd);
        return -1;
    }
    
    /* Set up audio memory pointer */
    ctx->audio_mem = (shared_audio_memory_t*)ctx->mapped_addr;
    
    /* Initialize the shared memory structure if not already done */
    if (ctx->audio_mem->control.magic != SHARED_MEMORY_MAGIC) {
        printf("Initializing shared memory structure...\n");
        memset(ctx->audio_mem, 0, sizeof(shared_audio_memory_t));
        
        ctx->audio_mem->control.magic = SHARED_MEMORY_MAGIC;
        ctx->audio_mem->control.sample_rate = SAMPLE_RATE;
        ctx->audio_mem->control.chunk_size = CHUNK_SIZE;
        ctx->audio_mem->control.num_chunks = NUM_CHUNKS;
        ctx->audio_mem->control.write_index = 0;
        ctx->audio_mem->control.total_samples = 0;
        ctx->audio_mem->control.dsp_ready = 0;
        
        /* Initialize chunk status - all free */
        for (int i = 0; i < NUM_CHUNKS; i++) {
            ctx->audio_mem->control.chunk_status[i] = 0;
        }
        
        /* Initialize statistics */
        ctx->audio_mem->stats.min_processing_time_us = UINT64_MAX;
        ctx->audio_mem->stats.max_processing_time_us = 0;
        ctx->audio_mem->stats.chunks_processed = 0;
        ctx->audio_mem->stats.total_processing_time_us = 0;
        ctx->audio_mem->stats.processing_errors = 0;
    }
    
    printf("Shared memory initialized:\n");
    printf("  Physical address: 0x%lx\n", SHARED_AUDIO_BASE);
    printf("  Virtual address: %p\n", ctx->audio_mem);
    printf("  Size: %zu bytes\n", ctx->size);
    printf("  Buffer capacity: %d samples (%d chunks of %d samples)\n", 
           BUFFER_SIZE, NUM_CHUNKS, CHUNK_SIZE);
    
    return 0;
}

/* Cleanup shared memory */
void shared_memory_cleanup(shared_memory_ctx_t *ctx) {
    if (!ctx) return;
    
    if (ctx->mapped_addr && ctx->mapped_addr != MAP_FAILED) {
        munmap(ctx->mapped_addr, ctx->size);
    }
    
    if (ctx->fd >= 0) {
        close(ctx->fd);
    }
    
    memset(ctx, 0, sizeof(shared_memory_ctx_t));
}

/* Write audio samples to circular buffer */
int shared_memory_write_samples(shared_memory_ctx_t *ctx, int16_t *samples, uint32_t count) {
    if (!ctx || !ctx->audio_mem || !samples) return -1;
    
    shared_audio_memory_t *mem = ctx->audio_mem;
    
    for (uint32_t i = 0; i < count; i++) {
        mem->audio_buffer[mem->control.write_index] = samples[i];
        mem->control.write_index = (mem->control.write_index + 1) % BUFFER_SIZE;
        mem->control.total_samples++;
    }
    
    return 0;
}

/* Get chunk status */
uint32_t shared_memory_get_chunk_status(shared_memory_ctx_t *ctx, uint32_t chunk_id) {
    if (!ctx || !ctx->audio_mem || chunk_id >= NUM_CHUNKS) return 0;
    return ctx->audio_mem->control.chunk_status[chunk_id];
}

/* Set chunk status */
void shared_memory_set_chunk_status(shared_memory_ctx_t *ctx, uint32_t chunk_id, uint32_t status) {
    if (!ctx || !ctx->audio_mem || chunk_id >= NUM_CHUNKS) return;
    ctx->audio_mem->control.chunk_status[chunk_id] = status;
}

/* Get pointer to specific chunk */
int16_t* shared_memory_get_chunk_ptr(shared_memory_ctx_t *ctx, uint32_t chunk_id) {
    if (!ctx || !ctx->audio_mem || chunk_id >= NUM_CHUNKS) return NULL;
    return &ctx->audio_mem->audio_buffer[chunk_id * CHUNK_SIZE];
}

/* Print processing statistics */
void shared_memory_print_stats(shared_memory_ctx_t *ctx) {
    if (!ctx || !ctx->audio_mem) return;
    
    shared_audio_memory_t *mem = ctx->audio_mem;
    
    printf("\n=== Audio Processing Statistics ===\n");
    printf("DSP Ready: %s\n", mem->control.dsp_ready ? "Yes" : "No");
    printf("Total samples written: %lu\n", mem->control.total_samples);
    printf("Chunks processed: %lu\n", mem->stats.chunks_processed);
    printf("Processing errors: %u\n", mem->stats.processing_errors);
    
    if (mem->stats.chunks_processed > 0) {
        uint64_t avg_time = mem->stats.total_processing_time_us / mem->stats.chunks_processed;
        printf("Average processing time: %lu μs\n", avg_time);
        printf("Min processing time: %lu μs\n", mem->stats.min_processing_time_us);
        printf("Max processing time: %lu μs\n", mem->stats.max_processing_time_us);
    }
    
    printf("Chunk status: ");
    for (int i = 0; i < NUM_CHUNKS; i++) {
        printf("%d ", mem->control.chunk_status[i]);
    }
    printf("\n");
}

#endif /* __linux__ */
