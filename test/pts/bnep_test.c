/*
 * Copyright (C) 2014 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

/*
 * bnep_test.c
 * based on panu_demo implemented by Ole Reinhardt <ole.reinhardt@kernelconcepts.de>
 */

#include "btstack-config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <net/if_arp.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <btstack/hci_cmds.h>
#include <btstack/run_loop.h>
#include <btstack/sdp_util.h>

#include "hci.h"
#include "btstack_memory.h"
#include "hci_dump.h"
#include "l2cap.h"
#include "sdp.h"
#include "pan.h"
#include "stdin_support.h"

#define NETWORK_TYPE_IPv4       0x0800
#define NETWORK_TYPE_ARP        0x0806
#define NETWORK_TYPE_IPv6       0x86DD
#define ICMP_TYPE_PING_REQUEST  0x08
#define ICMP_TYPE_PING_RESPONSE 0x00

// prototypes
static void show_usage();

// Configuration for PTS
static bd_addr_t pts_addr = {0x00,0x1b,0xDC,0x07,0x32,0xEF};
//static bd_addr_t pts_addr = {0xE0,0x06,0xE6,0xBB,0x95,0x79}; // Ole Thinkpad
// static bd_addr_t other_addr = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x16};
static bd_addr_t other_addr = { 0,0,0,0,0,0};
// broadcast
static bd_addr_t broadcast_addr = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

// Outgoing: Must match PTS TSPX_UUID_src_addresss 
static uint16_t bnep_src_uuid     = 0x1115; 

// Outgoing: Must match PTS TSPX_UUID_dest_address
static uint32_t bnep_dest_uuid    = 0x1116;

// Incoming: Must macht PTS TSPX_UUID_dest_address
static uint16_t bnep_local_service_uuid = 0x1116;


// Sample network protocol type filter set:
//   Ethernet type/length values the range 0x0000 - 0x05dc (Length), 0x05dd - 0x05ff (Reserved in IEEE 802.3)
//   Ethernet type 0x0600-0xFFFF
static bnep_net_filter_t network_protocol_filter [3] = {{0x0000, 0x05dc}, {0x05dd, 0x05ff}, {0x0600, 0xFFFF}};

// Sample multicast filter set:
//   Multicast filter range set to 00:00:00:00:00:00 - 00:00:00:00:00:00 means: We do not want to receive any multicast traffic
//   Ethernet type 0x0600-0xFFFF
static bnep_multi_filter_t multicast_filter [1] = {{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}};



// state
static bd_addr_t local_addr;
static uint16_t bnep_l2cap_psm      = 0x000f;
static uint16_t bnep_cid            = 0;

static uint8_t network_buffer[BNEP_MTU_MIN];
static size_t  network_buffer_len = 0;

static uint8_t panu_sdp_record[200];

static uint16_t setup_ethernet_header(int src_compressed, int dst_compressed, int broadcast, uint16_t network_protocol_type){
    // setup packet
    int pos = 0;
    // destination
    if (broadcast){
        BD_ADDR_COPY(&network_buffer[pos], broadcast_addr);
    } else {
        BD_ADDR_COPY(&network_buffer[pos], dst_compressed ? pts_addr : other_addr);
    }
    pos += 6;
    // source
    BD_ADDR_COPY(&network_buffer[pos], src_compressed ? local_addr   : other_addr);
    pos += 6;
    net_store_16(network_buffer, pos, network_protocol_type);
    pos += 2;
    return pos;
}

static void send_buffer(uint16_t pos){
    network_buffer_len = pos;
    if (bnep_can_send_packet_now(bnep_cid)) {
        bnep_send(bnep_cid, network_buffer, network_buffer_len);
        network_buffer_len = 0;
    }
}

static void send_ethernet_packet(int src_compressed, int dst_compressed){
    int pos = setup_ethernet_header(src_compressed, dst_compressed, 0, NETWORK_TYPE_IPv4); // IPv4
    // dummy data Ethernet packet
    int i;
    for (i = 60; i >= 0 ; i--){
        network_buffer[pos++] = i;
    }
    // test data payload
    for (i = 0; i < 0x5a0 ; i++){
        network_buffer[pos++] = i;
    }
    send_buffer(pos);
}

static void set_network_protocol_filter(void){
    bnep_set_net_type_filter(bnep_cid, network_protocol_filter, 3);
}

static void set_multicast_filter(void){
    bnep_set_multicast_filter(bnep_cid, multicast_filter, 1);
}

/* From RFC 5227 - 2.1.1
   A host probes to see if an address is already in use by broadcasting
   an ARP Request for the desired address.  The client MUST fill in the
   'sender hardware address' field of the ARP Request with the hardware
   address of the interface through which it is sending the packet.  The
   'sender IP address' field MUST be set to all zeroes; this is to avoid
   polluting ARP caches in other hosts on the same link in the case
   where the address turns out to be already in use by another host.
   The 'target hardware address' field is ignored and SHOULD be set to
   all zeroes.  The 'target IP address' field MUST be set to the address
   being probed.  An ARP Request constructed this way, with an all-zero
   'sender IP address', is referred to as an 'ARP Probe'.
*/
#define HARDWARE_TYPE_ETHERNET 0x0001
#define ARP_OPERATION_REQUEST 1
#define ARP_OPERATION_REPLY   2

static void send_arp_probe_ipv4(void){

    // "random address"
    static uint8_t requested_address[4] = {169, 254, 1, 0};
    requested_address[3]++;

    int pos = setup_ethernet_header(1, 0, 1, NETWORK_TYPE_ARP); 
    net_store_16(network_buffer, pos, HARDWARE_TYPE_ETHERNET);
    pos += 2;
    net_store_16(network_buffer, pos, NETWORK_TYPE_IPv4);
    pos += 2;
    network_buffer[pos++] = 6; // Hardware length (HLEN) - 6 MAC  Address
    network_buffer[pos++] = 4; // Protocol length (PLEN) - 4 IPv4 Address
    net_store_16(network_buffer, pos, ARP_OPERATION_REQUEST); 
    pos += 2;
    BD_ADDR_COPY(&network_buffer[pos], local_addr); // Sender Hardware Address (SHA)
    pos += 6;
    bzero(&network_buffer[pos], 4);                 // Sender Protocol Adress (SPA)
    pos += 4;
    BD_ADDR_COPY(&network_buffer[pos], other_addr); // Target Hardware Address (THA) (ignored for requests)
    pos += 6;
    memcpy(&network_buffer[pos], requested_address, 4);
    pos += 4;
    // magically, add some extra bytes for Ethernet padding
    pos += 18;
    send_buffer(pos);
}

static void send_arp_probe_ipv6(void){
    
}

#if 0

static void send_dhcp_discovery(void){
    
}

static void send_dhcp_request(void){
    
}

static void send_dns_request(void){
    
}

static void send_some_ipv6_packet(void){

    bd_addr_t an_addr = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x16};
    memcpy(other_addr, an_addr, 6);

    int pos = setup_ethernet_header(1, 0, 0, NETWORK_TYPE_IPv6); // IPv6
    uint8_t ipv6_packet[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x16, 0x3a, 0x00, 0x05, 0x02, 0x00, 0x00, 0x01, 0x00, 0x8f, 0x00, 0xf3, 0xa2,
        0x00, 0x00, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0xff, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x60, 0x7b, 0x87                 
    };
    memcpy(&network_buffer[pos], ipv6_packet, sizeof(ipv6_packet));
    pos += sizeof(ipv6_packet);
    send_buffer(pos);
}

static void send_some_ipv6_packet_2(void){

    bd_addr_t an_addr = { 0x33, 0x33, 0xFF, 0x60, 0x7B, 0x87};
    memcpy(other_addr, an_addr, 6);

    int pos = setup_ethernet_header(1, 0, 0, NETWORK_TYPE_IPv6); // IPv6
    uint8_t ipv6_packet[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3a, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x02, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x60, 0x7b,
        0x87, 0x87, 0x00, 0xb6, 0x64, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x5e, 0xf3, 0x70, 0xff, 0xfe, 0x60, 0x7b, 0x87       
    };
    memcpy(&network_buffer[pos], ipv6_packet, sizeof(ipv6_packet));
    pos += sizeof(ipv6_packet);
    send_buffer(pos);
}
#endif

static uint16_t sum_ones_complement(uint16_t a, uint16_t b){
    uint32_t sum = a + b;
    while (sum > 0xffff){
        sum = (sum & 0xffff) + 1;
    }
    return sum;
}

static uint16_t calc_internet_checksum(uint8_t * data, int size){
    uint32_t checksum = 0;
    while (size){
        // add 16-bit value
        checksum = sum_ones_complement(checksum, *(uint16_t*)data);
        data += 2;
        size -= 2;
    }
    return checksum;
}

static void send_ping_request_ipv4(void){

    uint8_t ipv4_packet[] = {
        // ip
        0x45, 0x00, 0x00, 0x00,   // version + ihl, dscp } ecn, total len
        0x00, 0x00, 0x00, 0x00, // identification (16), flags + fragment offset
        0x01, 0x01, 0x00, 0x00, // time to live, procotol: icmp, checksum (16),
        0x00, 0x00, 0x00, 0x00, // source IP address
        0x00, 0x00, 0x00, 0x00, // destination IP address
    };

    uint8_t icmp_packet[] = {
        // icmp
        0x08, 0x00, 0x00, 0x00, // type: 0x08 PING Request
        0x00, 0x00, 0x00, 0x00
    };

    // ethernet header
    int pos = setup_ethernet_header(1, 0, 0, NETWORK_TYPE_IPv4); // IPv4
    
    // ipv4
    int total_length = sizeof(ipv4_packet) + sizeof(icmp_packet);
    net_store_16(ipv4_packet, 2, total_length);
    uint16_t ipv4_checksum = calc_internet_checksum(ipv4_packet, sizeof(ipv4_packet));
    net_store_16(ipv4_packet, 10, ipv4_checksum);    
    // TODO: also set src/dest ip address
    memcpy(&network_buffer[pos], ipv4_packet, sizeof(ipv4_packet));
    pos += sizeof(ipv4_packet);

    // icmp
    uint16_t icmp_checksum = calc_internet_checksum(icmp_packet, sizeof(icmp_packet));
    net_store_16(icmp_packet, 2, icmp_checksum);    
    memcpy(&network_buffer[pos], icmp_packet, sizeof(icmp_packet));
    pos += sizeof(icmp_packet);

    // send
    send_buffer(pos);
}

static void send_ping_request_ipv6(void){

    uint8_t ipv6_packet[] = {
        // ip
        0x60, 0x00, 0x00, 0x00, // version (4) + traffic class (8) + flow label (24)
        0x00, 0x00,   58, 0x01, // payload length(16), next header = IPv6-ICMP, hop limit
        0x00, 0x00, 0x00, 0x00, // source IP address
        0x00, 0x00, 0x00, 0x00, // source IP address
        0x00, 0x00, 0x00, 0x00, // source IP address
        0x00, 0x00, 0x00, 0x00, // source IP address
        0x00, 0x00, 0x00, 0x00, // destination IP address
        0x00, 0x00, 0x00, 0x00, // destination IP address
        0x00, 0x00, 0x00, 0x00, // destination IP address
        0x00, 0x00, 0x00, 0x00, // destination IP address
    };

    uint8_t icmp_packet[] = {
        // icmp
        0x80, 0x00, 0x00, 0x00, // type: 0x08 PING Request, codde = 0, checksum(16)
        0x00, 0x00, 0x00, 0x00  // message
    };

    // ethernet header
    int pos = setup_ethernet_header(1, 0, 0, NETWORK_TYPE_IPv4); // IPv4
    
    // ipv6
    int payload_length = sizeof(icmp_packet);
    net_store_16(ipv6_packet, 4, payload_length);
    // TODO: also set src/dest ip address
    int checksum = calc_internet_checksum(&ipv6_packet[8], 32);
    checksum = sum_ones_complement(checksum, sizeof(ipv6_packet) + sizeof(icmp_packet));
    checksum = sum_ones_complement(checksum, 58 << 8);
    net_store_16(icmp_packet, 2, checksum);
    memcpy(&network_buffer[pos], ipv6_packet, sizeof(ipv6_packet));
    pos += sizeof(ipv6_packet);

    // icmp
    uint16_t icmp_checksum = calc_internet_checksum(icmp_packet, sizeof(icmp_packet));
    net_store_16(icmp_packet, 2, icmp_checksum);    
    memcpy(&network_buffer[pos], icmp_packet, sizeof(icmp_packet));
    pos += sizeof(icmp_packet);

    // send
    send_buffer(pos);
}

static void send_ping_response_ipv4(void){

    uint8_t ipv4_packet[] = {
        // ip
        0x45, 0x00, 0x00, 0x00,   // version + ihl, dscp } ecn, total len
        0x00, 0x00, 0x00, 0x00, // identification (16), flags + fragment offset
        0x01, 0x01, 0x00, 0x00, // time to live, procotol: icmp, checksum (16),
        0x00, 0x00, 0x00, 0x00, // source IP address
        0x00, 0x00, 0x00, 0x00, // destination IP address
    };

    uint8_t icmp_packet[] = {
        // icmp
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    // ethernet header
    int pos = setup_ethernet_header(1, 0, 0, NETWORK_TYPE_IPv4); // IPv4
    
    // ipv4
    int total_length = sizeof(ipv4_packet) + sizeof(icmp_packet);
    net_store_16(ipv4_packet, 2, total_length);
    uint16_t ipv4_checksum = calc_internet_checksum(ipv4_packet, sizeof(ipv4_packet));
    net_store_16(ipv4_packet, 10, ipv4_checksum);    
    // TODO: also set src/dest ip address
    memcpy(&network_buffer[pos], ipv4_packet, sizeof(ipv4_packet));
    pos += sizeof(ipv4_packet);

    // icmp
    uint16_t icmp_checksum = calc_internet_checksum(icmp_packet, sizeof(icmp_packet));
    net_store_16(icmp_packet, 2, icmp_checksum);    
    memcpy(&network_buffer[pos], icmp_packet, sizeof(icmp_packet));
    pos += sizeof(icmp_packet);

    // send
    send_buffer(pos);
}

static void show_usage(void){

    printf("\n--- Bluetooth BNEP Test Console ---\n");
    printf("Source        UUID %04x (== TSPX_UUID_src_address)\n", bnep_src_uuid);
    printf("Destination   UUID %04x (== TSPX_UUID_dest_address)\n", bnep_dest_uuid);
    printf("Local service UUID %04x (== TSPX_UUID_dest_address)\n", bnep_local_service_uuid);
    printf("---\n");
    printf("p - connect to PTS\n");
    printf("e - send general Ethernet packet\n");
    printf("c - send compressed Ethernet packet\n");
    printf("s - send source only compressed Ethernet packet\n");
    printf("d - send destination only compressed Ethernet packet\n");
    printf("f - set network filter\n");
    printf("m - set multicast network filter\n");
    printf("---\n");
    printf("1 - send ICMP Ping Request IPv4\n");
    printf("2 - send ICMP Ping Request IPv6\n");
    printf("4 - send IPv4 ARP request\n");
    printf("6 - send IPv6 ARP request\n");
#if 0
    printf("1 - get IP address via DHCP\n");
    printf("2 - send DNS request\n");
    printf("9 - send some IPv6 packet\n");
    printf("0 - send some IPv6 packet 2\n");
#endif
    printf("---\n");
    printf("Ctrl-c - exit\n");
    printf("---\n");
}

static int stdin_process(struct data_source *ds){
    char buffer;
    read(ds->fd, &buffer, 1);

    switch (buffer){
        case 'p':
            printf("Connecting to PTS at %s...\n", bd_addr_to_str(pts_addr));
            bnep_connect(NULL, pts_addr, bnep_l2cap_psm, bnep_src_uuid, bnep_dest_uuid);
            break;
        case 'e':
            printf("Sending general ethernet packet\n");
            send_ethernet_packet(0,0);
            break;
        case 'c':
            printf("Sending compressed ethernet packet\n");
            send_ethernet_packet(1,1);
            break;
        case 's':
            printf("Sending src only compressed ethernet packet\n");
            send_ethernet_packet(0,1);
            break;
        case 'd':
            printf("Sending dst only ethernet packet\n");
            send_ethernet_packet(1,0);
            break;
        case 'f':
            printf("Setting network protocol filter\n");
            set_network_protocol_filter();
            break;
        case 'm':
            printf("Setting multicast filter\n");
            set_multicast_filter();
            break;
        case '1':
            printf("Sending ICMP Ping via IPv4\n");
            send_ping_request_ipv4();
            break;
        case '2':
            printf("Sending ICMP Ping via IPv6\n");
            send_ping_request_ipv6();
            break;
        case '4':
            printf("Sending IPv4 ARP Probe\n");
            send_arp_probe_ipv4();
            break;
        case '6':
            printf("Sending IPv6 ARP Probe\n");
            send_arp_probe_ipv6();
            break;
#if 0
        case '9':
            printf("Sending some IPv6 packet\n");
            send_some_ipv6_packet();
            break;
        case '0':
            printf("Sending some IPv6 packet 2\n");
            send_some_ipv6_packet_2();
            break;
#endif
        default:
            show_usage();
            break;

    }
    return 0;
}

/*************** PANU client routines *********************/
static void packet_handler (void * connection, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    uint8_t   event;
    bd_addr_t event_addr;
    bd_addr_t src_addr;
    bd_addr_t dst_addr;
    uint16_t  uuid_source;
    uint16_t  uuid_dest;
    uint16_t  mtu;    
    uint16_t  network_type;
    uint8_t   icmp_type;
    int       ihl;
    int       payload_offset;

    switch (packet_type) {
		case HCI_EVENT_PACKET:
            event = packet[0];
            switch (event) {            
                case BTSTACK_EVENT_STATE:
                    /* BT Stack activated, get started */ 
                    if (packet[2] == HCI_STATE_WORKING) {
                        printf("BNEP Test ready\n");
                        show_usage();
                    }
                    break;

                case HCI_EVENT_COMMAND_COMPLETE:
					if (COMMAND_COMPLETE_EVENT(packet, hci_read_bd_addr)){
                        bt_flip_addr(local_addr, &packet[6]);
                        printf("BD-ADDR: %s\n", bd_addr_to_str(local_addr));
                        break;
                    }
                    break;

                case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                    // inform about user confirmation request
                    printf("SSP User Confirmation Request with numeric value '%06u'\n", READ_BT_32(packet, 8));
                    printf("SSP User Confirmation Auto accept\n");
                    break;
					
				case BNEP_EVENT_OPEN_CHANNEL_COMPLETE:
                    if (packet[2]) {
                        printf("BNEP channel open failed, status %02x\n", packet[2]);
                    } else {
                        // data: event(8), len(8), status (8), bnep source uuid (16), bnep destination uuid (16), remote_address (48)
                        uuid_source = READ_BT_16(packet, 3);
                        uuid_dest   = READ_BT_16(packet, 5);
                        mtu         = READ_BT_16(packet, 7);
                        bnep_cid    = channel;
                        //bt_flip_addr(event_addr, &packet[9]); 
                        memcpy(&event_addr, &packet[9], sizeof(bd_addr_t));
                        printf("BNEP connection open succeeded to %s source UUID 0x%04x dest UUID: 0x%04x, max frame size %u\n", bd_addr_to_str(event_addr), uuid_source, uuid_dest, mtu);
                    }
					break;
                    
                case BNEP_EVENT_CHANNEL_TIMEOUT:
                    printf("BNEP channel timeout! Channel will be closed\n");
                    break;
                    
                case BNEP_EVENT_CHANNEL_CLOSED:
                    printf("BNEP channel closed\n");
                    break;

                case BNEP_EVENT_READY_TO_SEND:
                    /* Check for parked network packets and send it out now */
                    if (network_buffer_len > 0) {
                        bnep_send(bnep_cid, network_buffer, network_buffer_len);
                        network_buffer_len = 0;
                    }
                    break;
                    
                default:
                    break;
            }
            break;
        case BNEP_DATA_PACKET:
            // show received packet on console

            // TODO: fix BNEP to return BD ADDR in little endian, to use these lines
            // bt_flip_addr(dst_addr, &packet[0]);
            // bt_flip_addr(src_addr, &packet[6]);
            // instead of these
            memcpy(dst_addr, &packet[0], 6);
            memcpy(src_addr, &packet[6], 6);
            // END TOOD

            network_type = READ_NET_16(packet, 12);
            printf("BNEP packet received\n");
            printf("Dst Addr: %s\n", bd_addr_to_str(dst_addr));
            printf("Src Addr: %s\n", bd_addr_to_str(src_addr));
            printf("Net Type: %04x\n", network_type);
            // ignore the next 60 bytes
            hexdumpf(&packet[74], size - 74);
            switch (network_type){
                case NETWORK_TYPE_IPv4:
                    ihl = packet[14] & 0x0f;
                    payload_offset = 14 + (ihl << 2);
                    // 
                    icmp_type = packet[payload_offset];
                    hexdumpf(&packet[payload_offset], size - payload_offset);
                    printf("ICMP packet of type %x\n", icmp_type);
                    switch (icmp_type){
                        case ICMP_TYPE_PING_REQUEST:
                            printf("IPv4 Ping Request received, sending pong\n");
                            send_ping_response_ipv4();
                            break;
                        break;
                    }
            }

            break;            
            
        default:
            break;
    }
}

int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){
    

    /* Initialize L2CAP */
    l2cap_init();
    l2cap_register_packet_handler(packet_handler);

    /* Initialise BNEP */
    bnep_init();
    bnep_register_packet_handler(packet_handler);
    bnep_register_service(NULL, bnep_local_service_uuid, 1691);  /* Minimum L2CAP MTU for bnep is 1691 bytes */

    /* Initialize SDP and add PANU record */
    sdp_init();

    uint16_t network_packet_types[] = { NETWORK_TYPE_IPv4, NETWORK_TYPE_ARP, 0};    // 0 as end of list
#ifdef EMBEDDED
    service_record_item_t * service_record_item = (service_record_item_t *) panu_sdp_record;
    pan_create_panu_service((uint8_t*) &service_record_item->service_record, network_packet_types, NULL, NULL, BNEP_SECURITY_NONE);
    printf("SDP service buffer size: %u\n", (uint16_t) (sizeof(service_record_item_t) + de_get_len((uint8_t*) &service_record_item->service_record)));
    sdp_register_service_internal(NULL, service_record_item);
#else
    pan_create_panu_service(panu_sdp_record, network_packet_types, NULL, NULL, BNEP_SECURITY_NONE);
    printf("SDP service record size: %u\n", de_get_len((uint8_t*) panu_sdp_record));
    sdp_register_service_internal(NULL, (uint8_t*)panu_sdp_record);
#endif

    /* Turn on the device */
    hci_power_control(HCI_POWER_ON);
    hci_discoverable_control(1);

    btstack_stdin_setup(stdin_process);

    return 0;
}

/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4 -*-  */
