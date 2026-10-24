#ifndef FUSEX_MEMORY_H
#define FUSEX_MEMORY_H

#include <stdint.h>

uint8_t fusex_peek( uint16_t addr );
void fusex_poke( uint16_t addr, uint8_t value );

#endif
