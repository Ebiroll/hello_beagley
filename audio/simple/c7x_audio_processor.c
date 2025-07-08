/*
 * c7x_audio_processor.c
 * 
 * Simple C7x DSP firmware for processing audio chunks
 * Designed for BeagleY-AI AM67A using Linux remoteproc (no XDC)
 * Uses shared memory in C7x DMA region
 */

#include <stdint.h>
#include <string.h>

/* Basic remoteproc resource table */
#include "rsc_table.h"

/* Memory addresses from device tree */
#define SHARED_AUDIO_BASE       0xa3080000  /* Upper 512KB of C7x_0 DMA region */
#define SHARED_AUDIO_SIZE       0x80000     /* 512KB */

/* Audio configuration - must match Linux app */
#define SAMPLE_RATE             48000
#define CHUNK_SIZE              6000
#define NUM_CHUNKS              8
#define BUFFER_SIZE             (CHUNK_SIZE * NUM_CHUNKS)

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
    uint32_t status;
    uint64_t processing_time_us;
} response_msg_t;

/* Shared memory structure - must match Linux */
typedef struct {
    int16_t audio_buffer[BUFFER_SIZE];
    struct {
        volatile uint32_t write_index;
        volatile uint32_t chunk_status[NUM_CHUNKS];
        uint32_t sample_rate;
        uint32_t chunk_size;
        uint32_t num_chunks;
        volatile uint64_t total_samples;
        uint32_t magic;
        volatile uint32_t dsp_ready;
    } control;
    struct {
        volatile uint64_t chunks_processed;
        volatile uint64_t total_processing_time_us;
        volatile uint64_t min_processing_time_us;
        volatile uint64_t max_processing_time_us;
        volatile uint32_t processing_errors;
    } stats;
} shared_audio_memory_t;

#define SHARED_MEMORY_MAGIC 0xABCD1234

/* Global variables */
static volatile shared_audio_memory_t *audio_mem;
static int16_t local_buffer[CHUNK_SIZE];

/* Simple cycle counter for timing (C7x specific) */
static inline uint64_t get_cycles(void) {
    /* For C7x, use built-in cycle counter if available */
    /* Fallback to simple counter for compilation */
    static uint64_t counter = 0;
    return counter++;
}

/* Initialize shared memory access */
static void init_shared_memory(void) {
    /* Direct access to shared memory region */
    audio_mem = (volatile shared_audio_memory_t*)SHARED_AUDIO_BASE;
    
    /* Validate magic number - if not valid, wait for Linux to initialize */
    while (audio_mem->control.magic != SHARED_MEMORY_MAGIC) {
        /* Wait for Linux app to initialize shared memory */
        for (volatile int i = 0; i < 10000; i++);  /* Simple delay */
    }
    
    /* Mark DSP as ready */
    audio_mem->control.dsp_ready = 1;
}

/* Simple audio processing functions */
static void convert_int16_to_float(int16_t *input, float *output, uint32_t size) {
    const float scale = 1.0f / 32768.0f;
    for (uint32_t i = 0; i < size; i++) {
        output[i] = (float)input[i] * scale;
    }
}

static void convert_float_to_int16(float *input, int16_t *output, uint32_t size) {
    const float scale = 32767.0f;
    for (uint32_t i = 0; i < size; i++) {
        float sample = input[i] * scale;
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;
        output[i] = (int16_t)sample;
    }
}

/* Simple low-pass filter */
static void audio_filter_lowpass(float *input, float *output, uint32_t size) {
    static float prev_sample = 0.0f;
    const float alpha = 0.1f;
    
    for (uint32_t i = 0; i < size; i++) {
        output[i] = alpha * input[i] + (1.0f - alpha) * prev_sample;
        prev_sample = output[i];
    }
}

/* Audio amplification */
static void audio_amplify(float *input, float *output, uint32_t size, float gain) {
    for (uint32_t i = 0; i < size; i++) {
        output[i] = input[i] * gain;
    }
}

/* Main audio processing function */
static int32_t process_audio_chunk(uint32_t chunk_id) {
    if (chunk_id >= NUM_CHUNKS) return -1;
    
    uint64_t start_cycles = get_cycles();
    
    /* Get pointer to chunk in shared memory */
    int16_t *chunk_ptr = (int16_t*)&audio_mem->audio_buffer[chunk_id * CHUNK_SIZE];
    
    /* Copy to local buffer for processing */
    for (uint32_t i = 0; i < CHUNK_SIZE; i++) {
        local_buffer[i] = chunk_ptr[i];
    }
    
    /* Simple processing: apply gain and basic filtering */
    float float_buffer[CHUNK_SIZE];
    float processed_buffer[CHUNK_SIZE];
    
    /* Convert to float */
    convert_int16_to_float(local_buffer, float_buffer, CHUNK_SIZE);
    
    /* Apply low-pass filter */
    audio_filter_lowpass(float_buffer, processed_buffer, CHUNK_SIZE);
    
    /* Apply amplification */
    audio_amplify(processed_buffer, processed_buffer, CHUNK_SIZE, 1.2f);
    
    /* Convert back to int16 */
    convert_float_to_int16(processed_buffer, local_buffer, CHUNK_SIZE);
    
    /* Copy processed audio back to shared memory */
    for (uint32_t i = 0; i < CHUNK_SIZE; i++) {
        chunk_ptr[i] = local_buffer[i];
    }
    
    uint64_t end_cycles = get_cycles();
    uint64_t processing_time = end_cycles - start_cycles;
    
    /* Update statistics */
    audio_mem->stats.chunks_processed++;
    audio_mem->stats.total_processing_time_us += processing_time;
    
    if (processing_time < audio_mem->stats.min_processing_time_us) {
        audio_mem->stats.min_processing_time_us = processing_time;
    }
    
    if (processing_time > audio_mem->stats.max_processing_time_us) {
        audio_mem->stats.max_processing_time_us = processing_time;
    }
    
    return 0;
}

/* Simple RPMsg handling */
static int handle_rpmsg_message(void *msg_data, uint32_t len) {
    if (len != sizeof(audio_msg_t)) return -1;
    
    audio_msg_t *msg = (audio_msg_t*)msg_data;
    response_msg_t response;
    
    uint64_t start_time = get_cycles();
    
    switch (msg->msg_type) {
        case MSG_INIT:
            response.msg_type = MSG_STATUS;
            response.chunk_id = 0;
            response.status = 0;  /* Success */
            response.processing_time_us = 0;
            break;
            
        case MSG_PROCESS_CHUNK:
            if (msg->chunk_id < NUM_CHUNKS) {
                int32_t result = process_audio_chunk(msg->chunk_id);
                
                response.msg_type = MSG_CHUNK_COMPLETE;
                response.chunk_id = msg->chunk_id;
                response.status = (result == 0) ? 0 : 1;
                response.processing_time_us = get_cycles() - start_time;
                
                /* Update chunk status */
                audio_mem->control.chunk_status[msg->chunk_id] = 3; /* Done */
            } else {
                response.msg_type = MSG_CHUNK_COMPLETE;
                response.chunk_id = msg->chunk_id;
                response.status = 1; /* Error */
                response.processing_time_us = 0;
                audio_mem->stats.processing_errors++;
            }
            break;
            
        default:
            response.msg_type = MSG_STATUS;
            response.chunk_id = 0;
            response.status = 1; /* Error */
            response.processing_time_us = 0;
            break;
    }
    
    /* Send response (implementation depends on RPMsg library) */
    /* For now, just return success */
    return 0;
}

/* Main function */
int main(void) {
    /* Initialize shared memory */
    init_shared_memory();
    
    /* Simple main loop */
    while (1) {
        /* In a real implementation, this would be driven by RPMsg interrupts */
        /* For now, just poll for work */
        
        /* Check for chunks ready for processing */
        for (uint32_t i = 0; i < NUM_CHUNKS; i++) {
            if (audio_mem->control.chunk_status[i] == 1) { /* Ready */
                audio_mem->control.chunk_status[i] = 2; /* Processing */
                process_audio_chunk(i);
                audio_mem->control.chunk_status[i] = 3; /* Done */
            }
        }
        
        /* Simple delay */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    return 0;
}
