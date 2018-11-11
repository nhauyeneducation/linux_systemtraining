#ifndef __MACVLAN__
#define __MACVLAN__

#define MACADDR_LEN 6

typedef struct mac_vlan_s
{
    unsigned int  vlanid;
    unsigned char macaddr[MACADDR_LEN];
    unsigned int  inc_port;
    unsigned int  status;      
} mac_vlan_t;

int macv_init(unsigned int max_record);
#ifdef ENABLE_DEBUG
void macv_dump(void);
#endif
mac_vlan_t *macv_insert(mac_vlan_t *mace);
mac_vlan_t *macv_remove_by_macaddr(unsigned char *macddr);
mac_vlan_t *macv_find_by_macaddr(unsigned char *macaddr);

#endif
