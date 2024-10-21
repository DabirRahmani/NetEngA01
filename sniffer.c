#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>


#define BUFFER_SIZE 65536

const char* get_icmp_type_string(unsigned int type) {
    switch (type) {
        case 0: return "Echo Reply";
        case 3: return "Destination Unreachable";
        case 4: return "Source Quench";
        case 5: return "Redirect";
        case 8: return "Echo Request";
        case 9: return "Router Advertisement";
        case 10: return "Router Solicitation";
        case 11: return "Time Exceeded";
        case 12: return "Parameter Problem";
        case 13: return "Timestamp";
        case 14: return "Timestamp Reply";
        case 15: return "Information Request";
        case 16: return "Information Reply";
        case 17: return "Address Mask Request";
        case 18: return "Address Mask Reply";
        default: return "Unknown Type";
    }
}

const char* get_icmp6_type_string(unsigned int type) {
    switch (type) {
        case 1: return "Destination Unreachable";
        case 2: return "Packet Too Big";
        case 3: return "Time Exceeded";
        case 4: return "Parameter Problem";
        case 128: return "Echo Request";
        case 129: return "Echo Reply";
        case 135: return "Neighbor Solicitation";
        default: return "Unknown Type";
    }
}

int main() {
    int raw_sock;
    struct sockaddr saddr;
    int saddr_len = sizeof(saddr);
    unsigned char *buffer = (unsigned char *)malloc(BUFFER_SIZE);

    // سوکت خام
    // روی هیچی بایند نمیکنیم که تمام ترافیک رو بگیره
    raw_sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (raw_sock < 0) {
        perror("Socket Error");
        return 1;
    }

    while (1) {
        printf("*****************************************************************************************************\n");
        // سایز پکت رو نگه میداریم بعدا نیاز میشه
        int data_size = recvfrom(raw_sock, buffer, BUFFER_SIZE, 0, &saddr, (socklen_t *)&saddr_len);
        if (data_size < 0) {
            perror("Recvfrom error");
            return 1;
        }

        // دیتا لینک
        struct ethhdr *eth = (struct ethhdr *)buffer;
        printf("data link => ethernet:\n");
        printf("Source MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            eth->h_source[0], eth->h_source[1], eth->h_source[2],
            eth->h_source[3], eth->h_source[4], eth->h_source[5]);
        printf("Destination MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            eth->h_dest[0], eth->h_dest[1], eth->h_dest[2],
            eth->h_dest[3], eth->h_dest[4], eth->h_dest[5]);
        printf("\n");

        // چون کلا دیتای این لایه با توجه به ورژن ای پی فرق داره مجبوریم یه سری چیزا رو هی تکرار کنیم
        // اول که ورژن رو به صورت دستی از توی بافر میگیریم
        // بعد از توش استراکت رو میکشیم بیرون 
        unsigned char version = buffer[sizeof(struct ethhdr)] >> 4;
        // ip
        if (version == 4) {
            struct iphdr *ip = (struct iphdr *)(buffer + sizeof(struct ethhdr));
            struct sockaddr_in source, dest;
            memset(&source, 0, sizeof(source));
            source.sin_addr.s_addr = ip->saddr;
            memset(&dest, 0, sizeof(dest));
            dest.sin_addr.s_addr = ip->daddr;
            printf("IPv4 Header:\n");
            printf("IP Version: %d\n", (unsigned int)ip->version);
            printf("Source IP: %s\n", inet_ntoa(source.sin_addr));
            printf("Destination IP: %s\n", inet_ntoa(dest.sin_addr));
            printf("\n");
        
            switch (ip->protocol) {
                case 1: { // icmp
                    struct icmphdr *icmp = (struct icmphdr *)(buffer + ip->ihl * 4 + sizeof(struct ethhdr));
                    printf("ICMP Header:\n");
                    printf("Type: %s\n", get_icmp_type_string((unsigned int)(icmp->type))); 
                    printf("Code: %d\n", (unsigned int)(icmp->code));
                    printf("ID: %d\n", ntohs(icmp->un.echo.id));
                    printf("Sequence: %d\n", ntohs(icmp->un.echo.sequence));
                    printf("Checksum: %d\n", ntohs(icmp->checksum));
                    printf("\n");
                    break;
                }
                case 6: { // tcp
                    struct tcphdr *tcp = (struct tcphdr *)(buffer + ip->ihl * 4 + sizeof(struct ethhdr));
                    printf("TCP Header:\n");
                    printf("Source Port: %u\n", ntohs(tcp->source));
                    printf("Destination Port: %u\n", ntohs(tcp->dest));
                    printf("Sequence Number: %u\n", ntohl(tcp->seq));
                    printf("Acknowledge Number: %u\n", ntohl(tcp->ack_seq));
                    printf("Flags: ");
                    if (tcp->syn) printf("SYN ");
                    if (tcp->ack) printf("ACK ");
                    if (tcp->fin) printf("FIN ");
                    if (tcp->rst) printf("RST ");
                    printf("\n");
                    printf("Window Size: %u\n", ntohs(tcp->window));
                    printf("Checksum: %u\n", ntohs(tcp->check));
                    printf("\n");
                    break;
                }
                case 17: { // udp کدش رو به صورت دستی توی تابع تبدیل به استرینگ میکنیم
                    struct udphdr *udp = (struct udphdr *)(buffer + ip->ihl * 4 + sizeof(struct ethhdr));
                    printf("UDP Header:\n");
                    printf("Source Port: %u\n", ntohs(udp->source));
                    printf("Destination Port: %u\n", ntohs(udp->dest));
                    printf("Length: %u\n", ntohs(udp->len));
                    printf("Checksum: %u\n", ntohs(udp->check));
                    printf("\n");
                    break;
                }
                default: 
                    printf("Unknown protocol\n");
                    break;
            }

        } 
        else if (version == 6) {
            struct ip6_hdr *ipv6 = (struct ip6_hdr *)(buffer + sizeof(struct ethhdr));
            char src_addr[INET6_ADDRSTRLEN], dest_addr[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &(ipv6->ip6_src), src_addr, INET6_ADDRSTRLEN);
            inet_ntop(AF_INET6, &(ipv6->ip6_dst), dest_addr, INET6_ADDRSTRLEN);
            printf("IPv6 Header:\n");
            printf("Source IP: %s\n", src_addr);
            printf("Destination IP: %s\n", dest_addr);
            printf("\n");

            switch (ipv6->ip6_nxt) {
                case IPPROTO_ICMPV6: { // icmpv6
                    struct icmp6_hdr *icmp6 = (struct icmp6_hdr *)(buffer + sizeof(struct ethhdr) + sizeof(struct ip6_hdr));
                    printf("ICMPv6 Header:\n");
                    printf("Type: %s\n", get_icmp6_type_string((unsigned int)(icmp6->icmp6_type)));
                    printf("Code: %d\n", (unsigned int)(icmp6->icmp6_code));
                    printf("Checksum: %d\n", ntohs(icmp6->icmp6_cksum));
                    printf("\n");
                    break;
                }
                case IPPROTO_TCP: { // tcp
                    struct tcphdr *tcp = (struct tcphdr *)(buffer + sizeof(struct ethhdr) + sizeof(struct ip6_hdr));
                    printf("TCP Header:\n");
                    printf("Source Port: %u\n", ntohs(tcp->source));
                    printf("Destination Port: %u\n", ntohs(tcp->dest));
                    printf("Sequence Number: %u\n", ntohl(tcp->seq));
                    printf("Acknowledge Number: %u\n", ntohl(tcp->ack_seq));
                    printf("Flags: ");
                    if (tcp->syn) printf("SYN ");
                    if (tcp->ack) printf("ACK ");
                    if (tcp->fin) printf("FIN ");
                    if (tcp->rst) printf("RST ");
                    printf("\n");
                    printf("Window Size: %u\n", ntohs(tcp->window));
                    printf("Checksum: %u\n", ntohs(tcp->check));
                    printf("\n");
                    break;
                }
                case IPPROTO_UDP: { // udp
                    struct udphdr *udp = (struct udphdr *)(buffer + sizeof(struct ethhdr) + sizeof(struct ip6_hdr));
                    printf("UDP Header:\n");
                    printf("Source Port: %u\n", ntohs(udp->source));
                    printf("Destination Port: %u\n", ntohs(udp->dest));
                    printf("Length: %u\n", ntohs(udp->len));
                    printf("Checksum: %u\n", ntohs(udp->check));
                    printf("\n");
                    break;
                }
                default: 
                    printf("Unknown protocol\n");
                    break;
            }
        } else {
         printf("not v4 neither v6:\n");
         if(data_size == 42)
            printf("Ethernet frame with an 802.1Q tag\n\n");
        } 

        // کل پکت رو پرینت میکنیم
        printf("Entire Packet data: size: %d", data_size);
        for (int i = 0; i < data_size; i++) {
            if(i%32 == 0 && i%16 == 0 && i%16 == 0) {
                printf("\n");
            }
            else if (i%16 == 0){
                printf("    ");
            }
            else if (i%8 == 0){
                printf(" ");
            }
            printf("%02x ", buffer[i]);
        }
        printf("\n");


    }

    close(raw_sock);
    free(buffer);
    return 0;
}
