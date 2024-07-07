export OBJ = .obj

CFLAGS += --std=c++20

.PHONY: all
all: +all
	
include build/ab.mk
