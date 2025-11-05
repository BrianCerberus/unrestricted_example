#ifndef _GWPMSTATS_H_
#define _GWPMSTATS_H_

#include <stdbool.h> // for bool type

/*
    register_pm_stats: Called by the radio.c rx interrupt, register the SU info if PM stat data is received.
    Inputs:
        gw_stats_shmptr: gateway stats pointer (on the linux chip)
        su_mac_address: last 3 bytes of the SU MAC address
        rx_ts: timestamp object
    Return:
        boolean: True if successfully registered, False otherwise
*/
bool register_pm_stats(gw_stats_v1_t *gw_stats_shmptr, uint8_t *su_mac_address, uint32_t rx_ts);

/*
    find_su_index: find the SU index in the stack, if not registered, find the next available slot and return the index
    Inputs:
        gw_stats_shmptr: gateway stats pointer (on the linux chip)
        su_mac_address: last 3 bytes of the SU MAC address
    Return:
        uint8_t: the located SU index in stack
*/
uint8_t find_su_index(gw_stats_v1_t *gw_stats_shmptr, uint8_t *su_mac_address);

/*
    PM_STATS_check: checking the pm Stats, called by the timer handler by every 60 seconds,
        check and print out all PM Stats info on stack 
    Inputs:
        gw_stats_shmptr: gateway stats pointer (on the linux chip)
        ts: current timestamp, provided by the timer handler
    Return:
        int: -1 for error, return counts of already sent resend request for SUs
*/
int PM_STATS_check(gw_stats_v1_t *gw_stats_shmptr, uint32_t ts);

/*
    PM_STATS_resend_req: Send resend request to a given SU, currently not in use
    Inputs:
        gw_stats_shmptr: gateway stats pointer (on the linux chip)
        su_mac_address: 3 byte SU MAC address
    Return:
        Boolean: true if sent, false if not sent
*/
bool PM_STATS_resend_req(gw_stats_v1_t *gw_stats_shmptr, uint8_t *su_mac_address);

#endif /* _GWPMSTATS_H_ */