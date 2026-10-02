#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>

#define SZ 2
#define FEATURE_STR "l<target version=\"1.0\">"\
    "<feature name=\"org.gnu.gdb.z80.cpu\">"\
    "<reg name=\"af\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"bc\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"de\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"hl\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"sp\" bitsize=\"16\" type=\"data_ptr\"/>"\
    "<reg name=\"pc\" bitsize=\"16\" type=\"code_ptr\"/>"\
    "<reg name=\"ix\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"iy\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"af'\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"bc'\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"de'\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"hl'\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"clockl\" bitsize=\"16\" type=\"int\"/>"\
    "<reg name=\"clockh\" bitsize=\"16\" type=\"int\"/>"\
    "</feature>"\
    "<architecture>z80</architecture>"\
    "</target>"

#endif /* ARCH_H */
