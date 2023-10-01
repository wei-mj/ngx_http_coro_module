CXXFLAGS=-fPIC -pipe -O -g -std=c++20

ARFLAGS=rcs

LINK=$(CXX)

all: libco_comm.a workflow1.so

libco_comm.a: co_scheduler.o co_scheduler_impl.o co_comm.o
	$(AR) $(ARFLAGS) $@ $^

workflow1.so: workflow1.o
	$(LINK) -shared -o $@ $^

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o
