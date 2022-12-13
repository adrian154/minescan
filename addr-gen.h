#ifndef __ADDR_GEN_H
#define __ADDR_GEN_H

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>

struct AddressGenerator {
    uint32_t state;
    bool finished;
};

int should_exclude(uint32_t addr);
in_addr_t next_address(struct AddressGenerator *addr_gen);

#endif