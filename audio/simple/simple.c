/*
 * c7x_audio_processor.c
 * 
 * C7x DSP firmware for processing audio chunks received via RPMsg
 * Designed for BeagleY-AI AM67A C7x DSP cores
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* TI-RTOS includes */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Diags.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>

/* IPC includes */
#include <ti/ipc/Ipc.h>
#include <ti/ipc/MessageQ.h>
#include <ti/ipc/remoteproc/Resource.h>
#include <ti/ipc/rpmsg/MessageQCopy.h>

/* C7x DSP specific includes */
#include <c7x.h>
#include <c7x_strm.h>
#include <ti/csl/csl_types.h>

/* Audio processing includes */
#include <mathf.h>
#include <complex.h>

/* Audio configuration - must match Linux application */
#define SAMPLE_RATE         48000
#define CHUNK_SIZE          6000   /* AUDIO_BUFFER_SIZE / 8 */
#define NUM_CHUNKS          8

/* Message types - must match Linux application */
typedef enum {
    MSG_PROCESS_CHUNK = 1,
    MSG_CHUNK_COMPLETE = 2,
    MSG_INIT = 3,
    MSG_STATUS = 4
} msg_type_t;

/* Message structures - must match Linux application */
typedef struct {
    uint32_t msg_type;
    uint32_t chunk_id;
    uint32_t buffer_offset;
    uint32_t chunk_size;
    uint64_t timestamp;
} audio_msg_t;

typedef struct {
    uint32_t msg_type;
    uint32_t chunk_id;
    uint32_t status;  /* 0 = success, 1 = error */
    uint64_t processing_time_us;
} response_msg_t;

/* RPMsg configuration */
#define RPMSG_ENDPOINT_NAME     "audio-processing"
#define HOST_ID                 0
#define REMOTE_ID               1
#define HEAP_ID                 0
#define MSGQ_HEAP_SIZE          8192

/* Global variables */
static MessageQCopy_Handle msgqHandle = NULL;
static UInt32 local_endpt = 0;
static UInt32 remote_endpt = MessageQCopy_ASSIGN_ANY;
static Bool initialized = FALSE;

/* Audio processing buffers */
static int16_t audio_chunk[CHUNK_SIZE] __attribute__((aligned(64)));
static float32_t float_buffer[CHUNK_SIZE] __attribute__((aligned(64)));
static float32_t processed_buffer[CHUNK_SIZE] __attribute__((aligned(64)));

/* Function prototypes */
static Void rpmsg_task(UArg arg0, UArg arg1);
static Int32 process_audio_chunk(int16_t *input, uint32_t size, uint32_t chunk_id);
static Void audio_filter_lowpass(float32_t *input, float32_t *output, uint32_t size);
static Void audio_amplify(float32_t *input, float32_t *output, uint32_t size, float32_t gain);
static Void convert_int16_to_float(int16_t *input, float32_t *output, uint32_t size);
static Void convert_float_to_int16(float32_t *input, int16_t *output, uint32_t size);
static UInt64 get_timestamp_us(void);

/* Task configuration */
Task_Params taskParams;
Task_Handle taskHandle;

/* Get timestamp in microseconds using C7x cycle counter */
static UInt64 get_timestamp_us(void) {
    /* C7x runs at ~1GHz, so divide by 1000 to get microseconds */
    return __cycles() / 1000;
}

/* Convert 16-bit signed integer samples to floating point */
static Void convert_int16_to_float(int16_t *input, float32_t *output, uint32_t size) {
    uint32_t i;
    const float32_t scale = 1.0f / 32768.0f;
    
    /* Use C7x SIMD instructions for vectorized conversion */
    #pragma MUST_ITERATE(16, , 16)
    #pragma UNROLL(4)
    for (i = 0; i < size; i += 4) {
        /* Process 4 samples at once using C7x vector instructions */
        int16x4_t vec_int = *(int16x4_t*)&input[i];
        float32x4_t vec_float = __builtin_c7x_vcvt_f32_s16(vec_int) * scale;
        *(float32x4_t*)&output[i] = vec_float;
    }
}

/* Convert floating point samples back to 16-bit signed integer */
static Void convert_float_to_int16(float32_t *input, int16_t *output, uint32_t size) {
    uint32_t i;
    const float32_t scale = 32767.0f;
    
    #pragma MUST_ITERATE(16, , 16)
    #pragma UNROLL(4)
    for (i = 0; i < size; i += 4) {
        /* Process 4 samples at once with saturation */
        float32x4_t vec_float = *(float32x4_t*)&input[i] * scale;
        int16x4_t vec_int = __builtin_c7x_vcvt_s16_f32_sat(vec_float);
        *(int16x4_t*)&output[i] = vec_int;
    }
}

/* Simple low-pass filter using C7x DSP instructions */
static Void audio_filter_lowpass(float32_t *input, float32_t *output, uint32_t size) {
    static float32_t prev_sample = 0.0f;
    const float32_t alpha = 0.1f;  /* Filter coefficient */
    uint32_t i;
    
    /* Simple IIR low-pass filter: y[n] = alpha * x[n] + (1-alpha) * y[n-1] */
    for (i = 0; i < size; i++) {
        output[i] = alpha * input[i] + (1.0f - alpha) * prev_sample;
        prev_sample = output[i];
    }
}

/* Audio amplification with C7x vector processing */
static Void audio_amplify(float32_t *input, float32_t *output, uint32_t size, float32_t gain) {
    uint32_t i;
    
    #pragma MUST_ITERATE(16, , 16)
    #pragma UNROLL(8)
    for (i = 0; i < size; i += 8) {
        /* Process 8 samples at once using C7x SIMD */
        float32x8_t vec_in = *(float32x8_t*)&input[i];
        float32x8_t vec_out = vec_in * gain;
        *(float32x8_t*)&output[i] = vec_out;
    }
}

/* Main audio processing function */
static Int32 process_audio_chunk(int16_t *input, uint32_t size, uint32_t chunk_id) {
    UInt64 start_time, end_time;
    
    if (size > CHUNK_SIZE) {
        System_printf("Error: chunk size %d exceeds maximum %d\n", size, CHUNK_SIZE);
        return -1;
    }
    
    start_time = get_timestamp_us();
    
    /* Step 1: Convert input to floating point */
    convert_int16_to_float(input, float_buffer, size);
    
    /* Step 2: Apply low-pass filter */
    audio_filter_lowpass(float_buffer, processed_buffer, size);
    
    /* Step 3: Apply amplification (gain = 1.2) */
    audio_amplify(processed_buffer, processed_buffer, size, 1.2f);
    
    /* Step 4: Convert back to 16-bit integer */
    convert_float_to_int16(processed_buffer, input, size);
    
    end_time = get_timestamp_us();
    
    System_printf("DSP: Processed chunk %d (%d samples) in %llu us\n", 
                  chunk_id, size, (end_time - start_time));
    
    return 0;
}

/* RPMsg communication task */
static Void rpmsg_task(UArg arg0, UArg arg1) {
    Int32 status;
    audio_msg_t recv_msg;
    response_msg_t resp_msg;
    UInt32 len;
    UInt64 processing_start, processing_end;
    
    System_printf("C7x Audio Processor: RPMsg task started\n");
    
    /* Create MessageQCopy for RPMsg communication */
    msgqHandle = MessageQCopy_create(local_endpt, &remote_endpt);
    if (msgqHandle == NULL) {
        System_printf("Error: Failed to create MessageQCopy\n");
        return;
    }
    
    /* Announce service to Linux */
    status = MessageQCopy_announce(REMOTE_ID, local_endpt, RPMSG_ENDPOINT_NAME);
    if (status < 0) {
        System_printf("Error: Failed to announce service\n");
        return;
    }
    
    System_printf("RPMsg endpoint announced: %s\n", RPMSG_ENDPOINT_NAME);
    initialized = TRUE;
    
    /* Main message processing loop */
    while (1) {
        /* Receive message from Linux */
        status = MessageQCopy_recv(msgqHandle, &recv_msg, &len, &remote_endpt, 
                                   MessageQCopy_FOREVER);
        
        if (status < 0) {
            System_printf("Error: MessageQCopy_recv failed with status %d\n", status);
            continue;
        }
        
        if (len != sizeof(audio_msg_t)) {
            System_printf("Error: Received message with invalid size %d\n", len);
            continue;
        }
        
        processing_start = get_timestamp_us();
        
        /* Process the message based on type */
        switch (recv_msg.msg_type) {
            case MSG_INIT:
                System_printf("Received initialization message\n");
                resp_msg.msg_type = MSG_STATUS;
                resp_msg.chunk_id = 0;
                resp_msg.status = 0;  /* Success */
                resp_msg.processing_time_us = 0;
                break;
                
            case MSG_PROCESS_CHUNK:
                System_printf("Received process chunk message: chunk_id=%d, size=%d\n",
                              recv_msg.chunk_id, recv_msg.chunk_size);
                
                /* Note: In a real implementation, you would access the shared memory
                 * buffer using the buffer_offset. For this example, we'll simulate
                 * processing by generating test audio data */
                
                /* Generate test audio chunk (simple sine wave) */
                for (uint32_t i = 0; i < recv_msg.chunk_size && i < CHUNK_SIZE; i++) {
                    float sample = sinf(2.0f * 3.14159f * 440.0f * i / SAMPLE_RATE);
                    audio_chunk[i] = (int16_t)(sample * 16000);
                }
                
                /* Process the audio chunk */
                status = process_audio_chunk(audio_chunk, recv_msg.chunk_size, recv_msg.chunk_id);
                
                processing_end = get_timestamp_us();
                
                /* Prepare response */
                resp_msg.msg_type = MSG_CHUNK_COMPLETE;
                resp_msg.chunk_id = recv_msg.chunk_id;
                resp_msg.status = (status == 0) ? 0 : 1;
                resp_msg.processing_time_us = processing_end - processing_start;
                break;
                
            default:
                System_printf("Unknown message type: %d\n", recv_msg.msg_type);
                resp_msg.msg_type = MSG_STATUS;
                resp_msg.chunk_id = recv_msg.chunk_id;
                resp_msg.status = 1;  /* Error */
                resp_msg.processing_time_us = 0;
                break;
        }
        
        /* Send response back to Linux */
        status = MessageQCopy_send(HOST_ID, remote_endpt, local_endpt, 
                                   &resp_msg, sizeof(response_msg_t));
        
        if (status < 0) {
            System_printf("Error: Failed to send response, status=%d\n", status);
        }
    }
}

/* Main function */
Int main()
{
    Int32 status;
    
    System_printf("=== C7x Audio Processor Firmware ===\n");
    System_printf("Chunk Size: %d samples\n", CHUNK_SIZE);
    System_printf("Sample Rate: %d Hz\n", SAMPLE_RATE);
    
    /* Initialize IPC */
    status = Ipc_start();
    if (status < 0) {
        System_printf("Error: Ipc_start failed with status %d\n", status);
        return -1;
    }
    
    System_printf("IPC initialized successfully\n");
    
    /* Create RPMsg task */
    Task_Params_init(&taskParams);
    taskParams.instance->name = "rpmsg_task";
    taskParams.priority = 5;
    taskParams.stackSize = 8192;
    
    taskHandle = Task_create(rpmsg_task, &taskParams, NULL);
    if (taskHandle == NULL) {
        System_printf("Error: Failed to create RPMsg task\n");
        return -1;
    }
    
    System_printf("Starting BIOS...\n");
    
    /* Start BIOS scheduler */
    BIOS_start();
    
    /* Should never reach here */
    return 0;
}