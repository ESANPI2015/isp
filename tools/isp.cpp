#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#include "ndlcom/Bridge.h"
#include "ndlcom/Node.h"
//#include "ndlcom/ExternalInterfaceParseUri.hpp"
#include "isp/isp.h"

static struct option long_options[] = {
    {"help",     no_argument,       0, 'h'},
    {"execute",  required_argument, 0, 'x'},
    {"upload",   no_argument,       0, 'u'},
    {"verify",   no_argument,       0, 'v'},
    {"download", no_argument,       0, 'd'},
    {"node_id",  required_argument, 0, 'n'},
    {"address",  required_argument, 0, 'a'},
    {"size",     required_argument, 0, 's'},
    {"uri",      required_argument, 0, 'i'},
    {"my_id",    required_argument, 0, 'm'},
    {0, 0, 0, 0}
};

static char filename[256];
static char uri[256];

enum ispAction {
    ISP_ACTION_NONE,
    ISP_ACTION_BOOTLOADER,
    ISP_ACTION_FIRMWARE,
    ISP_ACTION_UPLOAD,
    ISP_ACTION_DOWNLOAD,
    ISP_ACTION_VERIFY
};

/*C-type subclassing: ispMasterContext inherits from ispContext*/
typedef struct {
    ispContext ctx;
    FILE *fp;
} ispMasterContext;

void ispMasterWrite (void *context, const void *buffer, const unsigned int length)
{
    /*NOTE: This is ok, because ispContext is the first member of ispMasterContext*/
    ispMasterContext *mctx = (ispMasterContext *)context;
    fwrite(buffer, 1, length, mctx->fp);
}

unsigned int ispMasterRead (void *context, void *buffer, const unsigned int length)
{
    ispMasterContext *mctx = (ispMasterContext *)context;
    int n = fread(buffer, 1, length, mctx->fp);
    return (n > 0 ? n : 0);
}

long fileSize(FILE *fp)
{
    long value;
    fseek(fp, 0, SEEK_END);
    value = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    return value;
}

enum ispAction parse_args(ispMasterContext *context, int argc, char **argv);
void print_help(const char* name);

int main(int argc, char **argv)
{
    struct NDLComBridge bridge;
    struct NDLComNode node;
    ispMasterContext context, tmp;
    enum ispAction action = ISP_ACTION_NONE;
    unsigned int percentage = 0;
    unsigned int lastPercentage = 0;;
    unsigned int size;

    if ((action = parse_args(&tmp, argc, argv)) == ISP_ACTION_NONE) return -1;

    // Setup ndlcom stuff
    ndlcomBridgeInit(&bridge);
    ndlcomNodeInit(&node, tmp.ctx.sourceId);
    ndlcomNodeRegister(&node, &bridge);
    //ndlcom::ParseUriAndCreateExternalInterface(std::cerr, bridge, uri);
    // Thanks to MZ we have now to create an External Interface of our own

    // Prepare ISP master and its context
    ispMasterCreate(&context.ctx, &node, ispMasterRead, ispMasterWrite);
    // Insert stuff from parse_args
    ispMasterSetTarget(&context.ctx, tmp.ctx.targetId, tmp.ctx.startAddr, tmp.ctx.length);

    // II. Prepare actions
    switch (action)
    {
        case ISP_ACTION_BOOTLOADER:
            printf("Switching to bootloader at device %u\n", context.ctx.targetId);
            ispMasterExecuteSlaveBootloader(&context.ctx);
            ndlcomBridgeProcessOnce(&bridge);
            return 0;
        case ISP_ACTION_FIRMWARE:
            printf("Switching to firmware at device %u\n", context.ctx.targetId);
            ispMasterExecuteSlaveFirmware(&context.ctx);
            ndlcomBridgeProcessOnce(&bridge);
            return 0;
        case ISP_ACTION_UPLOAD:
            printf("Uploading '%s' to device %u: ", filename, context.ctx.targetId);
            // Open file for reading
            context.fp = fopen(filename, "r");
            if (!context.fp)
            {
                fprintf(stderr, "Could not open file '%s'\n", filename);
                return -1;
            }
            // Check size
            size = fileSize(context.fp);
            if (context.ctx.length > size)
                context.ctx.length = size;
            // Start uploading
            ispMasterStartUpload(&context.ctx);
            break;
        case ISP_ACTION_DOWNLOAD:
            printf("Downloading to '%s' from device %u: ", filename, context.ctx.targetId);
            // Open file for writing
            context.fp = fopen(filename, "w");
            if (!context.fp)
            {
                fprintf(stderr, "Could not open file '%s'\n", filename);
                return -1;
            }
            // Send first download command
            ispMasterStartDownload(&context.ctx);
            break;
        case ISP_ACTION_VERIFY:
        default:
            printf("Verifiing '%s' and content at device %u: ", filename, context.ctx.targetId);
            // Open file for reading
            context.fp = fopen(filename, "r");
            if (!context.fp)
            {
                fprintf(stderr, "Could not open file '%s'\n", filename);
                return -1;
            }
            // Check size
            size = fileSize(context.fp);
            if (context.ctx.length > size)
                context.ctx.length = size;
            // Send first download command
            ispMasterStartVerify(&context.ctx);
            break;
    }

    // III. Main loop for handling ndlcom packets
    while (ispIsBusy(&context.ctx))
    {
        // Every percent we print a '.'
        percentage = context.ctx.offset * 100 / context.ctx.length;
        if (percentage != lastPercentage)
        {
            printf(".");
            lastPercentage = percentage;
        }
        ndlcomBridgeProcessOnce(&bridge);
    }

    // IV. Check if we have been successful
    switch (context.ctx.state)
    {
        case ISP_STATE_IDLE:
            printf(" DONE\n");
            return 0;
        case ISP_STATE_ERROR:
            switch (action)
            {
                case ISP_ACTION_VERIFY:
                    fprintf(stderr, " Verification failed at offset 0x%x\n", context.ctx.offset);
                    break;
                default:
                    fprintf(stderr, " In state error but dont know why ...\n");
                    break;
            }
            break;
        default:
            break;
    }

    return -1;
}


enum ispAction parse_args(ispMasterContext *context, int argc, char **argv)
{
    enum ispAction action = ISP_ACTION_NONE;
    int c;
     
    while (1) {
        /* getopt_long stores the option index here. */
        int option_index = 0;
     
        c = getopt_long (argc, argv, "abc:d:f:",
                         long_options, &option_index);
     
        /* Detect the end of the options. */
        if (c == -1)
            break;
     
        switch (c) {
     
        case 'h': // help
            print_help(argv[0]);
            exit(-1);
     
        case 'u': // upload
            action = ISP_ACTION_UPLOAD;
            break;

        case 'v':
            action = ISP_ACTION_VERIFY;
            break;

        case 'd':
            action = ISP_ACTION_DOWNLOAD;
            break;

        case 'x':
            action = ISP_ACTION_FIRMWARE;
            if (strncmp(optarg, "bl", 2) == 0)
                action = ISP_ACTION_BOOTLOADER;
            break;

        case 'i':
            snprintf(uri, 256, "%s", optarg);
            break;

        case 'm':
            context->ctx.sourceId = atoi(optarg);
            break;

        case 'n':
            context->ctx.targetId = atoi(optarg);
            break;

        case 'a':
            sscanf(optarg,"0x%x",&(context->ctx.startAddr));
            break;

        case 's':
            context->ctx.length = atoi(optarg);
            break;
     
        default:
            break;
        }
    }
     
    // copy filename argument
    if (optind < argc)
    {
        strncpy(filename,argv[optind],256);
    }
    else if ((action != ISP_ACTION_BOOTLOADER) && (action != ISP_ACTION_FIRMWARE))
    {
        print_help(argv[0]);
        exit(-1);
    }

    // Check size
    if ((context->ctx.length < 1) && (action == ISP_ACTION_DOWNLOAD))
    {
        fprintf(stderr, "Size has to be greater than zero\n");
        exit(-1);
    }

    return action;
}

void print_help(const char* name)
{
    printf("Usage: %s [options]\n", name);
    printf("Options:\n");
    printf("  --help            Display this information\n");
    printf("  --execute={bl|fw} Executes the BootLoader (bl) or the FirmWare (fw) (only some devices)\n");
    printf("  --node_id=<id>    Node id of the device to program\n");
    printf("  --address=<addr>  address (hex) to write bin-file to (default 0x0)\n");
    printf("  --size=<size>     Size of the data to download (default 0)\n");
    printf("  --uri=<uri>       An URI to the interface for data transmission and reception\n");
    printf("  --my_id=<id>      An id to be used for ISP (default 0x01)\n");
    printf("\nThe following commands need a binary file argument\n");
    printf("  --upload          Upload a bin-file\n");
    printf("  --verify          Verify a bin-file (default)\n");
    printf("  --download        Download data and store it to a file (--size=<size> required)\n");
}

