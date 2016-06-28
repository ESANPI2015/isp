#ifndef __ISP_H
#define __ISP_H

/*TODO: ISP 2.0: We dont want to have a startAddr and such ... we just want regions like Firmware & Configuration*/

#include "ndlcom/Node.h"

/**
 * The states of the ISP master/slave algorithms
 */
typedef enum {
    ISP_STATE_IDLE,
    ISP_STATE_ERASING,
    ISP_STATE_UPLOADING,
    ISP_STATE_DOWNLOADING,
    ISP_STATE_VERIFIING,
    ISP_STATE_ERROR
} ispState;

/**
 * For the ISP master these functions provide access to the image files
 * For the ISP slave these functions provide access to the PROM/Flash memory
 */
typedef unsigned int (*ispReadFunc)(void *, const unsigned int);
typedef void (*ispWriteFunc)(const void *, const unsigned int);

/**
 * The ispContext contains all information needed for the ISP functionality
 */
typedef struct {
    /*NDLCom stuff*/
    struct NDLComInternalHandler handler;
    struct NDLComNode *node;
    NDLComId sourceId;
    NDLComId targetId;
    /*ISP stuff*/
    ispState state;
    unsigned int startAddr;
    unsigned int offset;
    unsigned int length;
    ispReadFunc read;
    ispWriteFunc write;
} ispContext;


/**
 * Checks whether an ISP process is still in execution
 */
int ispIsBusy(ispContext *ctx);

/**
 * Destroys a given context
 */
void ispDestroyContext(ispContext *ctx);

/* SLAVE FUNCTIONS*/

/**
 * This function creates a context for an ISP slave
 */
void ispCreateSlaveContext(ispContext *ctx, struct NDLComNode *node);

/* MASTER FUNCTIONS*/

/**
 * This function creates a context for an ISP master
 */
void ispCreateMasterContext(ispContext *ctx, struct NDLComNode *node);
/**
 * Sets the target id and region information for the upcoming isp operation
 */
void ispMasterSetTarget(ispContext *ctx, const NDLComId targetId, const unsigned int addr, const unsigned int len);
/**
 * Starts the upload of the content returned by read to the target device's PROM
 */
void ispMasterStartUpload(ispContext *ctx);
/**
 * Starts the download of the target device PROM content to the destination of write
 */
void ispMasterStartDownload(ispContext *ctx);
/**
 * Starts the download of the target device PROM content and compares it with the content returned by read
 */
void ispMasterStartVerify(ispContext *ctx);

#endif
