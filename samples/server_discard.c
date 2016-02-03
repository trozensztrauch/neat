#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <netinet/in.h>
#include "../neat.h"
#include "../neat_internal.h"

static uint32_t config_buffer_size = 8;
static uint16_t config_log_level = 1;
static char config_property[] = "NEAT_PROPERTY_TCP_REQUIRED,NEAT_PROPERTY_IPV4_REQUIRED";


#define debug_error(M, ...) fprintf(stderr, "[ERROR][%s:%d] " M "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)

static struct neat_flow_operations ops;
static struct neat_ctx *ctx = NULL;
static struct neat_flow *flow = NULL;
static unsigned char *buffer = NULL;
static uint32_t buffer_filled;

/*
    print usage and exit
*/
static void print_usage()
{
    printf("server_discard [OPTIONS]\n");
    printf("\t- P \tneat properties (%s)\n", config_property);
    printf("\t- S \tbuffer in byte (%d)\n", config_buffer_size);
    printf("\t- v \tlog level 0..2 (%d)\n", config_log_level);
}

/*
    Error handler
*/
static uint64_t on_error(struct neat_flow_operations *opCB)
{
    exit(EXIT_FAILURE);
}

/*
    Read data until buffered_amount == 0 - then stop event loop!
*/
static uint64_t on_readable(struct neat_flow_operations *opCB)
{
    // data is available to read
    neat_error_code code;

    code = neat_read(opCB->ctx, opCB->flow, buffer, config_buffer_size, &buffer_filled);
    if (code) {
        if (code == NEAT_ERROR_WOULD_BLOCK) {
            if (config_log_level >= 1) {
                printf("on_readable - NEAT_ERROR_WOULD_BLOCK\n");
            }
            return 0;
        } else {
            debug_error("code: %d", (int)code);
            return on_error(opCB);
        }
    }

    if (buffer_filled > 0) {
        if (config_log_level >= 1) {
            printf("[%d] received data - %d byte\n", opCB->flow->fd, buffer_filled);
        }
        if (config_log_level >= 2) {
            fwrite(buffer, sizeof(char), buffer_filled, stdout);
            printf("\n");
            fflush(stdout);
        }
    } else {
        if (config_log_level >= 1) {
            printf("[%d] disconnected\n", opCB->flow->fd);
        }
        opCB->on_readable = NULL;
        opCB->on_writable = NULL;
        neat_free_flow(opCB->flow);
    }
    return 0;
}

static uint64_t on_connected(struct neat_flow_operations *opCB)
{
    printf("[%d] connected - ", opCB->flow->fd);

    if (opCB->flow->family == AF_INET) {
        printf("IPv4 - ");
    } else if (opCB->flow->family == AF_INET6) {
        printf("IPv6 - ");
    }

    switch (opCB->flow->sockProtocol) {
    case IPPROTO_TCP:
        printf("TCP ");
        break;
    case IPPROTO_UDP:
        printf("UDP ");
        break;
#ifdef IPPROTO_SCTP
    case IPPROTO_SCTP:
        printf("SCTP ");
        break;
#endif
#ifdef IPPROTO_UDPLITE
    case IPPROTO_UDPLITE:
        printf("UDPLite ");
        break;
#endif
    default:
        printf("protocol #%d", opCB->flow->sockProtocol);
        break;
    }
    printf("\n");

    opCB->on_readable = on_readable;
    return 0;
}

int main(int argc, char *argv[])
{
    uint64_t prop;
    int arg, result;
    char *arg_property = config_property;
    char *arg_property_ptr;
    char arg_property_delimiter[] = ",;";

    result = EXIT_SUCCESS;

    while ((arg = getopt(argc, argv, "P:S:v:")) != -1) {
        switch(arg) {
        case 'P':
            arg_property = optarg;
            if (config_log_level >= 1) {
                printf("option - properties: %s\n", arg_property);
            }
            break;
        case 'S':
            config_buffer_size = atoi(optarg);
            if (config_log_level >= 1) {
                printf("option - buffer size: %d\n", config_buffer_size);
            }
            break;
        case 'v':
            config_log_level = atoi(optarg);
            if (config_log_level >= 1) {
                printf("option - log level: %d\n", config_log_level);
            }
            break;
        default:
            print_usage();
            goto cleanup;
            break;
        }
    }

    if (optind != argc) {
        debug_error("argument error");
        print_usage();
        goto cleanup;
    }

    if ((buffer = malloc(config_buffer_size)) == NULL) {
        debug_error("could not allocate buffer");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    if ((ctx = neat_init_ctx()) == NULL) {
        debug_error("could not initialize context");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // new neat flow
    if ((flow = neat_new_flow(ctx)) == NULL) {
        debug_error("neat_new_flow");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // set properties (TCP only etc..)
    if (neat_get_property(ctx, flow, &prop)) {
        debug_error("neat_get_property");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // read property arguments
    arg_property_ptr = strtok(arg_property, arg_property_delimiter);

    while (arg_property_ptr != NULL) {
        if (config_log_level >= 1) {
            printf("setting property: %s\n", arg_property_ptr);
        }

        if (strcmp(arg_property_ptr,"NEAT_PROPERTY_OPTIONAL_SECURITY") == 0) {
            prop |= NEAT_PROPERTY_TCP_REQUIRED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_REQUIRED_SECURITY") == 0) {
            prop |= NEAT_PROPERTY_REQUIRED_SECURITY;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_MESSAGE") == 0) {
            prop |= NEAT_PROPERTY_MESSAGE;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_IPV4_REQUIRED") == 0) {
            prop |= NEAT_PROPERTY_IPV4_REQUIRED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_IPV4_BANNED") == 0) {
            prop |= NEAT_PROPERTY_IPV4_BANNED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_IPV6_REQUIRED") == 0) {
            prop |= NEAT_PROPERTY_IPV6_REQUIRED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_IPV6_BANNED") == 0) {
            prop |= NEAT_PROPERTY_IPV6_BANNED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_SCTP_REQUIRED") == 0) {
            prop |= NEAT_PROPERTY_SCTP_REQUIRED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_SCTP_BANNED") == 0) {
            prop |= NEAT_PROPERTY_SCTP_BANNED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_TCP_REQUIRED") == 0) {
            prop |= NEAT_PROPERTY_TCP_REQUIRED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_TCP_BANNED") == 0) {
            prop |= NEAT_PROPERTY_TCP_BANNED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_UDP_REQUIRED") == 0) {
            prop |= NEAT_PROPERTY_UDP_REQUIRED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_UDP_BANNED") == 0) {
            prop |= NEAT_PROPERTY_UDP_BANNED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_UDPLITE_REQUIRED") == 0) {
            prop |= NEAT_PROPERTY_UDPLITE_REQUIRED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_UDPLITE_BANNED") == 0) {
            prop |= NEAT_PROPERTY_UDPLITE_BANNED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_CONGESTION_CONTROL_REQUIRED") == 0) {
            prop |= NEAT_PROPERTY_CONGESTION_CONTROL_REQUIRED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_CONGESTION_CONTROL_BANNED") == 0) {
            prop |= NEAT_PROPERTY_CONGESTION_CONTROL_BANNED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_RETRANSMISSIONS_REQUIRED") == 0) {
            prop |= NEAT_PROPERTY_RETRANSMISSIONS_REQUIRED;
        } else if (strcmp(arg_property_ptr,"NEAT_PROPERTY_RETRANSMISSIONS_BANNED") == 0) {
            prop |= NEAT_PROPERTY_RETRANSMISSIONS_BANNED;
        } else {
            printf("error - unknown property: %s\n", arg_property_ptr);
            print_usage();
            goto cleanup;
        }

        // get next property
        arg_property_ptr = strtok(NULL, arg_property_delimiter);
    }

    // set properties
    if (neat_set_property(ctx, flow, prop)) {
        debug_error("neat_set_property");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // set callbacks
    ops.on_connected = on_connected;
    ops.on_error = on_error;

    if (neat_set_operations(ctx, flow, &ops)) {
        debug_error("neat_set_operations");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // wait for on_connected or on_error to be invoked
    if (neat_accept(ctx, flow, "*", "8080")) {
        debug_error("neat_accept");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    neat_start_event_loop(ctx, NEAT_RUN_DEFAULT);

    // cleanup
cleanup:
    free(buffer);
    if (flow != NULL) {
        neat_free_flow(flow);
    }
    if (ctx != NULL) {
        neat_free_ctx(ctx);
    }
    exit(result);
}
