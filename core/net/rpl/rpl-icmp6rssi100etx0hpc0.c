/**
 * \addtogroup uip6
 * @{
 */
/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */
/**
 * \file
 *         ICMP6 I/O for RPL control messages.
 *
 * \author Joakim Eriksson <joakime@sics.se>, Nicolas Tsiftes <nvt@sics.se>
 * Contributors: Niclas Finne <nfi@sics.se>, Joel Hoglund <joel@sics.se>,
 *               Mathieu Pouillot <m.pouillot@watteco.com>
 */

#include "net/tcpip.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/uip-nd6.h"
#include "net/uip-icmp6.h"
#include "net/rpl/rpl-private.h"
#include "net/packetbuf.h"

#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <string.h>


#if UIP_CONF_IPV6
/*---------------------------------------------------------------------------*/
#define RPL_DIO_GROUNDED                 0x80
#define RPL_DIO_MOP_SHIFT                3
#define RPL_DIO_MOP_MASK                 0x3c
#define RPL_DIO_PREFERENCE_MASK          0x07

#define UIP_IP_BUF       ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_ICMP_BUF     ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define UIP_ICMP_PAYLOAD ((unsigned char *)&uip_buf[uip_l2_l3_icmp_hdr_len])
/*---------------------------------------------------------------------------*/
static void dis_input(void);
static void dio_input(void);
static void dao_input(void);
static void dao_ack_input(void);

/* some debug callbacks useful when debugging RPL networks */
#ifdef RPL_DEBUG_DIO_INPUT
void RPL_DEBUG_DIO_INPUT(uip_ipaddr_t *, rpl_dio_t *);
#endif

#ifdef RPL_DEBUG_DAO_OUTPUT
void RPL_DEBUG_DAO_OUTPUT(rpl_parent_t *);
#endif

static uint8_t dao_sequence = RPL_LOLLIPOP_INIT;

extern rpl_of_t RPL_OF;
/*fuzzy parameters*/

int outputindex = 0;

/*Array to save minimum values of the fired rules*/
int fuzzy_quality_mf[27];

/*Array to hold the peak value of output fuzzy set*/
int peak_value_ouput_mf[27];

/*Stores final crisp fuzzy output after defuzzification*/
uint8_t fuzzy_outputs;

/*Variable declaration for variables used to calualate minimum value in the minimum function*/
int dom_rssi;
int dom_etx; 
int dom_hpc;
/*fuzzy function declarations*/
int dom_connected(int frssi);
int dom_trans(int frssi);
int dom_disc(int frssi);
int dom_small(unsigned f_etx);
int dom_ave(unsigned f_etx);
int dom_large(unsigned f_etx);
int dom_near(unsigned f_rank);
int dom_far(unsigned f_rank);
int dom_very_far(unsigned f_rank);

void fuzzification_rssi();
void fuzzification_etx();
void fuzzification_hop_count();
int fuzzy_defuzzification(int fuzzy_quality_mf[], int p_value[], int outputindex);
int min(int dom_rssi, int dom_etx, int dom_hpc);


rpl_instance_t *process_instance;
/*
 * dis_rssi -> RSSI reading from DIS
 * dis_number -> DIS counter value
 * rssi_average -> store final value from the calculated RSSI average
 */
static uint8_t dis_rssi, dis_number, rssi_average;

/* Store possible parents info, gathered in discovery phase. */
static uint16_t possible_parent_rssi[5];
static uip_ipaddr_t possible_parent_addr[5];

/* Aid in storage of the above info. */
uint16_t best_parent_rssi;
uip_ipaddr_t best_parent_addr;
rpl_dio_t best_parent_dio;
rpl_parent_t *p;

/*
 * j -> used to cycle through the array of DIOs received in discovery phase
 * true_rssi -> used to aid on calculation of true_rssi_average
 * true_rssi_average -> real measurable RSSI average
 */
static int number_of_dios, true_rssi, true_rssi_average;

/* Store DIOs gathered in discovery phase. */
rpl_dio_t dios[5];

/* Assessing parent. Used to store address from child who sent DIS, to reply with DIO. */
uip_ipaddr_t *dio_addr;

/* Flag used distinguish when the DIS reception process has started. */
char process_dis_input = 0;

/* Self explanatory variables. */
PROCESS(multiple_dis_input, "Sliding DIS input");
PROCESS(wait_dios, "Multiple DIO input");
process_event_t dis_event;
process_event_t wait_dios_event;

/* Self-scalable timer on burst of DIS reception*/
static struct etimer dis_delay;

/* Priority assigned to each DIO*/
char priority;
/*
 * Timer used to delimit reception of DIOs in Discovery Phase.
 * After which, parent comparison will start.
 */
static struct etimer dios_input;

/* DAO delay upon best parent DIO processing */
struct ctimer dao_period;

/*Structure for membership functions(fuzzy sets) for the all inputs. 
The struct members holds the degree of memberships(dom) of each input value*/
struct mf_ave_Rssi{
int connected;
int disc;
int trans;
}mf_ave_rssi;

struct mf_Etx{
int  small;
int  average;
int  large;
}mf_etx;

struct mf_Hpc{
int  near;
int  far;
int  very_far;
}mf_hpc;

/*Fuzzification functions for the 3 inputs.The functions takes ave.rssi, etx and rank as input parameters*/
void fuzzification_rssi()
{
mf_ave_rssi.connected =  dom_connected(true_rssi_average);
mf_ave_rssi.trans	=  dom_trans(true_rssi_average);
mf_ave_rssi.disc = dom_disc(true_rssi_average);

}

void fuzzification_etx()
{
mf_etx.small =  dom_small(dio.mc.obj.etx);
mf_etx.average = dom_ave(dio.mc.obj.etx);
mf_etx.large = dom_large(dio.mc.obj.etx);

}

void fuzzification_hop_count()
{
mf_hpc.near =  dom_near(dio.rank);
mf_hpc.far = dom_far(dio.rank);
mf_hpc.very_far = dom_very_far(dio.rank);

}
/*Function definition for the defuzzification process*/
int fuzzy_defuzzification(int fuzzy_quality_mf[], int p_value[], int outputindex)
{
  int i;
  int aggregate_dom = 0;
  int peak_and_dom_multiplication  = 0;
    
   for(i = 0; i <= outputindex; i++ )
   {
   peak_and_dom_multiplication = peak_and_dom_multiplication + p_value[i] * fuzzy_quality_mf[i]/floatPrecession;
   aggregate_dom = aggregate_dom + fuzzy_quality_mf[i]/floatPrecession;
  
   } 
    return  (peak_and_dom_multiplication/aggregate_dom) ;
   
}
/*Definition of the minimum function to find smallest value during rule evaluation */
int min(int dom_rssi, int dom_etx, int dom_hpc)
{
int smallest;
if((dom_rssi <= dom_etx) && (dom_rssi <=dom_hpc))
	{
	    smallest = dom_rssi;
	}
else if((dom_etx <= dom_hpc) && (dom_etx <= dom_rssi))
	{
	    smallest = dom_etx;
	}
else
	{
	    smallest = dom_hpc;
	}
return smallest;

}


int dom_connected(int true_rssi_average)
{
        int numerator;
        int denominator;
	if(true_rssi_average >= -75)
	{
		return 1 * floatPrecession;
	}
	else if(true_rssi_average < -75 && true_rssi_average > -80)
	{ 
	   numerator = true_rssi_average + 80;
           denominator = -75 + 80;	
           return (numerator * floatPrecession ) / (denominator) ;
	}
	else if(true_rssi_average <= -80)
	{
		return 0 * floatPrecession;
	}
return 0;      
}

int dom_trans(int true_rssi_average)
{
       	int numerator;
        int denominator;
        if(true_rssi_average >= -75)
	{
		return 0 * floatPrecession;
	}
	else if(true_rssi_average < -75 && true_rssi_average > -80)
	{ 
            numerator = true_rssi_average + 75 ;
            denominator = -80 + 75;
            return ((numerator*floatPrecession)/denominator) ; 
        }
	else if(true_rssi_average <= -80 && true_rssi_average >= -85)
	{
		return 1 * floatPrecession;
	}
        else if(true_rssi_average < -85 && true_rssi_average > -90)
	{ 
		numerator = true_rssi_average + 90 ;
                denominator = -80 + 90;
                return ((numerator*floatPrecession) / denominator) ;
         }
        else if(true_rssi_average <= -90)
	{
		return 0 * floatPrecession;
	}
 return 0;       
}

int dom_disc(int true_rssi_average)
{
        int numerator;
        int denominator; 
	if(true_rssi_average <= -90)
	{
		return 1 * floatPrecession;
	}
	else if(true_rssi_average < -85 && true_rssi_average > -90)
	{ 
		
            numerator = true_rssi_average + 85;
            denominator = -90 + 85;
            return ((numerator*floatPrecession) / denominator) ;
	}
	else if(true_rssi_average >= -85)
	{
		return 0 * floatPrecession;
	}
return 0;        
}

int dom_small(unsigned f_etx)
{ 
        int numerator;
        int denominator;
        unsigned fuzzy_path_etx;

        fuzzy_path_etx = dio.mc.obj.etx/RPL_DAG_MC_ETX_DIVISOR,
        (dio.mc.obj.etx % RPL_DAG_MC_ETX_DIVISOR * 100) / RPL_DAG_MC_ETX_DIVISOR; 

	if( fuzzy_path_etx  <= 10)
	{
		return 1 * floatPrecession;  
	}
	else if(fuzzy_path_etx > 10 && fuzzy_path_etx < 30)
	{ 
             numerator = fuzzy_path_etx - 30;
             denominator = 10 - 30;
	     return ((numerator * floatPrecession) / denominator) ;
        }
	else if(fuzzy_path_etx >= 30)
	{
		return 0 * floatPrecession;
	}
return 0;        
}

int dom_ave(unsigned f_etx)
{
        int numerator;
        int denominator;

        unsigned fuzzy_path_etx;

        fuzzy_path_etx = dio.mc.obj.etx/RPL_DAG_MC_ETX_DIVISOR,
        (dio.mc.obj.etx % RPL_DAG_MC_ETX_DIVISOR * 100) ; 
	if(fuzzy_path_etx <= 10)
	{
		return 0 * floatPrecession;
	}
	else if(fuzzy_path_etx > 10 && fuzzy_path_etx < 30)
	{       
             numerator = fuzzy_path_etx - 10;
             denominator = 30 - 10;
	    return ((numerator*floatPrecession) / denominator) ;
        }
	else if(fuzzy_path_etx >= 30 && fuzzy_path_etx <= 60)
	{
		return 1 * floatPrecession;
	}
        else if(fuzzy_path_etx > 60 && fuzzy_path_etx < 80)
	{ 
	   numerator = fuzzy_path_etx -80;
           denominator = 60 - 80;
	  return ((numerator*floatPrecession) / denominator) ;	
        }
        else if(fuzzy_path_etx >= 80)
	{
		return 0 * floatPrecession;
	}
return 0;        
}

int dom_large(unsigned f_etx)
{
        int numerator;
        int denominator;
        unsigned fuzzy_path_etx;

        fuzzy_path_etx = dio.mc.obj.etx/RPL_DAG_MC_ETX_DIVISOR,
        (dio.mc.obj.etx % RPL_DAG_MC_ETX_DIVISOR * 100) / RPL_DAG_MC_ETX_DIVISOR;
 
	if(fuzzy_path_etx <= 80)
	{
		return 0 * floatPrecession;
	}
	else if(fuzzy_path_etx > 60 && fuzzy_path_etx < 80)
	{ 
                numerator = fuzzy_path_etx - 60;
                denominator = 80 - 60;
	        return ((numerator*floatPrecession) / denominator) ;
	}
	else if(fuzzy_path_etx >= 80)
	{
		return 1 * floatPrecession;
	}
 return 0;      
}

int dom_near(unsigned f_rank)
{
        int numerator;
        int denominator; 
	if(f_rank/256 <= 2)
	{
		return 1 * floatPrecession;
	}
	else if(f_rank/256 > 2 && f_rank/256 < 3)
	{ 
               numerator = f_rank/256 - 3;
               denominator = 2 - 3;
	       return ((numerator*floatPrecession) / denominator) ;
        }
	else if(f_rank/256 >= 3)
	{
		return 0 * floatPrecession;
	}
 return 0;       
}

int dom_far(unsigned f_rank)
{
        int numerator;
        int denominator;

	if(f_rank/256 <= 2)
	{
		return 0 * floatPrecession;
	}
	else if(f_rank/256 > 2 && f_rank/256 < 3)
	{ 
               numerator = f_rank/256 - 2;
               denominator = 3 - 2;
	       return ((numerator * floatPrecession) / denominator) ;
	}
	else if(f_rank/256 >= 3 && f_rank/256 <= 5)
	{
		return 1 * floatPrecession;
	}
        else if(f_rank/256 > 5 && f_rank/256 < 6)
	{ 
                numerator =f_rank/256 - 6;
                denominator = 5 - 6;
	        return ((numerator * floatPrecession) / denominator) ;
		
	}
        else if(f_rank/256 >= 6)
	{
		return 0 * floatPrecession;
	}
 return 0;       
}

int dom_very_far(unsigned f_rank)
{
        int numerator;
        int denominator;
         
	if(f_rank/256 < 6)
	{
		return 0 * floatPrecession;
	}
	else if(f_rank/256 > 5 && f_rank/256 < 6)
	{ 
                numerator = f_rank/256 - 5;
                denominator = 6 - 5;
	      return ((numerator * floatPrecession) / denominator) ;
	}
	else if(f_rank/256 >= 6)
	{
		return 1 * floatPrecession;
	}
return 0;
        
}


/*---------------------------------------------------------------------------*/
static int get_global_addr(uip_ipaddr_t *addr)
{
	int i;
	int state;

	for (i = 0; i < UIP_DS6_ADDR_NB; i++) {
		state = uip_ds6_if.addr_list[i].state;
		if (uip_ds6_if.addr_list[i].isused
				&& (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
			if (!uip_is_addr_link_local(&uip_ds6_if.addr_list[i].ipaddr)) {
				memcpy(addr, &uip_ds6_if.addr_list[i].ipaddr,
						sizeof(uip_ipaddr_t));
				return 1;
			}
		}
	}
	return 0;
}
/*---------------------------------------------------------------------------*/
static uint32_t get32(uint8_t *buffer, int pos) {
	return (uint32_t) buffer[pos] << 24 | (uint32_t) buffer[pos + 1] << 16
			| (uint32_t) buffer[pos + 2] << 8 | buffer[pos + 3];
}
/*---------------------------------------------------------------------------*/
static void set32(uint8_t *buffer, int pos, uint32_t value) {
	buffer[pos++] = value >> 24;
	buffer[pos++] = (value >> 16) & 0xff;
	buffer[pos++] = (value >> 8) & 0xff;
	buffer[pos++] = value & 0xff;
}
/*---------------------------------------------------------------------------*/
static uint16_t get16(uint8_t *buffer, int pos) {
	return (uint16_t) buffer[pos] << 8 | buffer[pos + 1];
}
/*---------------------------------------------------------------------------*/
static void set16(uint8_t *buffer, int pos, uint16_t value) {
	buffer[pos++] = value >> 8;
	buffer[pos++] = value & 0xff;
}

void start_parent_unreachable(void)
{
	process_post(&unreach_process, PARENT_UNREACHABLE, NULL);
}
/*---------------------------------------------------------------------------*/
static void dis_input(void) {
	rpl_instance_t *instance;
	rpl_instance_t *end;

	unsigned char *buffer;
	rpl_parent_t *p;
	rpl_dag_t *dag;
	uip_ipaddr_t myaddr;
	uip_ipaddr_t *pref;
	rpl_parent_t *p_dis;
	uip_ipaddr_t *dis_addr;
	uint8_t my_ip6id;
	uint8_t send_ip6id;
	int real_rssi;

	dis_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
	instance = &instance_table[0];
	dag = instance->current_dag;
	/* DAG Information Solicitation */
	/*PRINTF("RPL: Received a DIS from "); PRINT6ADDR(&UIP_IP_BUF->srcipaddr); PRINTF("\n");*/
	/* Store address from the node who sent the DIS, to reply with DIO afterwards */
	dio_addr = (&UIP_IP_BUF->srcipaddr);
	buffer = UIP_ICMP_PAYLOAD;

	real_rssi = buffer[0] - 45;
	if (buffer[0] > 200) {
		real_rssi = buffer[0] - 255 - 46;
	}

	myaddr = uip_ds6_if.addr_list[2].ipaddr;
	my_ip6id = myaddr.u8[15];
	/*PRINTF("dis_target = %u, , my_id = %u, rssi = %d\n", buffer[2], my_ip6id, real_rssi);*/

#if MOBILE_NODE
	if(((buffer[1] & 0x80) >> 7 == 1) && (my_ip6id == buffer[2])) {
		pref = rpl_get_parent_ipaddr(dag->preferred_parent);
		p_dis = rpl_find_parent(dag,dio_addr);
		dis_addr = rpl_get_parent_ipaddr(p_dis);
	    if(uip_ipaddr_cmp(dis_addr,pref)) {
               //if(real_rssi < -85 && hand_off_backoff_flag == 0 && test_unreachable != 1) {
		if(fuzzy_outputs < 60 && hand_off_backoff_flag == 0 && test_unreachable != 1) {      //note fuzzy_out value used for hand_off 
				PRINTF("DIS FROM PREFERRED PARENT\n");
				rpl_unreach();
				test_unreachable = 1;
				process_post(&unreach_process, PARENT_UNREACHABLE, NULL);
				return;
			}
		}
	}

#endif

	if (buffer[2] != 0) {
		return;
	}

	for (instance = &instance_table[0], end = instance + RPL_MAX_INSTANCES;
			instance < end; ++instance) {
		if (instance->used == 1) {
			process_instance = instance;
			dag = instance->current_dag;
#if RPL_LEAF_ONLY
			if(!uip_is_addr_mcast(&UIP_IP_BUF->destipaddr)) {
				/*PRINTF("RPL: LEAF ONLY Multicast DIS will NOT reset DIO timer\n");*/
#else /* !RPL_LEAF_ONLY */

			if (uip_is_addr_mcast(&UIP_IP_BUF->destipaddr)) {

				/* Here starts the reception and calculation of average RSSI when a burst of DIS is received. */

				/* Received multicast DIS with flag = 1 */
				if ((buffer[1] & 0x80) >> 7 == 1
						&& ((buffer[1] & 0x60) >> 5) != 0) {
					/* Loop avoidance */
					p = rpl_find_parent(dag, dio_addr);
					if (p != NULL) {
						/*PRINTF("Ignoring a DIO request."); PRINTF("\n");*/
						return;
					}
					/* Get counter */
					dis_number = (buffer[1] & 0x60) >> 5;
					PRINTF("Received DIS number %u\n", dis_number);
					/* RSSI calculation */
					true_rssi = dis_rssi - 45;
					if (dis_rssi > 200) {
						true_rssi = dis_rssi - 255 - 46;
					}
					true_rssi_average += true_rssi;
					/* Start process to receive DISs according to self-scalable timer */
					if (process_dis_input == 0) {
						process_start(&multiple_dis_input, NULL);
						process_dis_input++;
					}
					process_post_synch(&multiple_dis_input, SET_DIS_DELAY,
							NULL);
					return;
				} else {
					/*PRINTF("RPL: Multicast DIS => reset DIO timer\n");*/
					rpl_reset_dio_timer(instance);
				}

			} else {
#endif /* !RPL_LEAF_ONLY */
				if ((buffer[1] && 0x80) >> 7 == 1) {
					/* If a Unicast DIS with flag is received, just reply with a DIO with flag. */
					dio_output(instance, &UIP_IP_BUF->srcipaddr, 1);
					return;
				} else {
					/*PRINTF("RPL: Unicast DIS, reply to sender\n");*/
					dio_output(instance, &UIP_IP_BUF->srcipaddr, 0);
				}
			}
		}
	}
}
/*---------------------------------------------------------------------------*/
void eventhandler2(process_event_t ev, process_data_t data) {
	rpl_instance_t *instance = &instance_table[0];

	switch (ev) {

	/*
	 * Self scalable timer. This event uses the dis_number received
	 * and sets the timer accordingly.
	 */
	case SET_DIS_DELAY: {
		etimer_set(&dis_delay, (3 - dis_number) * CLOCK_SECOND / 50);
	}
		break;

		/*
		 * If all the DISs were received, this function is started to process them.
		 * It will assign a priority to the DIO, according to the fuzzy_outputs instead of rssi_average value
		 * and trigger the DIO with new_dio_interval();
		 */
	case PROCESS_EVENT_TIMER: {
           if (data == &dis_delay && etimer_expired(&dis_delay)) {
              /*PRINTF("true_rssi_average b/4 dividing with dis number = %d\n", true_rssi_average);*/
		true_rssi_average = true_rssi_average / (dis_number);

                //frssi = true_rssi_average; 
              /*PRINTF("true_rssi_average after dividing with dis number = %d\n", true_rssi_average);*/
		rssi_average = 255 + 46 + true_rssi_average;

              /*PRINTF("rssi_average after dividing with dis number = %d\n", rssi_average);*/
		/*call the fuzzification functions to calculate DOM for the input values(rssi,etx,hop_count)*/
          /*Here starts function call for the fuzzy algorithm*/                                                        
		  fuzzification_rssi();

/*PRINTF("fuzzy rssi (connected, trans, disc,aveR) = (%u.%u,%u.%u,%u.%u,%d)\n",mf_ave_rssi.connected/ floatPrecession,
(mf_ave_rssi.connected % floatPrecession * 100) /floatPrecession, 
mf_ave_rssi.trans/ floatPrecession,(mf_ave_rssi.trans % floatPrecession * 100) /floatPrecession,
mf_ave_rssi.disc/ floatPrecession,(mf_ave_rssi.disc % floatPrecession * 100) /floatPrecession,true_rssi_average);*/

PRINTF("fuzzy rssi(connected, trans, disc,aveR)=(%u,%d,%u,%d)\n",mf_ave_rssi.connected,mf_ave_rssi.trans, mf_ave_rssi.disc, true_rssi_average); 
		  fuzzification_etx();
PRINTF("fuzzy etx (small, average,large,etx) = (%u,%u,%u,%u.%u)\n",mf_etx.small, mf_etx.average, mf_etx.large,
dio.mc.obj.etx/RPL_DAG_MC_ETX_DIVISOR,
(dio.mc.obj.etx % RPL_DAG_MC_ETX_DIVISOR * 100) / RPL_DAG_MC_ETX_DIVISOR);
		 fuzzification_hop_count();
PRINTF("fuzzy rank (near, far, very_far,hpc) = (%u,%u,%u,%u)\n",mf_hpc.near,mf_hpc.far, mf_hpc.very_far,dio.rank/256);
                            
 /*Rule Evaluation for the 27 rules in the table, given average rssi, etx and hop-count inputs weight of 100, 0 and 0 percent respectively*/
			if (mf_ave_rssi.connected && mf_etx.small && mf_hpc.near > 0)
                         {
                           fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.small,mf_hpc.near);
                           //fuzzy_quality_mf[outputindex] = min(1,5,1);
                           peak_value_ouput_mf[outputindex] = 100;
			   
/*PRINTF("R1(mf_con,mf_sma,mf_near,fQ,pv,index) = (%u,%u,%u,%u,%u,%u)\n",mf_ave_rssi.connected,mf_etx.small,
mf_hpc.near,fuzzy_quality_mf[outputindex], peak_value_ouput_mf[outputindex],outputindex);*/
			   outputindex++;
                         } 
                        /*else { 
                              PRINTF("R1 not fired\n");
                             } */ 
                         if (mf_ave_rssi.connected && mf_etx.small && mf_hpc.far > 0)
                         {
			   fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.small,mf_hpc.far); 
			   peak_value_ouput_mf[outputindex] = 100;
			   outputindex++;
/*PRINTF("R2(mf_con,mf_sma,mf_far,fQ,pv,index) = (%u,%u,%u,%u,%u,%u)\n",mf_ave_rssi.connected,mf_etx.small,mf_hpc.far,
fuzzy_quality_mf[outputindex], peak_value_ouput_mf[outputindex],outputindex);*/
			 }
                         /*else { 
                               //PRINTF("R2 not fired\n");
                             }*/   
                       
                        if (mf_ave_rssi.connected && mf_etx.average && mf_hpc.near > 0)
                         {
                           fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.average,mf_hpc.near);
                           peak_value_ouput_mf[outputindex] = 100;
                           outputindex++;
/*PRINTF("R3(mf_con,mf_sma,mf_very_far,fQ,pv,index) = (%u,%u,%u,%u,%u,%u)\n",mf_ave_rssi.connected,mf_etx.small,
mf_hpc.very_far,fuzzy_quality_mf[outputindex], peak_value_ouput_mf[outputindex],outputindex);*/
                        }
                       /*else { 
                               //PRINTF("R3 not fired\n");
                             }*/  
                        if (mf_ave_rssi.connected && mf_etx.small && mf_hpc.very_far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.small,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 100;
                            outputindex++;
/*PRINTF("R4(mf_con,mf_ave,mf_near,fQ,pv,index) = (%u,%u,%u,%u,%u,%u)\n",mf_ave_rssi.connected,mf_etx.small,mf_hpc.near,
fuzzy_quality_mf[outputindex], peak_value_ouput_mf[outputindex],outputindex);*/
                         }
                        /*else { 
                               //PRINTF("R4 not fired\n");
                             }*/  
                        if (mf_ave_rssi.connected && mf_etx.average && mf_hpc.far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.average,mf_hpc.far);
                            peak_value_ouput_mf[outputindex] = 100;
                            outputindex++;
/*PRINTF("R5(mf_con,mf_ave,mf_far,fQ,pv,index) = (%u,%u,%u,%u,%u,%u)\n",mf_ave_rssi.connected,mf_etx.small,mf_hpc.near,
fuzzy_quality_mf[outputindex], peak_value_ouput_mf[outputindex],outputindex);*/
                         }
                         /*else { 
                               //PRINTF("R5 not fired\n");
                             }*/  
                        if (mf_ave_rssi.connected && mf_etx.large && mf_hpc.near > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.large,mf_hpc.near);
                            peak_value_ouput_mf[outputindex] = 100;
                            outputindex++;
                         }
                         /*else { 
                               //PRINTF("R6 not fired\n");
                             }*/    
                        if (mf_ave_rssi.connected && mf_etx.average && mf_hpc.very_far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.average,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 100;
                            outputindex++;
                         }
                         /*else { 
                               PRINTF("R7 not fired\n");
                             }*/  
                        if (mf_ave_rssi.connected  && mf_etx.large && mf_hpc.far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.trans,mf_etx.large,mf_hpc.far);
                            peak_value_ouput_mf[outputindex] = 100;
                            outputindex++; 
                         }
                        /* else { 
                               PRINTF("R8 not fired\n");
                             }*/  
                        if (mf_ave_rssi.connected && mf_etx.large && mf_hpc.very_far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.large,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 100;
                            outputindex++; 
                         }
                         /*else { 
                               PRINTF("R9 not fired\n");
                             }*/  
                        if (mf_ave_rssi.trans && mf_etx.small && mf_hpc.near > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.trans,mf_etx.small,mf_hpc.near);
                            peak_value_ouput_mf[outputindex] = 60;
                            outputindex++; 
                         }
                        /*else { 
                               PRINTF("R10 not fired\n");
                             }*/  
                        if (mf_ave_rssi.trans && mf_etx.small && mf_hpc.far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.small,mf_hpc.far);
                            peak_value_ouput_mf[outputindex] = 60;
                            outputindex++; 
                         }
                         /*else { 
                               PRINTF("R11 not fired\n");
                             }*/  
                        if (mf_ave_rssi.trans && mf_etx.average && mf_hpc.near > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.trans,mf_etx.average,mf_hpc.near);
                            peak_value_ouput_mf[outputindex] = 60;
                            outputindex++; 
                         }
                          /*else { 
                               PRINTF("R12 not fired\n");
                             }*/  
                        if (mf_ave_rssi.trans && mf_etx.small && mf_hpc.very_far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.trans,mf_etx.small,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 60;
                            outputindex++;
                         }
                          /*else { 
                               PRINTF("R13 not fired\n");
                             }*/    
                        if (mf_ave_rssi.trans && mf_etx.large && mf_hpc.near > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.trans,mf_etx.large,mf_hpc.near);
                            peak_value_ouput_mf[outputindex] = 60;
                            outputindex++;
                         }
                        /*else { 
                               PRINTF("R14 not fired\n");
                             }*/  
                        if (mf_ave_rssi.trans && mf_etx.average && mf_hpc.far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.trans,mf_etx.average,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 60;
                            outputindex++;
                         }
                         /*else { 
                               PRINTF("R15 not fired\n");
                             }*/  
                        if (mf_ave_rssi.trans && mf_etx.average && mf_hpc.very_far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.connected,mf_etx.average,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 60;
                            outputindex++;
                         }
                        /*else { 
                               PRINTF("R16 not fired\n");
                             }*/  
                        if (mf_ave_rssi.trans && mf_etx.large && mf_hpc.far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.trans,mf_etx.large,mf_hpc.far);
                            peak_value_ouput_mf[outputindex] = 60;
                            outputindex++;
                         }
                         /*else { 
                               PRINTF("R17 not fired\n");
                             }*/  
                        if (mf_ave_rssi.trans && mf_etx.large && mf_hpc.very_far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.trans,mf_etx.large,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 60;
                            outputindex++;
                         }
                         /*else { 
                               PRINTF("R18 not fired\n");
                             }*/  
                        if (mf_ave_rssi.disc && mf_etx.small && mf_hpc.near > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.disc,mf_etx.small,mf_hpc.far);
                            peak_value_ouput_mf[outputindex] = 30;
                            outputindex++;
                         }
                         /*else { 
                               PRINTF("R19 not fired\n");
                             }*/  
                        if (mf_ave_rssi.disc && mf_etx.small && mf_hpc.far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.disc,mf_etx.small,mf_hpc.far);
                            peak_value_ouput_mf[outputindex] = 30;
                            outputindex++;
                         }
                         /*else { 
                               PRINTF("R20 not fired\n");
                             }*/    
                        if (mf_ave_rssi.disc && mf_etx.average && mf_hpc.near > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.disc,mf_etx.average,mf_hpc.near);
                            peak_value_ouput_mf[outputindex] = 30;
                            outputindex++;
                         }
                        /*else { 
                               PRINTF("R21 not fired\n");
                             }*/  
                        if (mf_ave_rssi.disc && mf_etx.small && mf_hpc.very_far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.disc,mf_etx.small,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 30;
                            outputindex++;
                         }
                       /*else { 
                               PRINTF("R22 not fired\n");
                             }*/  
                        if (mf_ave_rssi.disc && mf_etx.average && mf_hpc.far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.disc,mf_etx.average,mf_hpc.far);
                            peak_value_ouput_mf[outputindex] = 30;
                            outputindex++;
                         }
                         /*else { 
                               PRINTF("R23 not fired\n");
                             }*/  
                        if (mf_ave_rssi.disc && mf_etx.large && mf_hpc.near > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.disc,mf_etx.large,mf_hpc.near);
                            peak_value_ouput_mf[outputindex] = 30;
                            outputindex++;
                         }
                         /*else { 
                               PRINTF("R24 not fired\n");
                             }*/  
                        if (mf_ave_rssi.disc && mf_etx.average && mf_hpc.very_far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.disc,mf_etx.average,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 30;
                            outputindex++;
                         } 
                         /*else { 
                               PRINTF("R25 not fired\n");
                             }*/   
                        if (mf_ave_rssi.disc && mf_etx.large && mf_hpc.far > 0)
                         {
                           fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.disc,mf_etx.large,mf_hpc.far);
                           peak_value_ouput_mf[outputindex] = 30;
                           outputindex++;
                         }
                           /*else { 
                               PRINTF("R26 not fired\n");
                             }*/  
                        if (mf_ave_rssi.disc && mf_etx.large && mf_hpc.very_far > 0)
                         {
                            fuzzy_quality_mf[outputindex] = min(mf_ave_rssi.disc,mf_etx.large,mf_hpc.very_far);
                            peak_value_ouput_mf[outputindex] = 30;
                            outputindex++;
                         }
                         /*else { 
                               PRINTF("R27 not fired\n");
                             }*/ 
                        /*Function call for deffuzzification process */ 
                        fuzzy_outputs = fuzzy_defuzzification(fuzzy_quality_mf,peak_value_ouput_mf, outputindex);
                           if (outputindex >= 27) {
                                outputindex = 0;
                              }
            
                            PRINTF("Fuzzy neighbour ouput quality = %u\n", fuzzy_outputs);
		              
				if (fuzzy_outputs > 60) 
		                    {
					priority = 1;
		                    }
		                if (fuzzy_outputs > 80) 
		                    {
					priority = 0;
		                    }                                               
		
			/* Schedule DIO response according to the priority assigned to the DIO */
			new_dio_interval(process_instance, NULL, 2, priority);                                       
			true_rssi_average = 0;
                        //fuzzy_outputs = 1;
		       } 
             else {
			//PRINTF("Ignoring DIO request. Average = %d\n", true_rssi_average);
			true_rssi_average = 0;
                        //fuzzy_outputs = 1;
		  }
		//}
	}
		break;
	}
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(multiple_dis_input, ev, data) {
	PROCESS_BEGIN();
	dis_event = process_alloc_event();
	while (1) {
		PROCESS_YIELD()
		;
		eventhandler2(ev, data);
	}
PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* Added flags and counter */
void 
dis_output(uip_ipaddr_t *addr, uint8_t flags, uint8_t counter, uint8_t rssi, uint8_t ip6id) 
{
	unsigned char *buffer;
	uip_ipaddr_t tmpaddr;
	char process_start_wait_dios = 0;

/* DAG Information Solicitation  - 2 bytes reserved      */
/*      0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7  */
/*     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ */
/*     |     Flags     |F| C | Reserved|   Option(s)...  */
/*     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ */

buffer = UIP_ICMP_PAYLOAD;
buffer[0] = rssi;
buffer[1] = flags << 7;
buffer[1] |= counter << 5;
buffer[2] = ip6id >> 2;
/*PRINTF("dis target is %u\n", buffer[2]);*/

if (addr == NULL) {
	uip_create_linklocal_rplnodes_mcast(&tmpaddr);
	addr = &tmpaddr;
}

/*PRINTF("RPL: Sending a DIS to "); PRINT6ADDR(addr); PRINTF("\n");*/

uip_icmp6_send(addr, ICMP6_RPL, RPL_CODE_DIS, 3);                                              //function that sends  DIS

/*
 * After sending a DIS. We check here if it was part of DIS burst (flag = 1).
 * We also check if it was the last DIS being sent (total of 3).
 * If this is true, we start the timer that waits for DIO replies from possible parents.
 * We use a flag to distinguish if we should start the process or reset the timer.
 */
if (addr == &tmpaddr && flags == 1 && counter == 3) {                                          //check here to increase DIS counter
	if (process_start_wait_dios == 0) {
		process_start(&wait_dios, NULL);
		process_post_synch(&wait_dios, SET_DIOS_INPUT, NULL);
		process_start_wait_dios++;
	} else {
		process_post_synch(&wait_dios, SET_DIOS_INPUT, NULL);
	}
}
}

/*---------------------------------------------------------------------------*/
static void
dio_input(void) 
{
unsigned char *buffer;
uint8_t buffer_length;
//rpl_dio_t dio;           //This value is made a global variable in rpl-private to be accessed in other files.
uint8_t subopt_type;
int i;
int len;
uip_ipaddr_t from;
uip_ipaddr_t *pref_addr;
rpl_parent_t *p_dio;
uip_ipaddr_t *from_addr;
uip_ds6_nbr_t *nbr;

rpl_instance_t *instance = &instance_table[0];
rpl_dag_t *dag = instance->current_dag;

memset(&dio, 0, sizeof(dio));

/* Set default values in case the DIO configuration option is missing. */
dio.dag_intdoubl = RPL_DIO_INTERVAL_DOUBLINGS;
dio.dag_intmin = RPL_DIO_INTERVAL_MIN;
dio.dag_redund = RPL_DIO_REDUNDANCY;
dio.dag_min_hoprankinc = RPL_MIN_HOPRANKINC;
dio.dag_max_rankinc = RPL_MAX_RANKINC;
dio.ocp = RPL_OF.ocp;
dio.default_lifetime = RPL_DEFAULT_LIFETIME;
dio.lifetime_unit = RPL_DEFAULT_LIFETIME_UNIT;

uip_ipaddr_copy(&from, &UIP_IP_BUF->srcipaddr);

/* DAG Information Object */
/*PRINTF("RPL: Received a DIO from "); PRINT6ADDR(&from); PRINTF("\n");*/

if ((nbr = uip_ds6_nbr_lookup(&from)) == NULL) {
	if ((nbr = uip_ds6_nbr_add(&from,
			(uip_lladdr_t *) packetbuf_addr(PACKETBUF_ADDR_SENDER), 0,
			NBR_REACHABLE)) != NULL) {
		/* set reachable timer */
		stimer_set(&nbr->reachable, UIP_ND6_REACHABLE_TIME / 1000);
		/*PRINTF("RPL: Neighbor added to neighbor cache "); PRINT6ADDR(&from); PRINTF(", "); PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER)); PRINTF("\n");*/
	} else {
		/*PRINTF("RPL: Out of Memory, dropping DIO from "); PRINT6ADDR(&from); PRINTF(", "); PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER)); PRINTF("\n");*/
		return;
	}
} else {
	/*PRINTF("RPL: Neighbor already in neighbor cache\n");*/
}

buffer_length = uip_len - uip_l3_icmp_hdr_len;

/* Process the DIO base option. */
i = 0;
buffer = UIP_ICMP_PAYLOAD;

dio.instance_id = buffer[i++];
dio.version = buffer[i++];
dio.rank = get16(buffer, i);
i += 2;

/*PRINTF("RPL: Incoming DIO (id, ver, rank) = (%u,%u,%u)\n",
		(unsigned)dio.instance_id,
		(unsigned)dio.version,
		(unsigned)dio.rank); */                                        

dio.grounded = buffer[i] & RPL_DIO_GROUNDED;
dio.mop = (buffer[i] & RPL_DIO_MOP_MASK) >> RPL_DIO_MOP_SHIFT;
dio.preference = buffer[i++] & RPL_DIO_PREFERENCE_MASK;

/*
 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | RPLInstanceID |Version Number |             Rank              |
 |+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 ||G|0| MOP | Prf |     DTSN      |     Flags | F |     RSSI	     |
 |+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                                                               |
 +                                                               +
 |                                                               |
 +                            DODAGID                            +
 |                                                               |
 +                                                               +
 |                                                               |
 ||+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+--+
 |   Option(s)...
 ||+-+-+-+-+-+-+-+-+
 *
 * According to the specification, DIO messages have the bytes Flags and Reserved equals to 0
 * F is being used to distinguish normal DIO from those triggered by the mobility process
 * When a parent sends a DIS with a flag, a DIO response is expected, and this DIO needs to carry a flag
 * so that normal/periodic DIOs don't trigger an unexpected behavior.
 * Reserved is being used to send the RSSI that is read by the parent upon DIS reception.
 */
dio.dtsn = buffer[i++];
i += 2;
/* one reserved byte */
/*i += 1; */

memcpy(&dio.dag_id, buffer + i, sizeof(dio.dag_id));
i += sizeof(dio.dag_id);

/*PRINTF("RPL: Incoming DIO (dag_id, pref) = ("); PRINT6ADDR(&dio.dag_id); PRINTF(", %u)\n", dio.preference);*/

/* Check if there are any DIO suboptions. */
for (; i < buffer_length; i += len) {
	subopt_type = buffer[i];
	if (subopt_type == RPL_OPTION_PAD1) {
		len = 1;
	} else {
		/* Suboption with a two-byte header + payload */
		len = 2 + buffer[i + 1];
	}

	if (len + i > buffer_length) {
		/*PRINTF("RPL: Invalid DIO packet\n"); RPL_STAT(rpl_stats.malformed_msgs++);*/
		return;
	}

	/*PRINTF("RPL: DIO option %u, length: %u\n", subopt_type, len - 2); */

	switch (subopt_type) {
	case RPL_OPTION_DAG_METRIC_CONTAINER:
		if (len < 6) {
			/*PRINTF("RPL: Invalid DAG MC, len = %d\n", len); */
			RPL_STAT(rpl_stats.malformed_msgs++);
			return;
		}
		dio.mc.type = buffer[i + 2];
		dio.mc.flags = buffer[i + 3] << 1;
		dio.mc.flags |= buffer[i + 4] >> 7;
		dio.mc.aggr = (buffer[i + 4] >> 4) & 0x3;
		dio.mc.prec = buffer[i + 4] & 0xf;
		dio.mc.length = buffer[i + 5];

		if (dio.mc.type == RPL_DAG_MC_NONE) {
			/* No metric container: do nothing */
		} else if (dio.mc.type == RPL_DAG_MC_ETX) {
			dio.mc.obj.etx = get16(buffer, i + 6);          
			/*PRINTF("RPL: DAG MC: type %u, flags %u, aggr %u, prec %u, length %u, ETX %u\n",
					(unsigned)dio.mc.type,
					(unsigned)dio.mc.flags,
					(unsigned)dio.mc.aggr,
					(unsigned)dio.mc.prec,
					(unsigned)dio.mc.length,
					(unsigned)dio.mc.obj.etx);*/
		} else if (dio.mc.type == RPL_DAG_MC_ENERGY) {
			dio.mc.obj.energy.flags = buffer[i + 6];
			dio.mc.obj.energy.energy_est = buffer[i + 7];
		} else {
			/*PRINTF("RPL: Unhandled DAG MC type: %u\n", (unsigned)dio.mc.type);*/
			return;
		}
		break;
	case RPL_OPTION_ROUTE_INFO:
		if (len < 9) {
			/*PRINTF("RPL: Invalid destination prefix option, len = %d\n", len); RPL_STAT(rpl_stats.malformed_msgs++);*/
			return;
		}

		/* The flags field includes the preference value. */
		dio.destination_prefix.length = buffer[i + 2];
		dio.destination_prefix.flags = buffer[i + 3];
		dio.destination_prefix.lifetime = get32(buffer, i + 4);

		if (((dio.destination_prefix.length + 7) / 8) + 8 <= len
				&& dio.destination_prefix.length <= 128) {
			/*PRINTF("RPL: Copying destination prefix\n");*/
			memcpy(&dio.destination_prefix.prefix, &buffer[i + 8],
					(dio.destination_prefix.length + 7) / 8);
		} else {
			/*PRINTF("RPL: Invalid route info option, len = %d\n", len); RPL_STAT(rpl_stats.malformed_msgs++);*/
			return;
		}

		break;
	case RPL_OPTION_DAG_CONF:
		if (len != 16) {
			/*PRINTF("RPL: Invalid DAG configuration option, len = %d\n", len); RPL_STAT(rpl_stats.malformed_msgs++);*/
			return;
		}

		/* Path control field not yet implemented - at i + 2 */
		dio.dag_intdoubl = buffer[i + 3];
		dio.dag_intmin = buffer[i + 4];
		dio.dag_redund = buffer[i + 5];
		dio.dag_max_rankinc = get16(buffer, i + 6);
		dio.dag_min_hoprankinc = get16(buffer, i + 8);
		dio.ocp = get16(buffer, i + 10);
		/* buffer + 12 is reserved */
		dio.default_lifetime = buffer[i + 13];
		dio.lifetime_unit = get16(buffer, i + 14);
		/*PRINTF("RPL: DAG conf:dbl=%d, min=%d red=%d maxinc=%d mininc=%d ocp=%d d_l=%u l_u=%u\n",
				dio.dag_intdoubl, dio.dag_intmin, dio.dag_redund,
				dio.dag_max_rankinc, dio.dag_min_hoprankinc, dio.ocp,
				dio.default_lifetime, dio.lifetime_unit);*/
		break;
	case RPL_OPTION_SMART_HOP:
		if (len != 4) {
			//printf("RPL: smart-HOP info not ok, len != 4\n");
			RPL_STAT(rpl_stats.malformed_msgs++);
			return;
		}
		i += 2;
		dio.flags = buffer[i++];
		dio.fuzzy_outputs = buffer[i++];
		break;

	default:
		PRINTF("RPL: Unsupported suboption type in DIO: %u\n",
				(unsigned)subopt_type);
	}
}

#ifdef RPL_DEBUG_DIO_INPUT
RPL_DEBUG_DIO_INPUT(&from, &dio);
#endif

/*
 * DIO reception can occur in 2 cases:
 *  - DIO reply when assessing parent
 *  - DIO reply when in discovery phase
 * IF we are assessing parent and receive a DIO:
 *  - Stop the countdown of the DIO reception
 *  if timer reached 0, the parent is considered unreachable
 *  - Post an event stating that a DIO was received and the parent is reachable.
 * If we are in discovery phase:
 *  - Save DIO address in array
 *  - Save RSSI in array
 *  - Save DIO in array
 *  - Increment total number of DIOs received (j)
 */

#if MOBILE_NODE
if(dio.flags == 1 && mobility_flag == 1) {          //dio.flags == 1, indicates DIO flag within the transmission phase.mob =1 shows mn is moving
	process_post_synch(&unreach_process, STOP_DIO_CHECK, NULL);
	process_post_synch(&unreach_process, PARENT_REACHABLE, dio.fuzzy_outputs);
	return;
}
if(dio.flags == 2 && mobility_flag == 1) {                      //dio.flags == 2, indicates DIO flag in discovery stage 
	p_dio = rpl_find_parent(dag, &from);
	pref_addr = rpl_get_parent_ipaddr(dag->preferred_parent);
	dio_addr = rpl_get_parent_ipaddr(p_dio);
	if(uip_ipaddr_cmp(dio_addr, pref_addr)) {
		/*printf("DIO from preferred parent -> DISCARDING\n");*/                //if the same return 
		return;
	}

	//PRINTF("Saving DIO from %u\n", from.u8[15]);
	possible_parent_addr[number_of_dios] = from;
	possible_parent_rssi[number_of_dios] = dio.fuzzy_outputs;
	dios[number_of_dios] = dio;
	number_of_dios++;
	PRINTF("Number of DIOs received = %d\n",number_of_dios);
	return;
}
#endif
if (mobility_flag != 1 && dio.flags == 0) {
	rpl_process_dio(&from, &dio, 0);
}
}
void eventhandler3(process_event_t ev, process_data_t data) {
rpl_instance_t *instance = &instance_table[0];
rpl_dag_t *dag = instance->current_dag;
int best_rssi, k, t;

switch (ev) {

/* Timer initiated after a DIS is sent, to wait for all DIO replies from possible parents. */
case SET_DIOS_INPUT: {
		etimer_set(&dios_input, CLOCK_SECOND / 20);
}
	break;

case STOP_DIOS_INPUT: {
	etimer_stop(&dios_input);
} break;

case PROCESS_EVENT_TIMER: {
	/*
	 * When the dios_input timer expires, we start comparing the received DIOs.
	 */
	if (data == &dios_input && etimer_expired(&dios_input)) {
		if (number_of_dios != 0 && mobility_flag == 1
				&& hand_off_backoff_flag == 0) {

			PRINTF("Received %d DIOS\n",number_of_dios);
			PRINTF("DIOS table list:\n");

			for (t = 0; t < number_of_dios; t++) {
				PRINTF("%u", possible_parent_addr[t].u8[15]);
				PRINT6ADDR(&possible_parent_addr[t]);
				PRINTF(" -> %u\n",possible_parent_rssi[t]);
			}
			best_parent_rssi = possible_parent_rssi[0];
                        PRINTF("A %u\n",best_parent_rssi);
			best_parent_addr = possible_parent_addr[0];
			best_parent_dio = dios[0];
			
			for (k = 1; k < number_of_dios; k++) {
				
				 /*PRINTF("COMPARING: \n");
				 PRINT6ADDR(&best_parent_addr);
				 PRINTF("BPR %u\n",best_parent_rssi);
				 PRINT6ADDR(&possible_parent_addr[k]);*/
				 PRINTF("PPR %u\n",possible_parent_rssi[k]); 
				if (possible_parent_rssi[k] > best_parent_rssi) {
					best_parent_rssi = possible_parent_rssi[k];
					best_parent_addr = possible_parent_addr[k];
					best_parent_dio = dios[k];
				}
			}
			PRINTF("Best -> %u\n", best_parent_addr.u8[15]);
			if (uip_ipaddr_cmp
			(&best_parent_addr,
					(rpl_get_parent_ipaddr(dag->preferred_parent)))) {
				PRINTF("Best parent = current parent\n");
				/*if (best_parent_rssi > 255) {
					best_parent_rssi -= 255;
				}
				best_rssi = best_parent_rssi -;
				if (best_parent_rssi > 200) {
					best_rssi = best_parent_rssi - 255 - 46;
				}*/
				if (best_parent_rssi <= 60) {
					PRINTF("Bad rssi -> Discovery phase\n"); 
					process_post_synch(&unreach_process, DIS_BURST, NULL);
				} else {
					process_post_synch(&tcpip_process, RESET_MOBILITY_FLAG,
							NULL);
				}
				/*else{
				 rpl_remove_parent(dag,dag->preferred_parent);
				 rpl_process_dio(&best_parent_addr, &best_parent_dio, 1);
				 } */
				/*process_post_synch(&dis_send_process,PROCESS_EVENT_INIT,NULL); */

				/*instance = rpl_get_instance(best_parent_dio.instance_id);
				 rpl_set_default_route(instance,&best_parent_addr); */
			} else {
				/* Process the DIO of the Best Parent */
				if(best_parent_addr.u8[15] != 0){
					mobility_flag = 0;
					rpl_process_dio(&best_parent_addr, &best_parent_dio, 1);
					number_of_dios = 0;
				}
			}
			for (k = 0; k < 5; k++) {
				possible_parent_rssi[k] = possible_parent_rssi[-1];
				possible_parent_addr[k] = possible_parent_addr[-1];
				dios[k] = dios[-1];
			}
		} else {

			/* No DIOs received. Repeat discovery phase. */
			if (mobility_flag == 1 && hand_off_backoff_flag == 0) {
				PRINTF("No DIOs received.\n");
					test_unreachable = 1;
					process_post(&unreach_process, PARENT_UNREACHABLE, NULL);
			}
		}
	}
}
	break;
}
}
PROCESS_THREAD(wait_dios, ev, data) {
PROCESS_BEGIN();
wait_dios_event = process_alloc_event();
while (1) {
	PROCESS_YIELD()
	;
	eventhandler3(ev, data);
}
PROCESS_END();
}

/*---------------------------------------------------------------------------*/

void dio_output(rpl_instance_t *instance, uip_ipaddr_t *uc_addr, uint8_t flags) {
unsigned char *buffer;
int pos;
rpl_dag_t *dag = instance->current_dag;
uint8_t output_rssi;
#if !RPL_LEAF_ONLY
uip_ipaddr_t addr;
#endif /* !RPL_LEAF_ONLY */

#if RPL_LEAF_ONLY
/* In leaf mode, we send DIO message only as unicasts in response to                     //note.. unicast address of DIO reply in
 unicast DIS messages. */
if(uc_addr == NULL) {
/*PRINTF("RPL: LEAF ONLY have multicast addr: skip dio_output\n");*/
return;
}
#endif /* RPL_LEAF_ONLY */

/* DAG Information Object */
pos = 0;

buffer = UIP_ICMP_PAYLOAD;
buffer[pos++] = instance->instance_id;
buffer[pos++] = dag->version;

#if RPL_LEAF_ONLY
/*PRINTF("RPL: LEAF ONLY DIO rank set to INFINITE_RANK\n"); */           //MN rank set to infinite rank because is a leaf node
set16(buffer, pos, INFINITE_RANK);
#else /* RPL_LEAF_ONLY */
set16(buffer, pos, dag->rank);                                                    
#endif /* RPL_LEAF_ONLY */
pos += 2;

buffer[pos] = 0;
if (dag->grounded) {
buffer[pos] |= RPL_DIO_GROUNDED;
}

buffer[pos] |= instance->mop << RPL_DIO_MOP_SHIFT;
buffer[pos] |= dag->preference & RPL_DIO_PREFERENCE_MASK;
pos++;

buffer[pos++] = instance->dtsn_out;

/* always request new DAO to refresh route */
RPL_LOLLIPOP_INCREMENT(instance->dtsn_out);
pos += 2;

memcpy(buffer + pos, &dag->dag_id, sizeof(dag->dag_id));
pos += 16;

#if !RPL_LEAF_ONLY
if (instance->mc.type != RPL_DAG_MC_NONE) {
instance->of->update_metric_container(instance);

buffer[pos++] = RPL_OPTION_DAG_METRIC_CONTAINER;
buffer[pos++] = 6;
buffer[pos++] = instance->mc.type;
buffer[pos++] = instance->mc.flags >> 1;
buffer[pos] = (instance->mc.flags & 1) << 7;
buffer[pos++] |= (instance->mc.aggr << 4) | instance->mc.prec;
if (instance->mc.type == RPL_DAG_MC_ETX) {
	buffer[pos++] = 2;
	set16(buffer, pos, instance->mc.obj.etx);                      // important for input to the fuzzy system
	pos += 2;
} else if (instance->mc.type == RPL_DAG_MC_ENERGY) {
	buffer[pos++] = 2;
	buffer[pos++] = instance->mc.obj.energy.flags;
	buffer[pos++] = instance->mc.obj.energy.energy_est;

} else {
	/*PRINTF("RPL: Unable to send DIO because of unhandled DAG MC type %u\n",
			(unsigned)instance->mc.type);*/
	return;
}
}
#endif /* !RPL_LEAF_ONLY */

/* Always add a DAG configuration option. */
buffer[pos++] = RPL_OPTION_DAG_CONF;
buffer[pos++] = 14;
buffer[pos++] = 0; /* No Auth, PCS = 0 */
buffer[pos++] = instance->dio_intdoubl;
buffer[pos++] = instance->dio_intmin;
buffer[pos++] = instance->dio_redundancy;
set16(buffer, pos, instance->max_rankinc);
pos += 2;
set16(buffer, pos, instance->min_hoprankinc);
pos += 2;
/* OCP is in the DAG_CONF option */
set16(buffer, pos, instance->of->ocp);
pos += 2;
buffer[pos++] = 0; /* reserved */
buffer[pos++] = instance->default_lifetime;
set16(buffer, pos, instance->lifetime_unit);
pos += 2;
/* Check if we have a prefix to send also. */
if (dag->prefix_info.length > 0) {
buffer[pos++] = RPL_OPTION_PREFIX_INFO;
buffer[pos++] = 30; /* always 30 bytes + 2 long */
buffer[pos++] = dag->prefix_info.length;
buffer[pos++] = dag->prefix_info.flags;
set32(buffer, pos, dag->prefix_info.lifetime);
pos += 4;
set32(buffer, pos, dag->prefix_info.lifetime);
pos += 4;
memset(&buffer[pos], 0, 4);
pos += 4;
memcpy(&buffer[pos], &dag->prefix_info.prefix, 16);
pos += 16;
/*PRINTF("RPL: Sending prefix info in DIO for "); PRINT6ADDR(&dag->prefix_info.prefix); PRINTF("\n");*/
} else {
/*PRINTF("RPL: No prefix to announce (len %d)\n", dag->prefix_info.length);*/
}
buffer[pos++] = RPL_OPTION_SMART_HOP;
buffer[pos++] = 2;
/* reserved 2 bytes */
buffer[pos++] = flags; /* flags */
//output_rssi = rssi_average; /* Embed RSSI average gathered from DIS burst, into DIO reply */         //changed to fuzzy output value.
output_rssi = fuzzy_outputs;
if (flags == 1) {
buffer[pos++] = dis_rssi;
} else {
buffer[pos++] = output_rssi; /* reserved */
}
//rssi_average = 0;
//fuzzy_outputs = 0;
#if RPL_LEAF_ONLY
#if (DEBUG)&DEBUG_PRINT
if(uc_addr == NULL) {
/*PRINTF("RPL: LEAF ONLY sending unicast-DIO from multicast-DIO\n");*/
}
#endif /* DEBUG_PRINT */
/*PRINTF("RPL: Sending unicast-DIO with rank %u to ", (unsigned)dag->rank);*/
PRINT6ADDR(uc_addr);
PRINTF("\n");
uip_icmp6_send(uc_addr, ICMP6_RPL, RPL_CODE_DIO, pos);
#else /* RPL_LEAF_ONLY */
/* Unicast requests get unicast replies! */
if (uc_addr == NULL) {
/*PRINTF("RPL: Sending a multicast-DIO with rank %u and flags = %d\n",*/
		/*(unsigned)instance->current_dag->rank, flags);*/
uip_create_linklocal_rplnodes_mcast(&addr);
uip_icmp6_send(&addr, ICMP6_RPL, RPL_CODE_DIO, pos);
} else {
/*PRINTF("RPL: Sending unicast-DIO with rank %u to ",
		(unsigned)instance->current_dag->rank); PRINT6ADDR(uc_addr); PRINTF("\n");*/
uip_icmp6_send(uc_addr, ICMP6_RPL, RPL_CODE_DIO, pos);
}
#endif /* RPL_LEAF_ONLY */
}
/*---------------------------------------------------------------------------*/
static void dao_input(void) {
uip_ipaddr_t dao_sender_addr;
rpl_dag_t *dag;
rpl_instance_t *instance;
unsigned char *buffer;
uint16_t sequence;
uint8_t instance_id;
uint8_t lifetime;
uint8_t prefixlen;
uint8_t flags;
uint8_t subopt_type;
/*
 uint8_t pathcontrol;
 uint8_t pathsequence;
 */
uip_ipaddr_t prefix;
uip_ds6_route_t *rep;
uint8_t buffer_length;
int pos;
int len;
int i;
int learned_from;
rpl_parent_t *p;
uip_ds6_nbr_t *nbr;

prefixlen = 0;

uip_ipaddr_copy(&dao_sender_addr, &UIP_IP_BUF->srcipaddr);

/* Destination Advertisement Object */
/*PRINTF("RPL: Received a DAO from "); PRINT6ADDR(&dao_sender_addr); PRINTF("\n");*/

buffer = UIP_ICMP_PAYLOAD;
buffer_length = uip_len - uip_l3_icmp_hdr_len;

pos = 0;
instance_id = buffer[pos++];

instance = rpl_get_instance(instance_id);
if (instance == NULL) {
/*PRINTF("RPL: Ignoring a DAO for an unknown RPL instance(%u)\n",
		instance_id);*/
return;
}

lifetime = instance->default_lifetime;

flags = buffer[pos++];
/* reserved */
pos++;
sequence = buffer[pos++];

dag = instance->current_dag;
/* Is the DAGID present? */
if (flags & RPL_DAO_D_FLAG) {
if (memcmp(&dag->dag_id, &buffer[pos], sizeof(dag->dag_id))) {
	/*PRINTF("RPL: Ignoring a DAO for a DAG different from ours\n");*/
	return;
}
pos += 16;
} else {
/* Perhaps, there are verification to do but ... */
}

/* Check if there are any RPL options present. */
for (i = pos; i < buffer_length; i += len) {
subopt_type = buffer[i];
if (subopt_type == RPL_OPTION_PAD1) {
	len = 1;
} else {
	/* The option consists of a two-byte header and a payload. */
	len = 2 + buffer[i + 1];
}

switch (subopt_type) {
case RPL_OPTION_TARGET:
	/* Handle the target option. */
	prefixlen = buffer[i + 3];
	memset(&prefix, 0, sizeof(prefix));
	memcpy(&prefix, buffer + i + 4, (prefixlen + 7) / CHAR_BIT);
	break;
case RPL_OPTION_TRANSIT:
	/* The path sequence and control are ignored. */
	/*      pathcontrol = buffer[i + 3];
	 pathsequence = buffer[i + 4]; */
	lifetime = buffer[i + 5];
	/* The parent address is also ignored. */
	break;
}
}

/*PRINTF("RPL: DAO lifetime: %u, prefix length: %u prefix: ",
	(unsigned)lifetime, (unsigned)prefixlen); PRINT6ADDR(&prefix); PRINTF("\n");*/

rep = uip_ds6_route_lookup(&prefix);

if (lifetime == RPL_ZERO_LIFETIME) {
/*PRINTF("RPL: No-Path DAO received\n");*/
/* No-Path DAO received; invoke the route purging routine. */
if (rep != NULL && rep->state.nopath_received == 0&&
rep->length == prefixlen &&
uip_ds6_route_nexthop(rep) != NULL &&
uip_ipaddr_cmp(uip_ds6_route_nexthop(rep), &dao_sender_addr)) {
	/*PRINTF("RPL: Setting expiration timer for prefix "); PRINT6ADDR(&prefix); PRINTF("\n");*/
	rep->state.nopath_received = 1;
	rep->state.lifetime = DAO_EXPIRATION_TIMEOUT;

	/* We forward the incoming no-path DAO to our parent, if we have
	 one. */
	if (dag->preferred_parent != NULL
			&& rpl_get_parent_ipaddr(dag->preferred_parent) != NULL) {
		/*PRINTF("RPL: Forwarding no-path DAO to parent "); PRINT6ADDR(rpl_get_parent_ipaddr(dag->preferred_parent)); PRINTF("\n");*/
		uip_icmp6_send(rpl_get_parent_ipaddr(dag->preferred_parent),
		ICMP6_RPL, RPL_CODE_DAO, buffer_length);
	}
	if (flags & RPL_DAO_K_FLAG) {
		dao_ack_output(instance, &dao_sender_addr, sequence);
	}
}
return;
}

learned_from =
	uip_is_addr_mcast(&dao_sender_addr) ?
	RPL_ROUTE_FROM_MULTICAST_DAO :
											RPL_ROUTE_FROM_UNICAST_DAO;

/*PRINTF("RPL: DAO from %s\n",
	learned_from ==
	RPL_ROUTE_FROM_UNICAST_DAO ? "unicast" : "multicast");*/
if (learned_from == RPL_ROUTE_FROM_UNICAST_DAO) {
/* Check whether this is a DAO forwarding loop. */
p = rpl_find_parent(dag, &dao_sender_addr);
/* check if this is a new DAO registration with an "illegal" rank */
/* if we already route to this node it is likely */
if (p != NULL &&
DAG_RANK(p->rank, instance) < DAG_RANK(dag->rank, instance)) {
	/*PRINTF
	("RPL: Loop detected when receiving a unicast DAO from a node with a lower rank! (%u < %u)\n",
			DAG_RANK(p->rank, instance), DAG_RANK(dag->rank, instance));*/
	p->rank = INFINITE_RANK;
	p->updated = 1;
	return;
}

/* If we get the DAO from our parent, we also have a loop. */
if (p != NULL && p == dag->preferred_parent) {
	PRINTF
	("RPL: Loop detected when receiving a unicast DAO from our parent\n");
	p->rank = INFINITE_RANK;
	p->updated = 1;
	return;
}
}

/*PRINTF("RPL: adding DAO route\n");*/

if ((nbr = uip_ds6_nbr_lookup(&dao_sender_addr)) == NULL) {
if ((nbr = uip_ds6_nbr_add(&dao_sender_addr,
		(uip_lladdr_t *) packetbuf_addr(PACKETBUF_ADDR_SENDER), 0,
		NBR_REACHABLE)) != NULL) {
	/* set reachable timer */
	stimer_set(&nbr->reachable, UIP_ND6_REACHABLE_TIME / 1000);
	/*PRINTF("RPL: Neighbor added to neighbor cache "); PRINT6ADDR(&dao_sender_addr); PRINTF(", "); PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER)); PRINTF("\n");*/
} else {
	/*PRINTF("RPL: Out of Memory, dropping DAO from "); PRINT6ADDR(&dao_sender_addr); PRINTF(", "); PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER)); PRINTF("\n");*/
	return;
}
} else {
/*PRINTF("RPL: Neighbor already in neighbor cache\n");*/
}

rpl_lock_parent(p);

rep = rpl_add_route(dag, &prefix, prefixlen, &dao_sender_addr);
if (rep == NULL) {
RPL_STAT(rpl_stats.mem_overflows++); PRINTF("RPL: Could not add a route after receiving a DAO\n");
return;
}

rep->state.lifetime = RPL_LIFETIME(instance, lifetime);
rep->state.learned_from = learned_from;

if (learned_from == RPL_ROUTE_FROM_UNICAST_DAO) {
if (dag->preferred_parent != NULL
		&& rpl_get_parent_ipaddr(dag->preferred_parent) != NULL) {
	/*PRINTF("RPL: Forwarding DAO to parent "); PRINT6ADDR(rpl_get_parent_ipaddr(dag->preferred_parent)); PRINTF("\n");*/
	uip_icmp6_send(rpl_get_parent_ipaddr(dag->preferred_parent),
	ICMP6_RPL, RPL_CODE_DAO, buffer_length);
}
if (flags & RPL_DAO_K_FLAG) {
	dao_ack_output(instance, &dao_sender_addr, sequence);
}
}
}
/*---------------------------------------------------------------------------*/
void dao_output(rpl_parent_t *parent, uint8_t lifetime) {
/* Destination Advertisement Object */
uip_ipaddr_t prefix;

if (get_global_addr(&prefix) == 0) {
/*PRINTF("RPL: No global address set for this node - suppressing DAO\n");*/
return;
}

/* Sending a DAO with own prefix as target */
dao_output_target(parent, &prefix, lifetime);
}
/*---------------------------------------------------------------------------*/
void dao_output_target(rpl_parent_t *parent, uip_ipaddr_t *prefix,
uint8_t lifetime) {
rpl_dag_t *dag;
rpl_instance_t *instance;
unsigned char *buffer;
uint8_t prefixlen;
int pos;

/* Destination Advertisement Object */

/* If we are in feather mode, we should not send any DAOs */
if (rpl_get_mode() == RPL_MODE_FEATHER) {
return;
}

if (parent == NULL) {
/*PRINTF("RPL dao_output_target error parent NULL\n");*/
return;
}

dag = parent->dag;
if (dag == NULL) {
/*PRINTF("RPL dao_output_target error dag NULL\n");*/
return;
}

instance = dag->instance;

if (instance == NULL) {
/*PRINTF("RPL dao_output_target error instance NULL\n");*/
return;
}
if (prefix == NULL) {
/*PRINTF("RPL dao_output_target error prefix NULL\n");*/
return;
}
#ifdef RPL_DEBUG_DAO_OUTPUT
RPL_DEBUG_DAO_OUTPUT(parent);
#endif

buffer = UIP_ICMP_PAYLOAD;

RPL_LOLLIPOP_INCREMENT(dao_sequence);
pos = 0;

buffer[pos++] = instance->instance_id;
buffer[pos] = 0;
#if RPL_DAO_SPECIFY_DAG
buffer[pos] |= RPL_DAO_D_FLAG;
#endif /* RPL_DAO_SPECIFY_DAG */
#if RPL_CONF_DAO_ACK
buffer[pos] |= RPL_DAO_K_FLAG;
#endif /* RPL_CONF_DAO_ACK */
++pos;
buffer[pos++] = 0; /* reserved */
buffer[pos++] = dao_sequence;
#if RPL_DAO_SPECIFY_DAG
memcpy(buffer + pos, &dag->dag_id, sizeof(dag->dag_id));
pos += sizeof(dag->dag_id);
#endif /* RPL_DAO_SPECIFY_DAG */

/* create target subopt */
prefixlen = sizeof(*prefix) * CHAR_BIT;
buffer[pos++] = RPL_OPTION_TARGET;
buffer[pos++] = 2 + ((prefixlen + 7) / CHAR_BIT);
buffer[pos++] = 0; /* reserved */
buffer[pos++] = prefixlen;
memcpy(buffer + pos, prefix, (prefixlen + 7) / CHAR_BIT);
pos += ((prefixlen + 7) / CHAR_BIT);

/* Create a transit information sub-option. */
buffer[pos++] = RPL_OPTION_TRANSIT;
buffer[pos++] = 4;
buffer[pos++] = 0; /* flags - ignored */
buffer[pos++] = 0; /* path control - ignored */
buffer[pos++] = 0; /* path seq - ignored */
buffer[pos++] = lifetime;

/*PRINTF("RPL: Sending DAO with prefix "); PRINT6ADDR(prefix); PRINTF(" to "); PRINT6ADDR(rpl_get_parent_ipaddr(parent)); PRINTF("\n");*/

if (rpl_get_parent_ipaddr(parent) != NULL) {
uip_icmp6_send(rpl_get_parent_ipaddr(parent), ICMP6_RPL, RPL_CODE_DAO, pos);
}
/*
 * smart-HOP depends a lot on downward routes.
 * DAO-ACK is enabled but there was no mechanism to re-send it in case of failure.
 * After we process the Best parent DIO and send a DAO in the end, we check if a DAO-ACK
 * was received within a certain period of time.
 * If not, re-send it.
 */
/*if(mobility_flag && check_dao_ack) {
 ctimer_set(&dao_period, CLOCK_SECOND / 4, rpl_schedule_dao, instance);
 }*/
}
/*---------------------------------------------------------------------------*/
static void dao_ack_input(void) {
#if DEBUG
unsigned char *buffer;
uint8_t buffer_length;
uint8_t instance_id;
uint8_t sequence;
uint8_t status;

buffer = UIP_ICMP_PAYLOAD;
buffer_length = uip_len - uip_l3_icmp_hdr_len;

instance_id = buffer[0];
sequence = buffer[2];
status = buffer[3];

/*PRINTF
("RPL: Received a DAO ACK with sequence number %d and status %d from ",
	sequence, status);*/
PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
PRINTF("\n");
/*
 * DAO was received. Stop the timer and reset flag
 */
if(check_dao_ack == 1) {
check_dao_ack = 0;
ctimer_stop(&dao_period);
}
#endif /* DEBUG */
}
/*---------------------------------------------------------------------------*/
void dao_ack_output(rpl_instance_t *instance, uip_ipaddr_t *dest,
uint8_t sequence) {
unsigned char *buffer;

/*PRINTF("RPL: Sending a DAO ACK with sequence number %d to ", sequence); PRINT6ADDR(dest); PRINTF("\n");*/

buffer = UIP_ICMP_PAYLOAD;

buffer[0] = instance->instance_id;
buffer[1] = 0;
buffer[2] = sequence;
buffer[3] = 0;

uip_icmp6_send(dest, ICMP6_RPL, RPL_CODE_DAO_ACK, 4);
}
/*---------------------------------------------------------------------------*/
void uip_rpl_input(void) {
/*PRINTF("Received an RPL control message\n");*/
switch (UIP_ICMP_BUF->icode) {
case RPL_CODE_DIO:
dio_input();
break;
case RPL_CODE_DIS:
dis_input();
break;
case RPL_CODE_DAO:
dao_input();
break;
case RPL_CODE_DAO_ACK:
dao_ack_input();
break;
default:
/*PRINTF("RPL: received an unknown ICMP6 code (%u)\n", UIP_ICMP_BUF->icode);*/
break;
}
uip_len = 0;
}
#endif /* UIP_CONF_IPV6 */
