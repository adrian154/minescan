#ifndef __ADDR_GEN_H
#define __ADDR_GEN_H

#include <arpa/inet.h>
#include <stdint.h>

struct AddressGenerator {
    uint32_t state;
};

in_addr_t next_address(struct AddressGenerator *addr_gen);

#endif