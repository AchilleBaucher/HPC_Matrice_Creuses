# your choice of compiler
CC = mpicc

# Add your choice of flags
CFLAGS = -O3 -Wall -Wextra -g -fopenmp
LDLIBS = -lm -fopenmp

all : cg

cg : cg.o mmio.o
mmio.o : mmio.c mmio.h
cg.o : cg.c mmio.h

cg_fusion :
	cp cg_fusion.c cg.c

cg_omp :
	cp cg_omp.c cg.c

cg_mpi :
	cp cg_mpi.c cg.c

fusion : cg_fusion cg

mpi : cg_mpi cg

omp : cg_omp cg

.PHONY: clean
clean :
	rm -rf *.o cg
