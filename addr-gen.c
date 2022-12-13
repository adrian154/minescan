#include "addr-gen.h"

#define ADDRGEN_INITIAL_STATE 0

// Each excluded subnet is encoded as the address followed by the subnet mask.
const uint32_t excluded_subnets[] = {
    0, 4278190080,
    167772160, 4278190080,
    2886729728, 4293918720,
    3232235520, 4294901760,
    1681915904, 4290772992,
    2130706432, 4278190080,
    2851995648, 4294901760,
    3221225472, 4294967040,
    3221225984, 4294967040,
    3325256704, 4294967040,
    3405803776, 4294967040,
    3227017984, 4294967040,
    3323068416, 4294836224,
    4026531840, 4026531840,
    4294967295, 0
};

void init_addrgen(struct AddressGenerator *addr_gen) {
    addr_gen->finished = false;
    addr_gen->state = ADDRGEN_INITIAL_STATE;
}

int should_exclude(uint32_t addr) {
    for(int i = 0; i < 15; i += 2) {
        if((addr & excluded_subnets[i + 1]) == excluded_subnets[i]) {
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

    do {

        /* We iterate through values {0..2^32-1} using an LCG; the parameters
           are chosen so that each value will be visited exactly once. */
        addr_gen->state = addr_gen->state * 1664525 + 1013904223;
    
    } while(should_exclude(addr_gen->state));

    if(addr_gen->state == ADDRGEN_INITIAL_STATE) {
        addr_gen->finished = true;
    }

    return htonl(addr_gen->state);

}