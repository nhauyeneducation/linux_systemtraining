#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "table.h"
#include "macvlan.h"

#ifdef ENABLE_DEBUG
#define DEBUG printf
#else
#define DEBUG 
#endif

#define MAX_RECORD 15000
#define GOLDEN_RATIO_PRIME_24 0x9e3701UL    

#define L1_HASHKEY_LEN 3
#define L1_HASHKEY_BIT 8
#define LOG printf

enum error_s
{
    SUCCESS = 0,
    ERR_TABLE_NEW = -100,
    ERR_MAX_RECORD_OVER, 
    ERR_MEM
};

Table_T mactable;

int mac_cmp(const void *x, const void *y)
{
    return memcmp(x, y, MACADDR_LEN);
}

unsigned int hash_24(unsigned int val, unsigned int bits)
{
    unsigned int hash = val * GOLDEN_RATIO_PRIME_24;

    return (hash >> (24 - bits)) & ((1 << bits) - 1);
}

unsigned mac_hash(const void *key)
{
    unsigned char *macaddr = (unsigned char *)key;
    unsigned int mackey = 0;
    unsigned machash;
 
    mackey = (macaddr[0] << 16) | (macaddr[1] << 8) | (macaddr[2]); 
    machash =  (unsigned )hash_24(mackey, L1_HASHKEY_BIT);      
    DEBUG("hashpos %u\n", machash);
    return machash; 
}

int macv_init(unsigned int max_record)
{
    if(MAX_RECORD < max_record)
        return ERR_MAX_RECORD_OVER;
    mactable = Table_new(max_record, mac_cmp, mac_hash);
    if(NULL == mactable) 
    {
        LOG("table new error\n");
        return ERR_TABLE_NEW; 
    } 
    return SUCCESS;    
}

int macv_total_record(void)
{
    return Table_length(mactable);
}

void show_macv(void)
{
    int i;
    void **array = Table_toArray(mactable, NULL);
    unsigned char *macaddr;
    mac_vlan_t *macvlan;

    LOG("--------------------------------------------\n\n");
    LOG("Vlan    Mac Address          Type         Port\n");
    LOG("----    ---------------      -------      -----\n");
 

    for(i=0; array[i]; i+=2)
    {
        macaddr = (unsigned char *)(array[i]);
        macvlan = (mac_vlan_t *)(array[i+1]); 
        LOG("%4d    %02x:%02x:%02x:%02x:%02x:%02x    %7s    %4d\n", macvlan->vlanid, macvlan->macaddr[0], macvlan->macaddr[1], macvlan->macaddr[2], macvlan->macaddr[3], macvlan->macaddr[4], macvlan->macaddr[5], macvlan->status==1?"DYNAMIC":"STATIC", macvlan->inc_port);
    }
    LOG("Total of record %5d\n", macv_total_record());
    free(array);
}

mac_vlan_t *macv_insert(mac_vlan_t *mace)
{
    return Table_put(mactable, mace->macaddr, mace); 
}

mac_vlan_t *macv_remove_by_macaddr(unsigned char *macaddr)
{
    return Table_remove(mactable, (const void *)macaddr);
}

mac_vlan_t *macv_find_by_macaddr(unsigned char *macaddr)
{
    return Table_get(mactable, (const void *)macaddr);
}

void freeall_record(const void *key, void **value, void *cl)
{
    if(*value)
    {
        free(*value);
    } 
}

void macv_delete_table(void)
{
    Table_map(mactable, freeall_record, NULL);
    Table_free(&mactable);
}

/* This interface written just for test - begin*/
#define INSERT_TEST 10000
#define MACSTR_LEN 150
#define LEARNTYPE_LEN 30
mac_vlan_t *create_random_mac_vlan(void)
{
    int i;
    mac_vlan_t *mac_vlan_e = malloc(sizeof(mac_vlan_t));
    if(NULL == mac_vlan_e)
    {
        return NULL;  
    }
    mac_vlan_e->vlanid   = (unsigned int )(random()%2000); 
    mac_vlan_e->inc_port = (unsigned int )(random()%24); 
    mac_vlan_e->status = (unsigned int )(random()%2);
    for(i=0;i<MACADDR_LEN;i++)
        mac_vlan_e->macaddr[i] = (unsigned char)(((unsigned int)random()) & 0x000000ff);

    DEBUG("create %02x:%02x:%02x:%02x:%02x:%02x vlanid %d key %p macvlan %p \n",mac_vlan_e->macaddr[0], mac_vlan_e->macaddr[1],mac_vlan_e->macaddr[2],
                    mac_vlan_e->macaddr[3], mac_vlan_e->macaddr[4], mac_vlan_e->macaddr[5], mac_vlan_e->vlanid, &(mac_vlan_e->macaddr[0]), mac_vlan_e); 
    return mac_vlan_e;  
}

int macv_insert_test(unsigned int num_insert_test)
{
    int i;
    for(i=0;i<num_insert_test;i++)
    {
        mac_vlan_t *mace = create_random_mac_vlan();
        macv_insert(mace);
    }
    return i;
}

int main(void)
{
    mac_vlan_t *found_mace = NULL, *add_mace = NULL;
    char macstr[MACSTR_LEN] = {0};
    unsigned char macaddr[MACADDR_LEN] = {0};
    int vlanid;
    char learntype[LEARNTYPE_LEN] = {0};
    int port;
    char command;
    int numread;
    macv_init(INSERT_TEST);
    macv_insert_test(INSERT_TEST/2000);
    LOG("Test bin already add %d record to mac table.Let type your command to query\n ", INSERT_TEST/2000);

    while(1)
    {
        memset(macstr, 0x00, MACSTR_LEN);
        memset(learntype, 0x00, LEARNTYPE_LEN);
        port = vlanid = 0;     
        fgets(macstr, MACSTR_LEN, stdin);
//        printf("Got str %s\n", macstr);
        numread = sscanf(macstr, "%c %*s %02x:%02x:%02x:%02x:%02x:%02x %*s %d %*s %s %*s %d",&command ,&macaddr[0], &macaddr[1], &macaddr[2], &macaddr[3], &macaddr[4], &macaddr[5], &vlanid, learntype, &port); 

        if(command == 'r')
        {
            found_mace = macv_remove_by_macaddr(macaddr);
            if(NULL == found_mace)
            {
                LOG("Address %02x:%02x:%02x:%02x:%02x:%02x not found in table, current total record %d\n",macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5] ,Table_length(mactable));  
            }
            else
            {
                LOG("Address %02x:%02x:%02x:%02x:%02x:%02x deleted from table, current total record %d\n", macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5],Table_length(mactable)); 
                free(found_mace);
            } 
            continue;
        } 
        else if(command == 'a')
        {
            if(0 >= vlanid || 0 >= port || 2048 < vlanid || 10000 < port)
            {
               LOG("Don't have info to add mace\n");
               continue;  
            } 
            add_mace = malloc(sizeof(mac_vlan_t));
            add_mace->vlanid = vlanid;
            add_mace->inc_port = port;
            memcpy(add_mace->macaddr, macaddr, MACADDR_LEN);
            if(0 == strcmp(learntype, "STATIC"))
                add_mace->status = 0;
            else
                add_mace->status = 1;

            found_mace = macv_insert(add_mace);  
            if(NULL != found_mace)
            {
                LOG("Record of mac %02x:%02x:%02x:%02x:%02x:%02x existing in table. Info updated and current total record %d\n", macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5], Table_length(mactable)); 
            }
            else
            {
                LOG("New record of mac %02x:%02x:%02x:%02x:%02x:%02x added to table, current total record %d\n", macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5], Table_length(mactable)); 
            }  
            continue; 
        }
        else if(command == 's')
        {
            show_macv();
            continue; 
        }  
        else if(command == 'e')
        {
            macv_delete_table();
            LOG("Good bye !!!");
            break; 
        }
        else
        {
             LOG("Unknown command %c.Type a-add, r-remove, s-show, e-exit. example \n", command);
             LOG("a mac c9:9a:66:32:0d:b7 vlanid 100 learntype DYNAMIC port 102\n");
             LOG("r mac c9:9a:66:32:0d:b7\n"); 
             LOG("s\n");
             LOG("e\n");  
             continue;
        }
    }
   
    return 0;     
}

/* This interface written just for test - end*/
