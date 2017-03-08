/*
 * Copyright (C) 2016 BlueKitchen GmbH
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


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "btstack.h"
#include "avdtp.h"
#include "avdtp_sink.h"
#include "avdtp_util.h"
#include "avdtp_initiator.h"
#include "avdtp_acceptor.h"

static const char * default_avdtp_sink_service_name = "BTstack AVDTP Sink Service";
static const char * default_avdtp_sink_service_provider_name = "BTstack AVDTP Sink Service Provider";

static avdtp_context_t avdtp_sink_context;

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

//static void (*handle_media_data)(avdtp_stream_endpoint_t * stream_endpoint, uint8_t *packet, uint16_t size);    

void a2dp_sink_create_sdp_record(uint8_t * service,  uint32_t service_record_handle, uint16_t supported_features, const char * service_name, const char * service_provider_name){
    uint8_t* attribute;
    de_create_sequence(service);

    // 0x0000 "Service Record Handle"
    de_add_number(service, DE_UINT, DE_SIZE_16, SDP_ServiceRecordHandle);
    de_add_number(service, DE_UINT, DE_SIZE_32, service_record_handle);

    // 0x0001 "Service Class ID List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, SDP_ServiceClassIDList);
    attribute = de_push_sequence(service);
    {
        de_add_number(attribute, DE_UUID, DE_SIZE_16, AUDIO_SINK_GROUP); 
    }
    de_pop_sequence(service, attribute);

    // 0x0004 "Protocol Descriptor List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, SDP_ProtocolDescriptorList);
    attribute = de_push_sequence(service);
    {
        uint8_t* l2cpProtocol = de_push_sequence(attribute);
        {
            de_add_number(l2cpProtocol,  DE_UUID, DE_SIZE_16, SDP_L2CAPProtocol);
            de_add_number(l2cpProtocol,  DE_UINT, DE_SIZE_16, PSM_AVDTP);  
        }
        de_pop_sequence(attribute, l2cpProtocol);
        
        uint8_t* avProtocol = de_push_sequence(attribute);
        {
            de_add_number(avProtocol,  DE_UUID, DE_SIZE_16, PSM_AVDTP);  // avProtocol_service
            de_add_number(avProtocol,  DE_UINT, DE_SIZE_16,  0x0103);  // version
        }
        de_pop_sequence(attribute, avProtocol);
    }
    de_pop_sequence(service, attribute);

    // 0x0005 "Public Browse Group"
    de_add_number(service,  DE_UINT, DE_SIZE_16, SDP_BrowseGroupList); // public browse group
    attribute = de_push_sequence(service);
    {
        de_add_number(attribute,  DE_UUID, DE_SIZE_16, SDP_PublicBrowseGroup);
    }
    de_pop_sequence(service, attribute);

    // 0x0009 "Bluetooth Profile Descriptor List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, SDP_BluetoothProfileDescriptorList);
    attribute = de_push_sequence(service);
    {
        uint8_t *a2dProfile = de_push_sequence(attribute);
        {
            de_add_number(a2dProfile,  DE_UUID, DE_SIZE_16, ADVANCED_AUDIO_DISTRIBUTION); 
            de_add_number(a2dProfile,  DE_UINT, DE_SIZE_16, 0x0103); 
        }
        de_pop_sequence(attribute, a2dProfile);
    }
    de_pop_sequence(service, attribute);


    // 0x0100 "Service Name"
    de_add_number(service,  DE_UINT, DE_SIZE_16, 0x0100);
    if (service_name){
        de_add_data(service,  DE_STRING, strlen(service_name), (uint8_t *) service_name);
    } else {
        de_add_data(service,  DE_STRING, strlen(default_avdtp_sink_service_name), (uint8_t *) default_avdtp_sink_service_name);
    }

    // 0x0100 "Provider Name"
    de_add_number(service,  DE_UINT, DE_SIZE_16, 0x0102);
    if (service_provider_name){
        de_add_data(service,  DE_STRING, strlen(service_provider_name), (uint8_t *) service_provider_name);
    } else {
        de_add_data(service,  DE_STRING, strlen(default_avdtp_sink_service_provider_name), (uint8_t *) default_avdtp_sink_service_provider_name);
    }

    // 0x0311 "Supported Features"
    de_add_number(service, DE_UINT, DE_SIZE_16, 0x0311);
    de_add_number(service, DE_UINT, DE_SIZE_16, supported_features);
}


void avdtp_sink_register_media_transport_category(uint8_t seid){
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_for_seid(seid, &avdtp_sink_context);
    avdtp_register_media_transport_category(stream_endpoint);
}

void avdtp_sink_register_reporting_category(uint8_t seid){
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_for_seid(seid, &avdtp_sink_context);
    avdtp_register_reporting_category(stream_endpoint);
}

void avdtp_sink_register_delay_reporting_category(uint8_t seid){
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_for_seid(seid, &avdtp_sink_context);
    avdtp_register_delay_reporting_category(stream_endpoint);
}

void avdtp_sink_register_recovery_category(uint8_t seid, uint8_t maximum_recovery_window_size, uint8_t maximum_number_media_packets){
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_for_seid(seid, &avdtp_sink_context);
    avdtp_register_recovery_category(stream_endpoint, maximum_recovery_window_size, maximum_number_media_packets);
}

void avdtp_sink_register_content_protection_category(uint8_t seid, uint16_t cp_type, const uint8_t * cp_type_value, uint8_t cp_type_value_len){
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_for_seid(seid, &avdtp_sink_context);
    avdtp_register_content_protection_category(stream_endpoint, cp_type, cp_type_value, cp_type_value_len);
}

void avdtp_sink_register_header_compression_category(uint8_t seid, uint8_t back_ch, uint8_t media, uint8_t recovery){
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_for_seid(seid, &avdtp_sink_context);
    avdtp_register_header_compression_category(stream_endpoint, back_ch, media, recovery);
}

void avdtp_sink_register_media_codec_category(uint8_t seid, avdtp_media_type_t media_type, avdtp_media_codec_type_t media_codec_type, const uint8_t * media_codec_info, uint16_t media_codec_info_len){
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_for_seid(seid, &avdtp_sink_context);
    avdtp_register_media_codec_category(stream_endpoint, media_type, media_codec_type, media_codec_info, media_codec_info_len);
}

void avdtp_sink_register_multiplexing_category(uint8_t seid, uint8_t fragmentation){
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_for_seid(seid, &avdtp_sink_context);
    avdtp_register_multiplexing_category(stream_endpoint, fragmentation);
}

// // media, reporting. recovery
// void avdtp_sink_register_media_transport_identifier_for_multiplexing_category(uint8_t seid, uint8_t fragmentation){

// }


/* END: tracking can send now requests pro l2cap cid */
// TODO remove

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    avdtp_packet_handler(packet_type, channel, packet, size, &avdtp_sink_context);
}

// TODO: find out which security level is needed, and replace LEVEL_0 in avdtp_sink_init
void avdtp_sink_init(void){
    avdtp_sink_context.stream_endpoints = NULL;
    avdtp_sink_context.connections = NULL;
    avdtp_sink_context.stream_endpoints_id_counter = 0;
    // TODO: assign dummy  handlers;

    l2cap_register_service(&packet_handler, PSM_AVDTP, 0xffff, LEVEL_0);
}

avdtp_stream_endpoint_t * avdtp_sink_create_stream_endpoint(avdtp_sep_type_t sep_type, avdtp_media_type_t media_type){
    return avdtp_create_stream_endpoint(sep_type, media_type, &avdtp_sink_context);
}

void avdtp_sink_register_media_handler(void (*callback)(avdtp_stream_endpoint_t * stream_endpoint, uint8_t *packet, uint16_t size)){
    if (callback == NULL){
        log_error("avdtp_sink_register_media_handler called with NULL callback");
        return;
    }
    avdtp_sink_context.handle_media_data = callback;
}

void avdtp_sink_register_packet_handler(btstack_packet_handler_t callback){
    if (callback == NULL){
        log_error("avdtp_sink_register_packet_handler called with NULL callback");
        return;
    }
    avdtp_sink_context.avdtp_callback = callback;
}

void avdtp_sink_connect(bd_addr_t bd_addr){
    avdtp_connection_t * connection = avdtp_connection_for_bd_addr(bd_addr, &avdtp_sink_context);
    if (!connection){
        connection = avdtp_create_connection(bd_addr, &avdtp_sink_context);
    }
    if (connection->state != AVDTP_SIGNALING_CONNECTION_IDLE) return;
    connection->state = AVDTP_SIGNALING_CONNECTION_W4_L2CAP_CONNECTED;
    l2cap_create_channel(packet_handler, connection->remote_addr, PSM_AVDTP, 0xffff, NULL);
}

void avdtp_sink_disconnect(uint16_t con_handle){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection) return;
    if (connection->state == AVDTP_SIGNALING_CONNECTION_IDLE) return;
    if (connection->state == AVDTP_SIGNALING_CONNECTION_W4_L2CAP_DISCONNECTED) return;
    
    connection->disconnect = 1;
    avdtp_request_can_send_now_self(connection, connection->l2cap_signaling_cid);
}

void avdtp_sink_open_stream(uint16_t con_handle, uint8_t acp_seid){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_media_connect: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) {
        printf("avdtp_sink_media_connect: wrong connection state %d\n", connection->state);
        return;
    }

    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_associated_with_acp_seid(acp_seid, &avdtp_sink_context);
    if (!stream_endpoint) {
        printf("avdtp_sink_media_connect: no stream_endpoint with acp seid %d found\n", acp_seid);
        return;
    }
    
    if (stream_endpoint->state < AVDTP_STREAM_ENDPOINT_CONFIGURED) return;
    if (stream_endpoint->remote_sep_index == 0xFF) return;

    printf(" AVDTP_INITIATOR_W2_MEDIA_CONNECT \n");
    connection->initiator_transaction_label++;
    connection->acp_seid = acp_seid;
    connection->int_seid = stream_endpoint->sep.seid;
    stream_endpoint->initiator_config_state = AVDTP_INITIATOR_W2_MEDIA_CONNECT;
    avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
}

void avdtp_sink_start_stream(uint16_t con_handle, uint8_t acp_seid){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_media_connect: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) {
        printf("avdtp_sink_media_connect: wrong connection state %d\n", connection->state);
        return;
    }

    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_associated_with_acp_seid(acp_seid, &avdtp_sink_context);
    if (!stream_endpoint) {
        printf("avdtp_sink_media_connect: no stream_endpoint with acp_seid %d found\n", acp_seid);
        return;
    }
    if (stream_endpoint->remote_sep_index == 0xFF) return;
    if (stream_endpoint->state < AVDTP_STREAM_ENDPOINT_OPENED) return;
    printf(" AVDTP_INITIATOR_W2_STREAMING_START \n");
    connection->initiator_transaction_label++;
    connection->acp_seid = acp_seid;
    connection->int_seid = stream_endpoint->sep.seid;
    stream_endpoint->initiator_config_state = AVDTP_INITIATOR_W2_STREAMING_START;
    avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
}

void avdtp_sink_stop_stream(uint16_t con_handle, uint8_t acp_seid){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_stop_stream: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) {
        printf("avdtp_sink_stop_stream: wrong connection state %d\n", connection->state);
        return;
    }

    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_associated_with_acp_seid(acp_seid, &avdtp_sink_context);
    if (!stream_endpoint) {
        printf("avdtp_sink_stop_stream: no stream_endpoint with acp seid %d found\n", acp_seid);
        return;
    }
    if (stream_endpoint->remote_sep_index == 0xFF) return;
    switch (stream_endpoint->state){
        case AVDTP_STREAM_ENDPOINT_OPENED:
        case AVDTP_STREAM_ENDPOINT_STREAMING:
            printf(" AVDTP_INITIATOR_W2_STREAMING_STOP \n");
            connection->initiator_transaction_label++;
            connection->acp_seid = acp_seid;
            connection->int_seid = stream_endpoint->sep.seid;
            stream_endpoint->initiator_config_state = AVDTP_INITIATOR_W2_STREAMING_STOP;
            avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
            break;
        default:
            break;
    }
}

void avdtp_sink_abort_stream(uint16_t con_handle, uint8_t acp_seid){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_abort_stream: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) {
        printf("avdtp_sink_abort_stream: wrong connection state %d\n", connection->state);
        return;
    }

    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_associated_with_acp_seid(acp_seid, &avdtp_sink_context);
    if (!stream_endpoint) {
        printf("avdtp_sink_abort_stream: no stream_endpoint for seid %d found\n", acp_seid);
        return;
    }
    if (stream_endpoint->remote_sep_index == 0xFF) return;
    switch (stream_endpoint->state){
        case AVDTP_STREAM_ENDPOINT_CONFIGURED:
        case AVDTP_STREAM_ENDPOINT_CLOSING:
        case AVDTP_STREAM_ENDPOINT_OPENED:
        case AVDTP_STREAM_ENDPOINT_STREAMING:
            printf(" AVDTP_INITIATOR_W2_STREAMING_ABORT \n");
            connection->initiator_transaction_label++;
            connection->acp_seid = acp_seid;
            connection->int_seid = stream_endpoint->sep.seid;
            stream_endpoint->initiator_config_state = AVDTP_INITIATOR_W2_STREAMING_ABORT;
            avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
            break;
        default:
            break;
    }
}

void avdtp_sink_discover_stream_endpoints(uint16_t con_handle){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_discover_stream_endpoints: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return;

    switch (connection->initiator_connection_state){
        case AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE:
            connection->initiator_transaction_label++;
            connection->initiator_connection_state = AVDTP_SIGNALING_CONNECTION_INITIATOR_W2_DISCOVER_SEPS;
            avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
            break;
        default:
            printf("avdtp_sink_discover_stream_endpoints: wrong state\n");
            break;
    }
}


void avdtp_sink_get_capabilities(uint16_t con_handle, uint8_t acp_seid){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_get_capabilities: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return;
    if (connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE) return;
    connection->initiator_transaction_label++;
    connection->initiator_connection_state = AVDTP_SIGNALING_CONNECTION_INITIATOR_W2_GET_CAPABILITIES;
    connection->acp_seid = acp_seid;
    avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
}


void avdtp_sink_get_all_capabilities(uint16_t con_handle, uint8_t acp_seid){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_get_all_capabilities: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return;
    if (connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE) return;
    connection->initiator_transaction_label++;
    connection->initiator_connection_state = AVDTP_SIGNALING_CONNECTION_INITIATOR_W2_GET_ALL_CAPABILITIES;
    connection->acp_seid = acp_seid;
    avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
}

void avdtp_sink_get_configuration(uint16_t con_handle, uint8_t acp_seid){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_get_configuration: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return;
    if (connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE) return;
    connection->initiator_transaction_label++;
    connection->initiator_connection_state = AVDTP_SIGNALING_CONNECTION_INITIATOR_W2_GET_CONFIGURATION;
    connection->acp_seid = acp_seid;
    avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
}

void avdtp_sink_set_configuration(uint16_t con_handle, uint8_t int_seid, uint8_t acp_seid, uint16_t configured_services_bitmap, avdtp_capabilities_t configuration){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_set_configuration: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return;
    if (connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE) return;
    
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_for_seid(int_seid, &avdtp_sink_context);
    if (!stream_endpoint) {
        printf("avdtp_sink_set_configuration: no initiator stream endpoint for seid %d\n", int_seid);
        return;
    }        
    printf("avdtp_sink_set_configuration int seid %d, acp seid %d\n", int_seid, acp_seid);
    
    connection->initiator_transaction_label++;
    connection->acp_seid = acp_seid;
    connection->int_seid = int_seid;
    connection->remote_capabilities_bitmap = configured_services_bitmap;
    connection->remote_capabilities = configuration;
    stream_endpoint->initiator_config_state = AVDTP_INITIATOR_W2_SET_CONFIGURATION;
    avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
}

void avdtp_sink_reconfigure(uint16_t con_handle, uint8_t acp_seid, uint16_t configured_services_bitmap, avdtp_capabilities_t configuration){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_reconfigure: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    //TODO: if opened only app capabilities, enable reconfigure for not opened
    if (connection->state < AVDTP_SIGNALING_CONNECTION_OPENED) return;
    if (connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE) return;
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_associated_with_acp_seid(acp_seid, &avdtp_sink_context);
    if (!stream_endpoint) return;
    if (stream_endpoint->remote_sep_index == 0xFF) return;
    connection->initiator_transaction_label++;
    connection->acp_seid = acp_seid;
    connection->int_seid = stream_endpoint->sep.seid;
    connection->remote_capabilities_bitmap = configured_services_bitmap;
    connection->remote_capabilities = configuration;
    stream_endpoint->initiator_config_state = AVDTP_INITIATOR_W2_RECONFIGURE_STREAM_WITH_SEID;
    avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
}

void avdtp_sink_suspend(uint16_t con_handle, uint8_t acp_seid){
    avdtp_connection_t * connection = avdtp_connection_for_con_handle(con_handle, &avdtp_sink_context);
    if (!connection){
        printf("avdtp_sink_suspend: no connection for handle 0x%02x found\n", con_handle);
        return;
    }
    if (connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) return;
    if (connection->initiator_connection_state != AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE) return;
    avdtp_stream_endpoint_t * stream_endpoint = avdtp_stream_endpoint_associated_with_acp_seid(acp_seid, &avdtp_sink_context);
    if (!stream_endpoint) return;
    if (stream_endpoint->remote_sep_index == 0xFF) return;
    connection->initiator_transaction_label++;
    connection->acp_seid = acp_seid;
    connection->int_seid = stream_endpoint->sep.seid;
    stream_endpoint->initiator_config_state = AVDTP_INITIATOR_W2_SUSPEND_STREAM_WITH_SEID;
    avdtp_request_can_send_now_initiator(connection, connection->l2cap_signaling_cid);
}