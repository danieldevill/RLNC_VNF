/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*extended by DBB de Villiers*/

#include <getopt.h>

//Includes for Kodo-c library
#include <time.h>
#include </home/coder/RLNC_VNF/coder/dataplane/kodo-c/shared_kodoc/include/kodoc/kodoc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>

//#include <config.h>
#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_hash.h>
#include <rte_jhash.h>

// #include "util.h"

//#include "dpdk_encode.h"
#include "dpdkr_pipeline.h"

/* Number of packets to attempt to read from queue. */
#define PKT_READ_SIZE  ((uint16_t)64)

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

static volatile bool force_quit;

#define ENCODING_RINGS 128
#define GENID_LEN 8
#define ETHER_TYPE_LEN 2

#define MAC_ENTRIES 200
#define NB_MBUF 8192
#define MAX_PKT_BURST 32
#define MEMPOOL_CACHE_SIZE 512

#define RX_RING_SIZE 512
#define TX_RING_SIZE 512

/* Coder type: -1,0,1,2 for none,encoder,decoder and recoder */
char* coder = NULL;
int coder_idx = -1;

uint32_t gentable_size = 0;

static unsigned genIDcounter = 0;

struct generationID {
    char ID[GENID_LEN];
};

/* GenerationID table: Used to store list of all generations still being decoded. */
struct generationID *genID_table;

/* Encoding rings array: Used to store all encoding rings based on dst_addr*/
struct rte_ring encoding_rings[ENCODING_RINGS];

static unsigned mac_counter = 0;

/* Mac Fwd TABLE to group generations by dst_addr */
struct mac_table_entry {
    struct ether_addr d_addr;
};
struct mac_table_entry *mac_fwd_table; 

static const struct rte_eth_conf port_conf = {
    .rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
};

//Kodo-c init:
//Define num symbols and size.
//Values are just selected from examples for now.
static uint32_t MAX_SYMBOLS = 8;
static uint32_t MAX_SYMBOL_SIZE = 1400; //Size of MTU datatgram.
//Select codec
static uint32_t codec = kodoc_full_vector; //Sliding window can make use of feedback.
//Finite field to use
static uint32_t finite_field = kodoc_binary8;

struct rte_mempool * codingmbuf_pool = NULL;

struct rte_mempool * l2fwd_pktmbuf_pool = NULL;

static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        force_quit = true;
        rte_eal_cleanup();
        printf("Quiting..\n");
    }
}

/*
 * Parse the application arguments to the client app.
 */
static int
parse_app_args(int argc, char *argv[])
{
    int option_index = 0, opt = 0;
    char **argvopt = argv;
    const char *progname = NULL;
    static struct option lgopts[] = {
        {NULL, 0, NULL, 0 }
    };
    progname = argv[0];

    while ((opt = getopt_long(argc, argvopt, "c:", lgopts,
        &option_index)) != EOF) {
        switch(opt) {
            case 'c':
                coder = optarg;
                if(strcmp(coder,"encode")==0)
                {
                    coder_idx = 0;
                    printf("Coding mode: Encode\n");
                }
                else if(strcmp(coder,"decode")==0)
                {
                    coder_idx = 1;
                    printf("Coding mode: Decode\n"); 
                }
                else if(strcmp(coder,"recode")==0)
                {
                    coder_idx = 2;
                    printf("Coding mode: Recode\n"); 
                }
                else
                {
                    coder_idx = -1;
                    printf("Coding mode: No coding\n");
                }
                break;
        }
    }

    return 0;
}

static struct rte_ring*
genID_in_genTable(char *generationID) //Need to add a flushing policy
{
    //Loop through genID_table, and check if generationID exists in table.
    int in_table = 0;
    for(uint i=0;i<genIDcounter;i++)
    {
        char ring_name[GENID_LEN+1];
        rte_memcpy(ring_name,(genID_table+i)->ID,GENID_LEN);
        if(memcmp(generationID,ring_name,GENID_LEN) == 0)
        {
            in_table = 1;
            return rte_ring_lookup(ring_name);
        }
    }
    if(in_table == 0) //GEN_ID not in table, so make new ring for decoded packets.
    {
        //Add genID to table.
        rte_memcpy(genID_table[genIDcounter].ID,generationID,GENID_LEN);
        
        //Create decoder queue for newly received generationID  
        char ring_name[GENID_LEN+1];
        sprintf(ring_name,"%s",genID_table[genIDcounter].ID);
        struct rte_ring *new_ring = rte_ring_create((const char *)ring_name,MAX_SYMBOLS,SOCKET_ID_ANY,0);

        if(new_ring!=NULL) //If ring created sucessfully.
        {
            printf("GEN TABLE UPDATED:\n");
            for(uint i=0;i<=genIDcounter;i++)
            {
                for(uint j = 0;j<GENID_LEN;j++)
                {
                    printf("%02X ",genID_table[i].ID[j]);
                }
                printf("\n");
            }

            genIDcounter++;
        }
        return new_ring;
    }
    return NULL;
}

static void
net_encode(kodoc_factory_t *encoder_factory)
{
    clock_t t_encoder_overall; //Time benchmark for overall encoding process.
    clock_t t_encoder_coder; //Time benchmark for RLNC coding of encoding process.
    clock_t t_encoder_load; //Time benchmark to load data into and out of the encoder.

    //Loop through each dst_addr in the MAC table and check if the ring is full. If the ring is full then begin encoding on that queue.
    for(uint i=0;i<=mac_counter;i++)
    {
        char ring_name[30];
        sprintf(ring_name,"encoding_ring%d",i);

        struct rte_ring* encode_ring_ptr = rte_ring_lookup(ring_name);

        if(encode_ring_ptr!=NULL)
        {
            if(rte_ring_count(encode_ring_ptr)==MAX_SYMBOLS-1) //Check if ring is fulled, if so, begin encoding. Also need to add if the time limit is reached as an OR.
            {
                //BENCHMARK
                t_encoder_overall = clock();

                //Begin encoding on rings.
                uint* obj_left = 0;
                //rte_mbuf to hold the dequeued data.
                struct rte_mbuf *dequeued_data[MAX_SYMBOLS-1];
                rte_pktmbuf_alloc_bulk(codingmbuf_pool,dequeued_data,MAX_SYMBOLS);
                rte_ring_dequeue_bulk(encode_ring_ptr,(void **)dequeued_data,MAX_SYMBOLS-1,obj_left);
                if(rte_ring_count(encode_ring_ptr)==0) //Checks if dequeued correctly.
                {
                    kodoc_coder_t encoder = kodoc_factory_build_coder(*encoder_factory);

                    kodoc_set_systematic_off(encoder);

                    uint32_t block_size = kodoc_block_size(encoder);

                    //Create Mbuf for data_in and payload.
                    struct rte_mbuf* rte_mbuf_data_in = rte_pktmbuf_alloc(codingmbuf_pool);

                    uint8_t* data_in = rte_pktmbuf_mtod(rte_mbuf_data_in, void *);

                    //Process data to be used by encoder.
                    for(uint pkt=0;pkt<MAX_SYMBOLS-1;pkt++)
                    {
                        //Get recieved packet
                        struct rte_mbuf *m = dequeued_data[pkt];
                        const unsigned char* data = rte_pktmbuf_mtod(m, void *); //Convert data to char.

                        //Fill data_in with data.
                        for(uint j=0;j<rte_pktmbuf_data_len(m);j++)
                        {
                            data_in[((MAX_SYMBOL_SIZE)*pkt)+j] = data[j+(2*ETHER_ADDR_LEN)]; //Data starts at 12th byte position, after src and dst. Include eth_type.
                        }
                    }

                    //Assign data buffer to encoder
                    kodoc_set_const_symbols(encoder, data_in, block_size);

                    //Create generationID 
                    char genID[GENID_LEN];
                    int genChar;
                    for(genChar=0;genChar<GENID_LEN;genChar++)
                    {
                        genID[genChar] = 'A' + (random() % 26);
                    }
                
                    struct ether_addr d_addr, s_addr;

                    //BENCHMARK
                    t_encoder_coder = clock();

                    //Loop through each packet in the queue.
                    for(uint pkt=0;pkt<MAX_SYMBOLS-1;pkt++)
                    {                        
                        //Get recieved packet
                        struct rte_mbuf *m = dequeued_data[pkt];
                        const unsigned char* data = rte_pktmbuf_mtod(m, void *); //Convert data to char.
                        //Get ethernet dst and src
                        rte_memcpy(d_addr.addr_bytes,data,ETHER_ADDR_LEN);
                        rte_memcpy(s_addr.addr_bytes,data+ETHER_ADDR_LEN,ETHER_ADDR_LEN);
                        //Allocate payload buffer
                        struct rte_mbuf* rte_mbuf_payload = rte_pktmbuf_alloc(codingmbuf_pool);
                        uint8_t* payload = rte_pktmbuf_mtod(rte_mbuf_payload, void *);

                        //Writes a symbol to the payload buffer.
                        int bytes_used = kodoc_write_payload(encoder, payload);
                        printf("EncodedPkt Generated: rank:%d bytes_used:%d\n", kodoc_rank(encoder), bytes_used);

                        //Create mbuf for encoded reply
                        struct rte_mbuf* encoded_mbuf = rte_pktmbuf_alloc(l2fwd_pktmbuf_pool);
                        char* encoded_data = rte_pktmbuf_append(encoded_mbuf,rte_pktmbuf_data_len(m)+GENID_LEN+10);
                        struct ether_hdr eth_hdr = {    
                            d_addr, //Same as incoming source addr.
                            s_addr, //Port mac address
                            0x2020 //My custom NC Ether type?
                        };
                        encoded_data = rte_memcpy(encoded_data,&eth_hdr,ETHER_HDR_LEN); //Add eth_hdr
                        encoded_data = rte_memcpy(encoded_data+ETHER_HDR_LEN,genID,GENID_LEN); //Add generationID
                        encoded_data = rte_memcpy(encoded_data+GENID_LEN,payload,kodoc_payload_size(encoder)); //Add payload

                        //Temp print encoded packet
                        rte_pktmbuf_dump(stdout,m,250);
                        rte_pktmbuf_dump(stdout,encoded_mbuf,250);
                        printf("\n\n");

                        /* Transmit coded packet */
                        struct rte_mbuf* tx_mbuf = encoded_mbuf;
                        //printf("%d Item added to encoding_tx_ring\n",rte_ring_enqueue_bulk(encoding_tx_ring,(void*)tx_mbuf,1,NULL));
                        const uint16_t tx_pkt = rte_eth_tx_burst(0,0,&tx_mbuf,1);

                        rte_pktmbuf_free(rte_mbuf_payload);
                        rte_pktmbuf_free(encoded_mbuf);
                    }

                    //BENCHMARK end.
                    t_encoder_coder = clock() - t_encoder_coder;
                    printf("t_encoder_coder: %f\n",((double)t_encoder_coder)/CLOCKS_PER_SEC);

                    rte_pktmbuf_free(rte_mbuf_data_in);
                    kodoc_delete_coder(encoder);

                    //BENCHMARK end.
                    t_encoder_overall = clock() - t_encoder_overall;
                    printf("t_encoder_overall: %f\n",((double)t_encoder_overall)/CLOCKS_PER_SEC);

                }
                else
                {
                    printf("Encoding error occured. Packets not encoded due to queue being empty.\n");
                }

                rte_ring_free(encode_ring_ptr);
            }
        }
    } 
}

static void
net_decode(kodoc_factory_t *decoder_factory)
{
    //Loop through each decoding ring and check if the ring has atleast one object. 
    for(uint i=0;i<genIDcounter;i++)
    {
        char ring_id[GENID_LEN+1];
        rte_memcpy(ring_id,(genID_table+i)->ID,sizeof((genID_table+i)->ID));
        struct rte_ring* decoding_ring = rte_ring_lookup(ring_id);

        if(decoding_ring!=NULL)
        {
            printf("In Ring %s Count:%d\n",decoding_ring->name,rte_ring_count(decoding_ring));
            if(rte_ring_count(decoding_ring)>=MAX_SYMBOLS-1) //Check if ring is fulled, if so, begin decoding. Also need to add if the time limit is reached as an OR.
            {
                //Begin decoding on rings.
                uint* obj_left = 0;
                //rte_mbuf to hold the dequeued data.
                struct rte_mbuf *dequeued_data[MAX_SYMBOLS];
                int obj_dequeued = rte_ring_mc_dequeue_bulk(decoding_ring,(void **)dequeued_data,MAX_SYMBOLS-1,obj_left);

                if(obj_dequeued>=(int)MAX_SYMBOLS-1)
                {
                    printf("Decoding..\n");
                    //Create decoder
                    kodoc_coder_t decoder = kodoc_factory_build_coder(*decoder_factory);
                    //Create Data buffers
                    uint32_t block_size = kodoc_block_size(decoder);

                    struct rte_mbuf* rte_mbuf_data_out = rte_pktmbuf_alloc(codingmbuf_pool);

                    uint8_t* data_out = rte_pktmbuf_mtod(rte_mbuf_data_out, void *);

                    //Specifies the data buffer where the decoder will store the decoded symbol.
                    kodoc_set_mutable_symbols(decoder , data_out, block_size);

                    //Loop through each packet in the queue. In the future, it would be better to encode as a group using pointers instead.
                    uint pkt=0;
                    while (!kodoc_is_complete(decoder))
                    {
                        //Get recieved packet
                        if(pkt>=(MAX_SYMBOLS-1))
                        {
                            rte_ring_free(decoding_ring);
                            //Remove generationID from table.
                            for(uint genIndex=i;genIndex<genIDcounter;genIndex++)//i is the position to remove the genID at.
                            {
                                genID_table[genIndex] = genID_table[genIndex+1]; 
                            }
                            memset(&genID_table[i], 0, sizeof(struct generationID));
                            genIDcounter--;
                            rte_pktmbuf_free(rte_mbuf_data_out);
                            kodoc_delete_coder(decoder);
                            printf("GENID removed\n");
                            return;
                        }
                        struct rte_mbuf *m = dequeued_data[pkt];
                        const unsigned char* data = rte_pktmbuf_mtod(m, void *); //Convert data to char.

                        struct rte_mbuf* rte_mbuf_payload = rte_pktmbuf_alloc(codingmbuf_pool);
                        uint8_t* payload = rte_pktmbuf_mtod(rte_mbuf_payload, void *);

                        int rank  = kodoc_rank(decoder);

                        //Fill payload to decode, with data.
                        for(uint j=0;j<rte_pktmbuf_data_len(m);j++)
                        {
                            payload[j] = (uint8_t)data[j+ETHER_HDR_LEN+GENID_LEN]; //Data starts at 14th byte position, after src and dst. Exclude eth_type (Which will be NC type).
                        }

                        //Pass payload to decoder
                        kodoc_read_payload(decoder,payload);

                        //Decoder rank indicates how many symbols have been decoded.
                        printf("Payload processed by decoder, current rank = %d\n", rank);
                        rte_pktmbuf_free(rte_mbuf_payload);

                        pkt++;
                    }

                    uint8_t* pkt_ptr = data_out;
                    for(uint pkt=0;pkt<MAX_SYMBOLS-1;pkt++) //Get decoded packets from data_out and send those out again. Resultant packets are systematic.
                    {
                        //Stores each packet from data_out
                        //Get recieved packet
                        struct rte_mbuf *m = dequeued_data[pkt];
                        const unsigned char* data = rte_pktmbuf_mtod(m, void *); //Convert data to char.
                        
                        //Get ethernet dst and src
                        struct ether_addr d_addr;
                        rte_memcpy(d_addr.addr_bytes,data,ETHER_ADDR_LEN);
                        struct ether_addr s_addr;
                        rte_memcpy(s_addr.addr_bytes,data+ETHER_ADDR_LEN,ETHER_ADDR_LEN);

                        //Create mbuf for decoded reply
                        struct rte_mbuf* decoded_mbuf = rte_pktmbuf_alloc(codingmbuf_pool);
                        char* decoded_data = rte_pktmbuf_append(decoded_mbuf,rte_pktmbuf_data_len(m)-GENID_LEN-10);
                        struct ether_hdr eth_hdr = {
                            d_addr, //Same as incoming source addr.
                            s_addr, //Port mac address
                            (pkt_ptr[0] | ((pkt_ptr[1]) << 8)) //Ether_type from decoded packet.
                        };  
                        
                        decoded_data = rte_memcpy(decoded_data,&eth_hdr,ETHER_HDR_LEN);
                        decoded_data = rte_memcpy(decoded_data+ETHER_HDR_LEN-ETHER_TYPE_LEN,pkt_ptr,MAX_SYMBOL_SIZE); //Original ether header must be overwritten else it is writen twice. Allows using std ether_hdr struct.
                                                
                        /* Transmit decoded mbuf */
                        struct rte_mbuf* tx_mbuf = decoded_mbuf;
                        /*Print packet in and decoded packet*/
                        rte_pktmbuf_dump(stdout,m,250);
                        rte_pktmbuf_dump(stdout,tx_mbuf,250);
                        const uint16_t tx_pkt = rte_eth_tx_burst(0,0,&tx_mbuf,1);

                        rte_pktmbuf_free(decoded_mbuf);

                        /* Advance pointer to next decoded data of packet in data_out. */
                        pkt_ptr += MAX_SYMBOL_SIZE;
                    }

                    rte_pktmbuf_free(rte_mbuf_data_out);

                    //Remove generationID from table.
                    for(uint genIndex=i;genIndex<genIDcounter;genIndex++)//i is the position to remove the genID at.
                    {
                        genID_table[genIndex] = genID_table[genIndex+1]; 
                    }
                    memset(&genID_table[i], 0, sizeof(struct generationID));
                    genIDcounter--;

                    printf("GENID Deleted: GEN TABLE UPDATED:\n");
                    for(uint genIndex=0;genIndex<=genIDcounter;genIndex++)
                    {
                        char ring_name[GENID_LEN+1];
                        rte_memcpy(ring_name,(genID_table+genIndex)->ID,GENID_LEN);
                        printf("%s:", ring_name);
                        for(uint j = 0;j<GENID_LEN;j++)
                        {
                            printf("%02X ",genID_table[genIndex].ID[j]);
                        }
                        printf("\n");
                    }

                    //Free decoder.
                    kodoc_delete_coder(decoder);
                }
                rte_ring_free(decoding_ring);
            }   
        }
    }
}

/*
 * Application main function - loops through
 * receiving and processing packets. Never returns
 */
int
main(int argc, char *argv[])
{
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval = 0;
    struct rte_mbuf *pkts[MAX_PKT_BURST];
    struct rte_eth_link link;

    mac_fwd_table = calloc(MAC_ENTRIES,MAC_ENTRIES * sizeof(struct mac_table_entry));
    genID_table = (struct generationID*)calloc(MAC_ENTRIES,MAC_ENTRIES * sizeof(struct generationID));

    if ((retval = rte_eal_init(argc, argv)) < 0) {
        return -1;
    }

    argc -= retval;
    argv += retval;

    if (parse_app_args(argc, argv) < 0) {
        rte_exit(EXIT_FAILURE, "Invalid command-line arguments\n");
    }

    /* create the mbuf pool for packets going out. */
    l2fwd_pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NB_MBUF,MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,rte_socket_id());

    /* create the mbuf pool for data_in and payload for encoder. And for decoder. */
    codingmbuf_pool = rte_pktmbuf_pool_create("coding_pool",NB_MBUF,MEMPOOL_CACHE_SIZE,0,MAX_SYMBOLS*MAX_SYMBOL_SIZE,SOCKET_ID_ANY);

    /* Network coding factories */
    kodoc_factory_t encoder_factory = kodoc_new_encoder_factory(codec,finite_field,MAX_SYMBOLS-1,MAX_SYMBOL_SIZE);
    kodoc_factory_t decoder_factory = kodoc_new_decoder_factory(codec,finite_field,MAX_SYMBOLS-1,MAX_SYMBOL_SIZE);

    RTE_LOG(INFO, APP, "Finished Process Init.\n");

    printf("[Press Ctrl-C to quit ...]\n");

    /* Configure Coding Port */
    uint16_t portid = 0;
    /* Check if valid port */
    retval = rte_eth_dev_is_valid_port(portid);
    if (retval != 1)
        rte_exit(EXIT_FAILURE, "Port is out of range | not attached: err=%d, port=%u\n",
              retval, portid);
    /* init port */
    printf("Initializing port %u... ", portid);
    retval= rte_eth_dev_configure(portid, 1, 1, &port_conf);
    if (retval< 0)
        rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
              retval, portid);
    printf("dev configure: %X\n", retval );

    /* Adjust number of rx/tx descriptors */
    retval= rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd,
                           &nb_txd);
    if (retval< 0)
        rte_exit(EXIT_FAILURE,
             "Cannot adjust number of descriptors: err=%d, port=%u\n",
             retval, portid);
    
    /* init one RX queue on each port */
    retval= rte_eth_rx_queue_setup(portid, 0, nb_rxd,
                     rte_eth_dev_socket_id(portid),
                     NULL,
                     l2fwd_pktmbuf_pool);
    if (retval< 0)
        rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
              retval, portid);
    printf("Hexo rx q setup: %X\n", retval );
    
    /* init one TX queue on each port */
    retval= rte_eth_tx_queue_setup(portid, 0, nb_txd,
            rte_eth_dev_socket_id(portid),
            NULL);
    if (retval< 0)
        rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
            retval, portid);
    printf("Hexo tx q setup: %X\n", retval );
        
    /* Start device */
    retval = rte_eth_dev_start(portid);
    if (retval < 0)
        rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
              retval, portid);
    printf("Hexo dev start: %X\n", retval );

    /* Enable promiscuous mode */
    rte_eth_promiscuous_enable(portid);
    retval = rte_eth_promiscuous_get(portid);
    if (retval < 0)
        rte_exit(EXIT_FAILURE, "rte_eth_promiscuous_enable:err=%d, port=%u\n",
              retval, portid);

    /* Check link status */
    memset(&link, 0, sizeof(link));
    rte_eth_link_get_nowait(portid, &link);
    printf("Port%d. Speed %u Mbps - %s. Link %s \n",portid, link.link_speed,(link.link_duplex == ETH_LINK_FULL_DUPLEX) ? ("full-duplex") : ("half-duplex"),(link.link_status == 1) ? ("Up") : ("Down"));
    
    /* Display the port MAC address. */
    struct ether_addr addr;
    rte_eth_macaddr_get(portid, &addr);
    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                       " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
                    portid,
                    addr.addr_bytes[0], addr.addr_bytes[1],
                    addr.addr_bytes[2], addr.addr_bytes[3],
                    addr.addr_bytes[4], addr.addr_bytes[5]);

    force_quit = false;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (!force_quit)
    {
        /* Get packets for Encoding */
        unsigned nb_rx = rte_eth_rx_burst(portid, 0,
                                 pkts, MAX_PKT_BURST);

        if(nb_rx > 0) 
        {
            for(uint pkt=0;pkt<nb_rx;pkt++)
            {
                if(coder_idx==-1)
                {
                    struct rte_mbuf *m = pkts[pkt];
                    struct rte_mbuf *tx_mbuf = m;
                    rte_pktmbuf_dump(stdout,m,250);
                    const uint16_t tx_pkt = rte_eth_tx_burst(0,0,&tx_mbuf,1);
                    if(tx_pkt ==1)
                    {
                        printf("Packet added to tx queue.\n");
                    }

                }
                if(coder_idx==0)
                {
                    int index = 0;
                    struct rte_mbuf *m = pkts[pkt];
                    //rte_pktmbuf_dump(stdout,m,100);

                    /* Get recieved packet */
                    const unsigned char* data = rte_pktmbuf_mtod(m, void *);
                    /* Get ethernet dst and src */
                    struct ether_addr d_addr;
                    rte_memcpy(d_addr.addr_bytes,data,ETHER_ADDR_LEN);
                    struct ether_addr s_addr;
                    rte_memcpy(s_addr.addr_bytes,data+ETHER_ADDR_LEN,ETHER_ADDR_LEN);

                    int mac_add = 0;
                    for (int i=0;i<MAC_ENTRIES;i++)
                    {
                        /* Check if table contains src address. */
                        if(memcmp(mac_fwd_table[i].d_addr.addr_bytes,s_addr.addr_bytes,sizeof(s_addr.addr_bytes)) == 0) 
                        {
                            /* Dont add mac address as it is already in the table. */
                            mac_add = 1;
                            index = i;
                        }
                    }
                    if(unlikely(mac_add==0))
                    {
                        mac_fwd_table[mac_counter].d_addr = s_addr;

                        /* Create rte_ring for encoding queue, for each new MAC entry. */
                        char ring_name[30];
                        sprintf(ring_name,"encoding_ring%d",mac_counter);
                        encoding_rings[mac_counter] = *rte_ring_create((const char *)ring_name,MAX_SYMBOLS,-1,0);

                        index = mac_counter;

                        mac_counter++;
                    
                        /* Print updated MAC table to Console. */
                        printf("\nUpdated MAC TABLE.\nD_ADDR\n");
                        for(uint i=0;i<mac_counter;i++)
                        {
                            if(mac_fwd_table[i].d_addr.addr_bytes[0] != 0)
                            {
                                for (uint j = 0; j < sizeof(mac_fwd_table[i].d_addr.addr_bytes); ++j)
                                {
                                    printf("%02x:", mac_fwd_table[i].d_addr.addr_bytes[j]);
                                }
                                printf("\n");
                            }
                        }
                    }

                    /* Add packet to encoding_ring */
                    char ring_name[30];
                    sprintf(ring_name,"encoding_ring%d",index);
                    struct rte_ring* encode_ring_ptr = rte_ring_lookup(ring_name);
                    printf("Index %d RingCount %d\n", index, rte_ring_count(encode_ring_ptr));
                    rte_ring_enqueue(encode_ring_ptr,(void *)m);

                    /* Reset encoding_ring flag */
                    int rst_encode = 0;
                    if(rte_ring_count(encode_ring_ptr)==MAX_SYMBOLS-1)
                    {
                        rst_encode = 1;
                    }

                    /* Run encoder function. */
                    net_encode(&encoder_factory);
                    printf("Pkt added to encoder.\n");

                    /* If rst_encode set: reset encoding ring */
                    if(unlikely(rst_encode == 1))
                    {
                        rte_ring_free(rte_ring_lookup(ring_name));
                        encoding_rings[mac_counter-1] = *rte_ring_create((const char *)ring_name,MAX_SYMBOLS,-1,0);
                    }
                }
                else if(coder_idx==1)
                {
                    printf("Decode\n");

                    struct rte_mbuf *m = pkts[pkt];

                    rte_pktmbuf_dump(stdout,m,250);

                    // Get recieved packet
                    const unsigned char* data = rte_pktmbuf_mtod(m, void *);
                    // Get ethernet dst and src 
                    struct ether_addr d_addr;
                    rte_memcpy(d_addr.addr_bytes,data,ETHER_ADDR_LEN);
                    struct ether_addr s_addr;
                    rte_memcpy(s_addr.addr_bytes,data+ETHER_ADDR_LEN,ETHER_ADDR_LEN);

                    //Get genID from encoded packet.
                    char genID[GENID_LEN];
                    rte_memcpy(genID,&data[14],GENID_LEN);

                    //Check if GENID is valid
                    for(uint genchar=0;genchar<GENID_LEN;genchar++)
                    {
                        if(genID[genchar]==0) //Drop packet.
                        {
                            printf("Invalid Encoded Packet.\n");
                            break;
                        }
                    }

                    //Check if genID is in genTable
                    struct rte_ring* decode_ring_ptr = genID_in_genTable(genID);
                    if(decode_ring_ptr!=NULL)
                    {
                        //Add packet to decoding ring
                        rte_ring_enqueue(decode_ring_ptr,(void *)m);
                        //Call decoder
                        net_decode(&decoder_factory);
                    }
                    else
                    {
                        printf("Decoding Ring does not exist.\n");
                    }
                }
                else if(coder_idx==2)
                {
                    struct rte_mbuf *m = pkts[pkt];
                    rte_pktmbuf_dump(stdout,m,250);
                }
            }
        }
    }

    //Cleanup after network coding
    kodoc_delete_factory(encoder_factory);
    kodoc_delete_factory(decoder_factory);
}
