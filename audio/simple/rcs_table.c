/*
 * rsc_table.h
 * 
 * Simple resource table for remoteproc on BeagleY-AI
 * No XDC dependency - just basic structures
 */

#ifndef RSC_TABLE_H
#define RSC_TABLE_H

#include <stdint.h>

/* Resource table entry types */
#define RSC_CARVEOUT    0
#define RSC_DEVMEM      1  
#define RSC_TRACE       2
#define RSC_VDEV        3
#define RSC_LAST        4

/* VirtIO device types */
#define VIRTIO_ID_RPMSG     7

/* VirtIO device features */
#define VIRTIO_RPMSG_F_NS   0  /* Name service notifications */

/* Resource table structures */
struct resource_table_hdr {
    uint32_t ver;           /* Version */
    uint32_t num;           /* Number of entries */
    uint32_t reserved[2];   /* Reserved fields */
};

struct fw_rsc_hdr {
    uint32_t type;          /* Resource type */
    uint8_t data[0];        /* Resource data */
};

struct fw_rsc_carveout {
    uint32_t type;          /* RSC_CARVEOUT */
    uint32_t da;            /* Device address */
    uint32_t pa;            /* Physical address */  
    uint32_t len;           /* Length */
    uint32_t flags;         /* Flags */
    uint32_t reserved;      /* Reserved */
    uint8_t name[32];       /* Name */
};

struct fw_rsc_vdev_vring {
    uint32_t da;            /* Device address */
    uint32_t align;         /* Alignment */
    uint32_t num;           /* Number of buffers */
    uint32_t notifyid;      /* Notification ID */
    uint32_t reserved;      /* Reserved */
};

struct fw_rsc_vdev {
    uint32_t type;          /* RSC_VDEV */
    uint32_t id;            /* VirtIO device ID */
    uint32_t notifyid;      /* Notification ID */
    uint32_t dfeatures;     /* Device features */
    uint32_t gfeatures;     /* Guest features */
    uint32_t config_len;    /* Config length */
    uint8_t status;         /* Status */
    uint8_t num_of_vrings;  /* Number of vrings */
    uint8_t reserved[2];    /* Reserved */
};

/* Simple resource table for audio processing */
struct audio_resource_table {
    struct resource_table_hdr hdr;
    uint32_t offset[1];     /* Offset to resources */
    
    /* VirtIO device for RPMsg */
    struct fw_rsc_vdev rpmsg_vdev;
    struct fw_rsc_vdev_vring rpmsg_vring0;
    struct fw_rsc_vdev_vring rpmsg_vring1;
};

/* Memory layout from device tree */
#define C7X_0_DMA_BASE      0xa3000000
#define C7X_0_DMA_SIZE      0x100000
#define VRING0_DA           (C7X_0_DMA_BASE + 0x10000)  /* 64KB offset */
#define VRING1_DA           (C7X_0_DMA_BASE + 0x20000)  /* 128KB offset */

/* Resource table instance */
extern struct audio_resource_table resource_table;

#endif /* RSC_TABLE_H */

/*
 * rsc_table.c - Resource table implementation
 */

#include "rsc_table.h"

/* Place resource table in special section for remoteproc */
#pragma DATA_SECTION(resource_table, ".resource_table")
#pragma RETAIN(resource_table)

struct audio_resource_table resource_table = {
    /* Resource table header */
    {
        1,          /* Version */
        1,          /* Number of entries */
        {0, 0}      /* Reserved */
    },
    
    /* Offset array */
    {
        offsetof(struct audio_resource_table, rpmsg_vdev)
    },
    
    /* RPMsg VirtIO device */
    {
        RSC_VDEV,           /* type */
        VIRTIO_ID_RPMSG,    /* id */
        0,                  /* notifyid */
        VIRTIO_RPMSG_F_NS,  /* dfeatures */
        0,                  /* gfeatures */
        0,                  /* config_len */
        0,                  /* status */
        2,                  /* num_of_vrings */
        {0, 0}              /* reserved */
    },
    
    /* VRing 0 (Host to DSP) */
    {
        VRING0_DA,          /* da */
        16,                 /* align */
        256,                /* num */
        0,                  /* notifyid */
        0                   /* reserved */
    },
    
    /* VRing 1 (DSP to Host) */
    {
        VRING1_DA,          /* da */  
        16,                 /* align */
        256,                /* num */
        1,                  /* notifyid */
        0                   /* reserved */
    }
};
