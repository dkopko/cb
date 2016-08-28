
CFLAGS=-Wall -Wno-unused-function -g3 -ggdb3 -gdwarf-4 -mtune=native
CFLAGS+=-D_GNU_SOURCE
CFLAGS+=-O2
CFLAGS+=-DNDEBUG=1
#CFLAGS+=-DCB_HEAVY_ASSERT
#CFLAGS+=-DCB_VERBOSE


CXXFLAGS+=$(CFLAGS)

OBJS=cb.o \
	 cb_interpreter.o \
	 cb_lb_set.o \
	 cb_map.o \
	 cb_random.o \
	 test_lb_set.o \
	 test_measure.o \
	 test_measure_ll.o \
	 test_measure_ll_f.o

EXES=main \
	 test_lb_set \
	 test_measure \
	 test_measure_ll \
	 test_interpreter \
	 test_structmap \
	 test_misc

.PHONY : all clean

all : $(EXES)

cb.o : cb.h

cb_lb_set.o : cb.h cb_lb_set.h

cb_map.o : cb.h cb_map.h

cb_structmap.o : cb.h cb_structmap.h cb_bits.h

main : cb.o cb_map.o

test_lb_set : test_lb_set.o cb.o cb_lb_set.o cb_random.o
	$(CXX) $(LDFLAGS) -o $@ $^

test_misc: test_misc.o

test_measure : test_measure.o cb.o cb_map.o cb_random.o
	$(CXX) $(LDFLAGS) -o $@ $^

test_measure_ll : test_measure_ll.o test_measure_ll_f.o

test_interpreter : test_interpreter.o cb_interpreter.o

test_structmap : test_structmap.o cb.o cb_structmap.o

clean :
	rm -f $(EXES) $(OBJS) map-*-*

