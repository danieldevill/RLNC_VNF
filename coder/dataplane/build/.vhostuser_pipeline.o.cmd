cmd_vhostuser_pipeline.o = gcc -Wp,-MD,./.vhostuser_pipeline.o.d.tmp  -m64 -pthread  -march=native -DRTE_MACHINE_CPUFLAG_SSE -DRTE_MACHINE_CPUFLAG_SSE2 -DRTE_MACHINE_CPUFLAG_SSE3 -DRTE_MACHINE_CPUFLAG_SSSE3 -DRTE_MACHINE_CPUFLAG_SSE4_1 -DRTE_MACHINE_CPUFLAG_SSE4_2 -DRTE_MACHINE_CPUFLAG_AES -DRTE_MACHINE_CPUFLAG_PCLMULQDQ -DRTE_MACHINE_CPUFLAG_AVX  -I/home/switch/dataplane/build/include -I/usr/src/dpdk-18.11/x86_64-native-linuxapp-gcc/include -include /usr/src/dpdk-18.11/x86_64-native-linuxapp-gcc/include/rte_config.h -D_GNU_SOURCE -I/home/switch/kodo-c/shared_kodoc/include -O0 -g -W -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wold-style-definition -Wpointer-arith -Wcast-align -Wnested-externs -Wcast-qual -Wformat-nonliteral -Wformat-security -Wundef -Wwrite-strings -Wdeprecated -Wimplicit-fallthrough=2 -Wno-format-truncation    -o vhostuser_pipeline.o -c /home/switch/dataplane/vhostuser_pipeline.c 
