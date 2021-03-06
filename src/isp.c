#include "isp/isp.h"
#include "representations/id.h"
#include "representations/Isp.h"

/*Internally used functions*/
void ispSlaveHandler(void *context, const struct NDLComHeader *header, const void *payload, const void *origin);
void ispSlaveCmdHandler(ispContext *ctx, const struct NDLComHeader *header, const struct IspCommand *cmd);
void ispSlaveDataHandler(ispContext *ctx, const struct NDLComHeader *header, const struct IspData *data);
void ispSendAck(ispContext *ctx, const uint32_t addr);

void ispMasterHandler(void *context, const struct NDLComHeader *header, const void *payload, const void *origin);
void ispMasterCmdHandler(ispContext *ctx, const struct NDLComHeader *header, const struct IspCommand *cmd);
void ispMasterDataHandler(ispContext *ctx, const struct NDLComHeader *header, const struct IspData *data);
void ispSendCmd(ispContext *ctx, const uint8_t cmd, const uint32_t addr, const uint32_t len);
int  ispSendData(ispContext *ctx);

/*Library functions*/

/*SLAVE STUFF*/

void ispSlaveCreate(ispContext *ctx, struct NDLComNode *node, ispReadFunc readFunc, ispWriteFunc writeFunc, ispExecFunc execFunc)
{
    ctx->state = ISP_STATE_IDLE;
    ctx->node = node;
    ctx->offset = 0;
    ctx->length = 0;

    /*We are the source and everyone could be the target (Master) at first*/
    ctx->sourceId = ctx->node->headerConfig.mOwnSenderId;
    ctx->targetId = NDLCOM_ADDR_BROADCAST;

    /*Read and write functions*/
    ctx->read = readFunc;
    ctx->write = writeFunc;
    ctx->exec = execFunc;

    /*NOTE: This means to implement a handler function (see lib/stm32common/src/isp.c)*/
    /*Register isp slave handler*/
    ndlcomNodeHandlerInit(&ctx->handler, ispSlaveHandler, 0, ctx);
    ndlcomNodeRegisterNodeHandler(ctx->node, &ctx->handler);
}

void ispSendAck(ispContext *ctx, const uint32_t addr)
{
    struct IspCommand command;
    command.mBase.mId = REPRESENTATIONS_REPRESENTATION_ID_IspCommand;
    command.mCommand = ISP_CMD_ACK;
    command.mAddress = addr;
    command.mLength = ctx->length - ctx->offset;

    ndlcomNodeSend(ctx->node, ctx->targetId, &command, sizeof(command));
}

void ispSlaveCmdHandler(ispContext *ctx, const struct NDLComHeader *header, const struct IspCommand *cmd)
{
    /*When we get an ACK and are in state UPLOADING, we read content from file and transmit it as DATA packet*/
    switch (cmd->mCommand)
    {
        case ISP_CMD_ACK:
            /*What does this mean? oO*/
            break;
        case ISP_CMD_UPLOAD:
            /*The master wants to upload stuff to our PROM/Flash*/
            if (ctx->state != ISP_STATE_IDLE)
                break;
            /*Ok, we can do it*/
            /*TODO: Check address and length, How?*/
            ctx->startAddr = cmd->mAddress;
            ctx->offset = 0;
            ctx->length = cmd->mLength;
            /*Acknowledge and proceed*/
            ispSendAck(ctx, cmd->mAddress);
            ctx->state = ISP_STATE_UPLOADING;
            break;
        case ISP_CMD_DOWNLOAD:
            /*The master wants to download stuff from our PROM/Flash*/
            if (ctx->state != ISP_STATE_IDLE)
                break;
            /*Read, send data and proceed*/
            ctx->startAddr = cmd->mAddress;
            ctx->offset = 0;
            ctx->length = cmd->mLength;
            ispSendData(ctx);
            break;
        case ISP_CMD_EXECUTE:
            /*We shall jump to the (newly) written code*/
            if (ctx->state != ISP_STATE_IDLE)
                break;
            /*Acknowledge and execute*/
            ispSendAck(ctx, cmd->mAddress);
            ctx->exec(ctx);
            break;
        case ISP_CMD_ABORT:
            /*Acknowledge and return to idle state*/
            ispSendAck(ctx, cmd->mAddress);
            ctx->state = ISP_STATE_IDLE;
            break;
        default:
            /*Unknown stuff? oO*/
            break;
    }
}

void ispSlaveDataHandler(ispContext *ctx, const struct NDLComHeader *header, const struct IspData *data)
{
    int n = (ctx->length - ctx->offset > ISP_DATA_TRANSMISSION_BLOCK_SIZE)?ISP_DATA_TRANSMISSION_BLOCK_SIZE:ctx->length - ctx->offset;

    switch (ctx->state)
    {
        case ISP_STATE_UPLOADING:
            /*Check if addresses match*/
            if (ctx->startAddr+ctx->offset > data->mAddress)
            {
                /*We already got this packet, so skip it but ack*/
                ispSendAck(ctx, data->mAddress);
                break;
            }
            else if (ctx->startAddr+ctx->offset < data->mAddress)
            {
                /*Something went wrong :(*/
                ctx->state = ISP_STATE_ERROR;
                break;
            }
            /*Write data to buffer*/
            ctx->write(ctx, data->mData, n);
            /*Update offset*/
            ctx->offset += n;
            /*Check if we still have to write data*/
            if (ctx->offset >= ctx->length)
            {
                /*Ready :)*/
                ctx->state = ISP_STATE_IDLE;
            } else {
                /*Still work todo :] */
                ctx->state = ISP_STATE_UPLOADING;
            }
            /*When we have successfully written the data we have to send an acknowledgement*/
            ispSendAck(ctx, data->mAddress);
            break;
        default:
            break;
    }
}

void ispSlaveHandler(void *context, const struct NDLComHeader *header, const void *payload, const void *origin)
{
    /*Handle incoming isp stuff*/
    const struct Representation *repr = (const struct Representation *)payload;
    ispContext *ctx = (ispContext *)context;

    /*When the master is not allowed to control us, we quit:
     * If we are in the middle of something noone else can control us*/
    if (ispIsBusy(ctx) && (header->mSenderId != ctx->targetId))
        return;

    /*Register the master's id for responses*/
    ctx->targetId = header->mSenderId;

    switch (repr->mId)
    {
        case REPRESENTATIONS_REPRESENTATION_ID_IspCommand:
            /*Call command handler*/
            ispSlaveCmdHandler(ctx, header, (const struct IspCommand *)repr);
            break;
        case REPRESENTATIONS_REPRESENTATION_ID_IspData:
            /*Call data handler*/
            ispSlaveDataHandler(ctx, header, (const struct IspData *)repr);
            break;
        default:
            break;
    }
}

/*MASTER STUFF*/

void ispMasterCreate(ispContext *ctx, struct NDLComNode *node, ispReadFunc readFunc, ispWriteFunc writeFunc)
{
    /*Give some initial values*/
    ctx->state = ISP_STATE_IDLE;
    ctx->node = node;
    ctx->offset = 0;
    ctx->length = 0;

    /*We are the source, and we are targetting ALL devices Muahahaha!*/
    ctx->sourceId = ctx->node->headerConfig.mOwnSenderId;
    ctx->targetId = NDLCOM_ADDR_BROADCAST;

    /*Read and write functions*/
    ctx->read = readFunc;
    ctx->write = writeFunc;
    ctx->exec = NULL;
    
    /*Register isp master handler*/
    ndlcomNodeHandlerInit(&ctx->handler, ispMasterHandler, 0, ctx);
    ndlcomNodeRegisterNodeHandler(ctx->node, &ctx->handler);
}

void ispDestroy(ispContext *ctx)
{
    /*TODO: Deregister isp handler*/
    ctx->node = NULL;
}

int ispIsBusy(ispContext *ctx)
{
    /*If the state is not the idle nor the error state, we still have work to do...*/
    if (ctx->state == ISP_STATE_IDLE)
        return 0;
    if (ctx->state == ISP_STATE_ERROR)
        return 0;
    return 1;
}

void ispMasterSetTarget(ispContext *ctx, const NDLComId targetId, const unsigned int addr, const unsigned int len)
{
    if (ispIsBusy(ctx))
        return;

    ctx->targetId = targetId;
    ctx->startAddr = addr;
    ctx->length = len;
    ctx->offset = 0;
}

void ispMasterStartUpload(ispContext *ctx)
{
    if (ispIsBusy(ctx))
        return;

    /*Send upload command*/
    ispSendCmd(ctx, ISP_CMD_UPLOAD, ctx->startAddr, ctx->length);
    ctx->state = ISP_STATE_ERASING;
}

void ispMasterStartDownload(ispContext *ctx)
{
    if (ispIsBusy(ctx))
        return;

    /*Send first download command*/
    ispSendCmd(ctx, ISP_CMD_DOWNLOAD, ctx->startAddr, ISP_DATA_TRANSMISSION_BLOCK_SIZE);
    ctx->state = ISP_STATE_DOWNLOADING;
}

void ispMasterStartVerify(ispContext *ctx)
{
    if (ispIsBusy(ctx))
        return;

    /*Send first download command*/
    ispSendCmd(ctx, ISP_CMD_DOWNLOAD, ctx->startAddr, ISP_DATA_TRANSMISSION_BLOCK_SIZE);
    ctx->state = ISP_STATE_VERIFIING;
}

/*FIXME This EXECUTE command is not well-formed ... it should be clear which image to load (from address?)*/
void ispMasterExecuteSlaveBootloader (ispContext *ctx)
{
    /*Trigger execute command*/
    ispSendCmd(ctx, ISP_CMD_EXECUTE, ctx->startAddr, ctx->length);
}

void ispMasterExecuteSlaveFirmware (ispContext *ctx)
{
    /*Trigger execute command*/
    ispSendCmd(ctx, ISP_CMD_EXECUTE, ctx->startAddr, ctx->length);
}

/*Internally used function implementations*/
void ispSendCmd(ispContext *ctx, const uint8_t cmd, const uint32_t addr, const uint32_t len)
{
    struct IspCommand command;
    command.mBase.mId = REPRESENTATIONS_REPRESENTATION_ID_IspCommand;
    command.mCommand = cmd;
    command.mAddress = addr;
    command.mLength = len;

    ndlcomNodeSend(ctx->node, ctx->targetId, &command, sizeof(command));
}

int ispSendData(ispContext *ctx)
{
    struct IspData data;
    int n = (ctx->length - ctx->offset > ISP_DATA_TRANSMISSION_BLOCK_SIZE)?ISP_DATA_TRANSMISSION_BLOCK_SIZE:ctx->length - ctx->offset;

    data.mBase.mId = REPRESENTATIONS_REPRESENTATION_ID_IspData;
    data.mAddress = ctx->startAddr + ctx->offset;

    /*Call read function*/
    n = ctx->read(ctx, data.mData, n);

    ndlcomNodeSend(ctx->node, ctx->targetId, &data, sizeof(data));

    return n;
}

void ispMasterCmdHandler(ispContext *ctx, const struct NDLComHeader *header, const struct IspCommand *cmd)
{
    /*When we get an ACK and are in state UPLOADING, we read content from file and transmit it as DATA packet*/
    switch (cmd->mCommand)
    {
        case ISP_CMD_ACK:
            /*Got an ACK, so we can proceed*/
            switch (ctx->state)
            {
                case ISP_STATE_UPLOADING:
                    /*Update offset*/
                    ctx->offset += ISP_DATA_TRANSMISSION_BLOCK_SIZE;
                case ISP_STATE_ERASING:
                    /*Check if we had transmitted data*/
                    if (ispSendData(ctx) > 0)
                    {
                        ctx->state = ISP_STATE_UPLOADING;
                    } else if (ctx->offset < ctx->length)
                    {
                        /*TODO: If we could not send data but there is still data to be sent, we have to send an abort command!!!*/
                        ctx->state = ISP_STATE_ERROR;
                    } else {
                        /*Ready :)*/
                        ctx->state = ISP_STATE_IDLE;
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

void ispMasterDataHandler(ispContext *ctx, const struct NDLComHeader *header, const struct IspData *data)
{
    /*When we get a data packet AND are in state DOWNLOADING, we write content to file and transmit a new DOWNLOAD command*/
    int n = (ctx->length - ctx->offset > ISP_DATA_TRANSMISSION_BLOCK_SIZE)?ISP_DATA_TRANSMISSION_BLOCK_SIZE:ctx->length - ctx->offset;
    int i;
    uint8_t buffer[ISP_DATA_TRANSMISSION_BLOCK_SIZE];

    switch (ctx->state)
    {
        case ISP_STATE_VERIFIING:
            /*Check if addresses match*/
            if (ctx->startAddr+ctx->offset != data->mAddress)
            {
                ctx->state = ISP_STATE_ERROR;
                break;
            }
            /*Read content from provided function*/
            n = ctx->read(ctx, buffer, n);
            /*Compare buffer with received data*/
            for (i = 0; i < n; ++i)
            {
                if (data->mData[i] != buffer[i])
                {
                    ctx->state = ISP_STATE_ERROR;
                    break;
                }
                /*Update offset (useful for backtracking)*/
                ctx->offset++;
            }

            /*Proceed if comparison was successful*/
            if (ctx->state != ISP_STATE_ERROR)
            {
                /*Check if we still have to read data*/
                if ((ctx->offset >= ctx->length) || (n < 1))
                {
                    /*Ready :)*/
                    ctx->state = ISP_STATE_IDLE;
                } else {
                    /*Download next packet*/
                    ispSendCmd(ctx, ISP_CMD_DOWNLOAD, ctx->startAddr + ctx->offset, ISP_DATA_TRANSMISSION_BLOCK_SIZE);
                    ctx->state = ISP_STATE_DOWNLOADING;
                }
            }
            break;
        case ISP_STATE_DOWNLOADING:
            /*Check if addresses match*/
            if (ctx->startAddr+ctx->offset != data->mAddress)
            {
                ctx->state = ISP_STATE_ERROR;
                break;
            }
            /*Write data to buffer*/
            ctx->write(ctx, data->mData, n);
            /*Update offset*/
            ctx->offset += n;
            /*Check if we still have to read data*/
            if (ctx->offset >= ctx->length)
            {
                /*Ready :)*/
                ctx->state = ISP_STATE_IDLE;
            } else {
                /*Download next packet*/
                ispSendCmd(ctx, ISP_CMD_DOWNLOAD, ctx->startAddr + ctx->offset, ISP_DATA_TRANSMISSION_BLOCK_SIZE);
                ctx->state = ISP_STATE_DOWNLOADING;
            }
            break;
        default:
            break;
    }
}

void ispMasterHandler(void *context, const struct NDLComHeader *header, const void *payload, const void *origin)
{
    /*Handle incoming isp stuff*/
    const struct Representation *repr = (const struct Representation *)payload;
    ispContext *ctx = (ispContext *)context;

    if (header->mSenderId != ctx->targetId)
        return;

    switch (repr->mId)
    {
        case REPRESENTATIONS_REPRESENTATION_ID_IspCommand:
            /*Call command handler*/
            ispMasterCmdHandler(ctx, header, (const struct IspCommand *)repr);
            break;
        case REPRESENTATIONS_REPRESENTATION_ID_IspData:
            /*Call data handler*/
            ispMasterDataHandler(ctx, header, (const struct IspData *)repr);
            break;
        default:
            break;
    }
}
