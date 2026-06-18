#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <linux/ipmi.h>

#define DCMI_NETFN              0x2c
#define DCMI_GET_POWER_READING  0x02
#define DCMI_GROUP_EXT          0xdc

typedef struct {
    int fd;
    struct ipmi_addr addr;
} ipmi_context_t;

static ipmi_context_t ipmi_ctx;

int use_ipmi = 0;
extern int has_energy;
extern int interval;

static int
do_init_ipmi()
{
    const char *device_paths[] = {"/dev/ipmi0", "/dev/ipmi/0", "/dev/ipmidev/0",
                                  NULL};

    for (int i = 0; device_paths[i] != NULL; i++) {
        ipmi_ctx.fd = open(device_paths[i], O_RDWR);
        if (ipmi_ctx.fd >= 0) {
            fprintf(stderr, "IPMI device found: %s\n", device_paths[i]);
            break;
        }
    }

    if (ipmi_ctx.fd < 0) {
        perror("Failed to open IPMI device");
        return -1;
    }

    struct ipmi_system_interface_addr *si_addr = (struct
        ipmi_system_interface_addr *)&ipmi_ctx.addr;
    si_addr->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
    si_addr->channel = IPMI_BMC_CHANNEL;
    si_addr->lun = 0;

    return 0;
}

void
init_ipmi(void)
{
    if (use_ipmi) {
        if (do_init_ipmi() < 0) {
            fprintf(stderr, "Failed to initialize IPMI. Exiting.\n");
            exit(EXIT_FAILURE);
        }
    }
}

void
fini_ipmi(void)
{
    if (use_ipmi) {
        close(ipmi_ctx.fd);
    }
}

void
print_ipmi_headers(void)
{
    if (!use_ipmi)
        return;
    printf(" IPMI(W)");
    if (has_energy)
        printf(" IPMI(J)");
}

int
send_ipmi_command(ipmi_context_t *ctx, uint8_t netfn, uint8_t cmd,
                  uint8_t *req_data, int req_len,
                  uint8_t *resp_data, int *resp_len)
{
    struct ipmi_req req;
    struct ipmi_recv recv;
    static long msgid = 0;

    memset(&req, 0, sizeof(req));
    req.addr = (unsigned char *)&ctx->addr;
    req.addr_len = sizeof(struct ipmi_system_interface_addr);
    req.msgid = msgid++;
    req.msg.netfn = netfn;
    req.msg.cmd = cmd;
    req.msg.data_len = req_len;
    req.msg.data = req_data;

    if (ioctl(ctx->fd, IPMICTL_SEND_COMMAND, &req) < 0) {
        perror("Failed to send IPMI command");
        return -1;
    }

    memset(&recv, 0, sizeof(recv));
    recv.addr = (unsigned char *)&ctx->addr;
    recv.addr_len = sizeof(ctx->addr);
    recv.msg.data = resp_data;
    recv.msg.data_len = *resp_len;

    if (ioctl(ctx->fd, IPMICTL_RECEIVE_MSG_TRUNC, &recv) < 0) {
        if (errno != EAGAIN) {
            perror("Failed to receive IPMI response");
            return -1;
        }
    }

    *resp_len = recv.msg.data_len;

    if (resp_data[0] != 0x00) {
        fprintf(stderr, "IPMI Error: completion code = 0x%02x\n", resp_data[0]);
        return -1;
    }

    return 0;
}

static int
get_ipmi_power_reading(void)
{
    uint8_t req_data[4] = {DCMI_GROUP_EXT, 0x01, 0x00, 0x00};
    uint8_t resp_data[256];
    int resp_len = sizeof(resp_data);

    if (send_ipmi_command(&ipmi_ctx, DCMI_NETFN, DCMI_GET_POWER_READING,
                          req_data, 4, resp_data, &resp_len) < 0) {
        return -1;
    }

    if (resp_len < 16) {
        fprintf(stderr, "IPMI response data length is too short: %d\n",
                resp_len);
        return -1;
    }

    uint16_t current_power = resp_data[2] | (resp_data[3] << 8);

    return current_power;
}

void
monitor_ipmi(void)
{
    double ipmi_energy = 0.0;
    int power_ipmi = -1;
    if (use_ipmi) {
        power_ipmi = get_ipmi_power_reading();
        if (power_ipmi >= 0) {
            printf(" %d", power_ipmi);
            if (has_energy) {
                ipmi_energy += power_ipmi * (interval / 1000000.0);
                printf(" %.3f", ipmi_energy);
            }
        }
    }
}