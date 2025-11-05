#include "gw-config.h" // Reference for the PM_Stats size
#include <string.h> // for strcmp
#include <syslog.h> // for syslog
#include "gw_stats.h" // for gw_stats
#include <stdint.h>
#include <time.h>
#include "gateway.h"
#include "gw_stats.h"
#include "pktcomm.h"
#include <stdio.h>
#include "gwpmstats.h"
#include <ctype.h>
#include <stdbool.h>

#define STACK_IS_FULL 255  // Modify this if the size of su stack exceed 256 - which is unlikely

#define EN_CFG_PATH "/etc/en-cfg"

// DJB2 hash function, perfect for adjacent SU MAC addresses
int hash_mac_address(uint8_t *mac_address) {
    uint32_t hash = 5381;
    for (int i = 0; i < 3; i++) {
        hash = ((hash << 5) + hash) + mac_address[i]; // hash * 33 + mac_address[i]
    }
    return (int)(hash % GW_PM_STATS_MAX);
}

/*
    register_pm_stats: Called by the radio.c rx interrupt, register the SU info if PM stat data is received.
    Inputs:
        gw_stats_shmptr: gateway stats pointer (on the linux chip)
        su_mac_address: last 3 bytes of the SU MAC address
        rx_ts: timestamp object
    Return:
        boolean: True if successfully registered, False otherwise
*/
bool register_pm_stats(gw_stats_v1_t *gw_stats_shmptr, uint8_t *su_mac_address, uint32_t rx_ts)
{
    // When PM Stats Monitoring feature is enabled in en-cfg, this function will be call from radio.c
    //  whenever there is an PM Stats received by radio.
    // The function will register the SU mac address and the Timestamp to the Hash Table
    // Currently the max SU supported is 150 (could be modified)

    uint8_t su_index = find_su_index(gw_stats_shmptr, su_mac_address);

    // check if the stack is full
    if (su_index == STACK_IS_FULL) {
        syslog(LOG_ERR, "PM Stats FATAL: SU %02x:%02x:%02x is not registered to PM STATS Stack at time %d.",
               su_mac_address[0], su_mac_address[1], su_mac_address[2], rx_ts);
        return false;
    } else {
        // the stack is not full, now process to record the PM Stats
        // check if this is the first time SU is registered
        if (gw_stats_shmptr->pm.gw_pm_stats_individual[su_index].su_pm_stats_count == 0) {
            gw_stats_shmptr->pm.su_count++;
            gw_stats_shmptr->pm.gw_pm_stats_individual[su_index].su_pm_stats_count++;
            gw_stats_shmptr->pm.gw_pm_stats_individual[su_index].su_pm_stats_ts_last = rx_ts;
            memcpy(gw_stats_shmptr->pm.gw_pm_stats_individual[su_index].su_mac_addr, su_mac_address, 3);
            syslog(LOG_DEBUG, "PM Stats: Registered SU %02x:%02x:%02x to PM STATS Stack at time %d.",
                   su_mac_address[0], su_mac_address[1], su_mac_address[2], rx_ts);
            syslog(LOG_DEBUG, "PM Stats: SU Count: %d", gw_stats_shmptr->pm.su_count);
            return true;
        } else {
            // not first time, need to check the timestamp
            if (rx_ts - gw_stats_shmptr->pm.gw_pm_stats_individual[su_index].su_pm_stats_ts_last < GW_PM_STATS_IGNORE_SEC) {
                // two PM Stats received within 60 second, ignore this one (could be hopped message)
                return true;
            } else {
                gw_stats_shmptr->pm.gw_pm_stats_individual[su_index].su_pm_stats_count++;
                gw_stats_shmptr->pm.gw_pm_stats_individual[su_index].su_pm_stats_ts_last = rx_ts;

                syslog(LOG_DEBUG, "PM Stats: Updated SU %02x:%02x:%02x to PM STATS Stack at time %d.",
                       su_mac_address[0], su_mac_address[1], su_mac_address[2], rx_ts);
                syslog(LOG_DEBUG, "PM Stats SU Count: %d", gw_stats_shmptr->pm.su_count);
                return true;
            }
        }
    }
}

/*
    find_su_index: find the SU index in the stack, if not registered, find the next available slot and return the index
    Inputs:
        gw_stats_shmptr: gateway stats pointer (on the linux chip)
        su_mac_address: last 3 bytes of the SU MAC address
    Return:
        uint8_t: the located SU index in stack
*/
uint8_t find_su_index(gw_stats_v1_t *gw_stats_shmptr, uint8_t *su_mac_address)
{
    uint32_t hash_index = hash_mac_address(su_mac_address);
    if (gw_stats_shmptr->pm.su_count == 0) {
        // first SU ever
        return hash_index;
    } else {
        // Check if the hash MAC address is not taken
        if (gw_stats_shmptr->pm.gw_pm_stats_individual[hash_index].su_pm_stats_count == 0) {
            return hash_index;
        }
        // Check if the hash MAC address is the same
        if (memcmp(gw_stats_shmptr->pm.gw_pm_stats_individual[hash_index].su_mac_addr, su_mac_address, 3) == 0) {
            return hash_index;
        } else {
            // Collision, search for the MAC address in the stack, until there is an adjacent empty slot
            for (uint8_t i = 1; i < GW_PM_STATS_MAX; i++) {
                uint8_t search_index = (hash_index + i) % GW_PM_STATS_MAX;
                // first check if the slot is used or not
                if (gw_stats_shmptr->pm.gw_pm_stats_individual[search_index].su_pm_stats_count == 0) {
                    // empty slot, and all previous slots are checked, no existing record
                    return search_index;
                } else if (memcmp(gw_stats_shmptr->pm.gw_pm_stats_individual[search_index].su_mac_addr, su_mac_address, 3) == 0) {
                    // found the MAC address
                    return search_index;
                } else {
                    // slot used, continue searching
                }
            }
            // no empty slot found
            return STACK_IS_FULL;
        }
    }
}

/*
    PM_STATS_check: checking the pm Stats, called by the timer handler by every 60 seconds,
        check and print out all PM Stats info on stack 
    Inputs:
        gw_stats_shmptr: gateway stats pointer (on the linux chip)
        ts: current timestamp, provided by the timer handler
    Return:
        int: -1 for error, return counts of already sent resend request for SUs
*/
int PM_STATS_check(gw_stats_v1_t *gw_stats_shmptr, uint32_t ts)
{
    int return_val = 0;

    // debug information
    syslog(LOG_DEBUG, "PM_STATS_check called by the gw_timer_handler.");
    syslog(LOG_DEBUG, "PM Stats: Current SU Count: %d", gw_stats_shmptr->pm.su_count);
    syslog(LOG_DEBUG, "PM Stats: Available SU allocations: %d", GW_PM_STATS_MAX - gw_stats_shmptr->pm.su_count);

    // go over the sensors in the list one by one
    // Since the SUs are placed using the hash function, we will have to loop through the whole list
    int entry_count = 0;
    for (int index = 0; index < GW_PM_STATS_MAX; index++) {
        // check if the stats exist
        if (gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_pm_stats_count != 0) {
            entry_count++;
            syslog(LOG_DEBUG, "Sensor Hash Index %03d | MAC %02x:%02x:%02x | PM Stats Count %d | Entry %03d out of %03d",
            index, gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_mac_addr[0], 
            gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_mac_addr[1], 
            gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_mac_addr[2], 
            gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_pm_stats_count,
            entry_count, gw_stats_shmptr->pm.su_count);
            // su_pm_stats_count should be always greater than 1 - because they are registered
            // it takes more than 40,000 years to overflow uint 32, less than a year for uint 16

            // PM STATS overdue, currently not in use
            if (ts - gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_pm_stats_ts_last
            > GW_PM_STATS_INTERVAL_SEC + GW_PM_STATS_OVERDUE_SEC) {
                // send PM STATS resend request
                if (PM_STATS_resend_req(gw_stats_shmptr, gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_mac_addr)) {
                    // successfully sent request
                    syslog(LOG_DEBUG, "PM Stats: SU %02x:%02x:%02x missed a PM STATS report.",
                    gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_mac_addr[0], 
                    gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_mac_addr[1], 
                    gw_stats_shmptr->pm.gw_pm_stats_individual[index].su_mac_addr[2]);
                    return_val++;
                }
            }
        }

    }

    return return_val;
}

/*
    PM_STATS_resend_req: Send resend request to a given SU, currently not in use
    Inputs:
        gw_stats_shmptr: gateway stats pointer (on the linux chip)
        su_mac_address: 3 byte SU MAC address
    Return:
        Boolean: true if sent, false if not sent
*/
bool PM_STATS_resend_req(gw_stats_v1_t *gw_stats_shmptr, uint8_t *su_mac_address)
{
    // sending resend request to one sensor only, called by the PM_STATS_check
    // TODO
    return true;
}