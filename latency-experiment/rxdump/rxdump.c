#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>

#include "psio.h"

void dump_packet(char *buf, int len)
{
	struct ethhdr *ethh;
	struct iphdr *iph;
	struct ip6_hdr *ip6h;
	struct udphdr *udph;
	struct tcphdr *tcph;
	uint8_t proto_in_ip = 0;
	char outbuf[64];

	ethh = (struct ethhdr *)buf;
	printf("%02X:%02X:%02X:%02X:%02X:%02X -> %02X:%02X:%02X:%02X:%02X:%02X ",
			ethh->h_source[0],
			ethh->h_source[1],
			ethh->h_source[2],
			ethh->h_source[3],
			ethh->h_source[4],
			ethh->h_source[5],
			ethh->h_dest[0],
			ethh->h_dest[1],
			ethh->h_dest[2],
			ethh->h_dest[3],
			ethh->h_dest[4],
			ethh->h_dest[5]);

	/* IP layer */
	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:
		iph = (struct iphdr *)(ethh + 1);
		proto_in_ip = iph->protocol;
		udph = (struct udphdr *)((uint32_t *)iph + iph->ihl);
		tcph = (struct tcphdr *)((uint32_t *)iph + iph->ihl);
		printf(" ");
		inet_ntop(AF_INET, (void *)&iph->saddr, outbuf, sizeof(outbuf));
		printf("%s", outbuf);
		if (proto_in_ip == IPPROTO_TCP || proto_in_ip == IPPROTO_UDP)
			printf("(%d)", ntohs(udph->source));
		printf(" -> ");
		inet_ntop(AF_INET, (void *)&iph->daddr, outbuf, sizeof(outbuf));
		printf("%s", outbuf);
		if (proto_in_ip == IPPROTO_TCP || proto_in_ip == IPPROTO_UDP)
			printf("(%d)", ntohs(udph->dest));
		printf(" TTL=%d ", iph->ttl);
		if (ip_fast_csum(iph, iph->ihl)) {
			__sum16 org_csum, correct_csum;
			org_csum = iph->check;
			iph->check = 0;
			correct_csum = ip_fast_csum(iph, iph->ihl);
			printf("(bad checksum %04x should be %04x) ",
					ntohs(org_csum), ntohs(correct_csum));
			iph->check = org_csum;
		}
		break;
	case ETH_P_IPV6:
		ip6h = (struct ip6_hdr *)(ethh + 1);
		proto_in_ip = ip6h->ip6_nxt;
		udph = (struct udphdr *)((uint8_t *)ip6h + ip6h->ip6_plen);
		tcph = (struct tcphdr *)((uint8_t *)ip6h + ip6h->ip6_plen);
		printf(" ");
		inet_ntop(AF_INET6, (void *)&ip6h->ip6_src, outbuf, sizeof(outbuf));
		printf("%s", outbuf);
		if (proto_in_ip == IPPROTO_TCP || proto_in_ip == IPPROTO_UDP)
			printf("(%d)", ntohs(udph->source));
		printf(" -> ");
		inet_ntop(AF_INET6, (void *)&ip6h->ip6_dst, outbuf, sizeof(outbuf));
		printf("%s", outbuf);
		if (proto_in_ip == IPPROTO_TCP || proto_in_ip == IPPROTO_UDP)
			printf("(%d)", ntohs(udph->dest));
		printf(" ");
		break;
	default:
		printf("protocol %04hx  ", ntohs(ethh->h_proto));
		goto done;
	}

	/* Transport layer */
	switch (proto_in_ip) {
	case IPPROTO_TCP:
		printf("TCP ");
		if (tcph->syn)
			printf("S ");
		if (tcph->fin)
			printf("F ");
		if (tcph->ack)
			printf("A ");
		if (tcph->rst)
			printf("R ");

		printf("seq %u ", ntohl(tcph->seq));
		if (tcph->ack)
			printf("ack %u ", ntohl(tcph->ack_seq));
		break;
	case IPPROTO_UDP:
		printf("UDP ");
		break;
	default:
		printf("protocol %d ", proto_in_ip);
		goto done;
	}

done:
	printf("len=%d\n", len);
}

int num_devices;
struct ps_device devices[PS_MAX_DEVICES];

struct ps_handle handle;
int num_devices_attached;
int devices_attached[PS_MAX_DEVICES];

void print_usage(char *argv0)
{
	fprintf(stderr, "Usage: %s <interface to sniff> <...>",
			argv0);

	exit(2);
}

void parse_opt(int argc, char **argv)
{
	int i, j, ifindex= -1;

	if (argc < 2)
		print_usage(argv[0]);

	for (i = 1; i < argc; i++) {
		ifindex = -1;

		for (j = 0; j < num_devices; j++) {
			if (strcmp(argv[i], devices[j].name) != 0)
				continue;

			ifindex = devices[j].ifindex;
			break;
		}

		if (ifindex == -1) {
			fprintf(stderr, "Interface %s does not exist!\n", argv[i]);
			exit(4);
		}

		for (j = 0; j < num_devices_attached; j++) {
			if (devices_attached[j] == ifindex)
				goto already_attached;
		}

		devices_attached[num_devices_attached] = ifindex;
		num_devices_attached++;

already_attached:
		;
	}

	assert(num_devices_attached > 0);
    printf("attached %d devices, ifindex = %d\n", num_devices_attached, ifindex);
}

void attach()
{
	int ret;
	int i, j;

	ret = ps_init_handle(&handle);
	if (ret != 0) {
		perror("ps_init_handle");
		exit(1);
	}

	for (i = 0; i < num_devices_attached; i++) {
		struct ps_queue queue;

		queue.ifindex = devices_attached[i];

		for (j = 0; j < devices[devices_attached[i]].num_rx_queues; j++) {
			queue.qidx = j;

			ret = ps_attach_rx_device(&handle, &queue);
			if (ret != 0) {
				perror("ps_attach_rx_device");
				exit(1);
			}
		}
	}
}

int packet_write(void *ptr, int length, FILE *fd)
{
    int i;
    for (i = 0; i < length/4 + 1; i ++) {
        fprintf(fd, "%x ", ((int *)ptr)[i]);
        if (i % 16 == 0) {
            fprintf(fd, "\n");
        }
    }
    return 0;
}


void dump()
{
	int ret, i, j;
	struct ps_chunk chunk;
	struct timespec test;
	uint64_t total_packet = 0;
	uint64_t ctr = 1;
    char *pos;

    FILE *fd = fopen("packet.txt", "w");

	ret = ps_alloc_chunk(&handle, &chunk);
	if (ret != 0) {
		perror("ps_alloc_chunk");
		exit(1);
	}

	chunk.cnt = 128; /* no batching */
	chunk.recv_blocking = 1;

	for (;;) {
		int ret = ps_recv_chunk(&handle, &chunk);

		if (ret < 0) {
			if (errno == EINTR)
				continue;

			if (!chunk.recv_blocking && errno == EWOULDBLOCK)
				break;

			assert(0);
		}

        for (j = 0; j < ret; j ++) {
		    total_packet ++;

            pos = chunk.buf + chunk.info[j].offset;
            if (*((int *)(pos + 44)) != 0x10f20100) {
                clock_gettime(CLOCK_MONOTONIC, &test);
                uint64_t a = test.tv_sec * 1000000 + test.tv_nsec / 1000;
                printf("%d %ld\n", *((int *)(pos + 44)), a);
            }
            /*
                                packet_write(chunk.buf + chunk.info[j].offset, chunk.info[j].len, fd);
                                fprintf(fd, "\n----------------%d\n\n", *((int *)(pos + 42)));
            */
        }

		chunk.cnt = 128;
	}
}

void handle_signal(int signal)
{
	fflush(stdout);
	exit(0);
}

int main(int argc, char **argv)
{
	num_devices = ps_list_devices(devices);
	if (num_devices == -1) {
		perror("ps_list_devices");
		exit(1);
	}

	signal(SIGINT, handle_signal);

	parse_opt(argc, argv);
	attach();
	dump();

	return 0;
}
