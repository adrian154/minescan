#include "addr-gen.h"
#include <stdlib.h>
#include <stdio.h>

int init_addrgen(struct AddressGenerator *addr_gen) {

    addr_gen->finished = false;
    addr_gen->state = 0;

    int sz = 64;
    addr_gen->exclude_prefixes= malloc(sz * sizeof(uint32_t));
    addr_gen->exclude_masks = malloc(sz * sizeof(uint32_t));

    // Read excluded subnets list
    FILE *fp = fopen("exclude.txt", "r");
    if(fp == NULL) {
        perror("failed to read exclude.txt");
        return 1;
    }

    char line[32];
    int octets[4];
    int prefixLen;
    int idx = 0;
    while(fgets(line, sizeof(line), fp)) {
        if(sscanf(line, "%d.%d.%d.%d/%d", octets, octets + 1, octets + 2, octets + 3, &prefixLen) == 5) {
            addr_gen->exclude_prefixes[idx] = (uint32_t)(octets[0] & 0xff) << 24 | (uint32_t)(octets[1] & 0xff) << 16 | (uint32_t)(octets[2] & 0xff) << 8 | (uint32_t)(octets[3] & 0xff);
            addr_gen->exclude_masks[idx] = ~((uint32_t)0xffffffff >> prefixLen);
            idx++;
            if(idx > sz) {
                addr_gen->exclude_prefixes = realloc(addr_gen->exclude_prefixes, sz * 2 * sizeof(uint32_t));
                addr_gen->exclude_masks =  realloc(addr_gen->exclude_masks, sz * 2 * sizeof(uint32_t));
            }
        }
    }

    addr_gen->num_excluded_subnets = idx;
    fclose(fp);
    return 0;

}

int should_exclude(struct AddressGenerator *addr_gen, uint32_t addr) {
    for(int i = 0; i < addr_gen->num_excluded_subnets; i++) {
        if((addr & addr_gen->exclude_masks[i]) == addr_gen->exclude_prefixes[i]) {
            return 1;
        }
    }
    return 0;
}

/* Get the next address to scan; returns zero if no more addresses available. */
in_addr_t next_address(struct AddressGenerator *addr_gen) {

    if(addr_gen->finished) {
        return 0;
    }

    // Iterate through values 0..2^32-1 using an LCG such that each value is visited exactly once
    do {
        addr_gen->state = addr_gen->state * 1664525 + 1013904223;
    } while(should_exclude(addr_gen, addr_gen->state));

    if(addr_gen->state == 0) {
        addr_gen->finished = true;
    }

    return htonl(addr_gen->state);

}