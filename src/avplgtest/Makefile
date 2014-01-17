CC = gcc
CFLAGS += -Wall -pedantic

ifdef DEBUG
CFLAGS += -g -O0
endif

BIN_NAME := avplgtest
BIN_OBJS := avplgtest.o
BIN_SRCS := avplgtest.c
BIN_DIR ?= /usr/bin
INCLUDE ?= -I../libavplg
DEP_FILE := .deps
AVLIB ?= ${CURDIR}/../libavplg/libavplg.a


.PHONY: all install uninstall clean

all: $(BIN_NAME)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $<

$(BIN_NAME): $(BIN_OBJS)
	$(CC) -o $(BIN_NAME) $(BIN_OBJS) $(AVLIB) -lpthread

install: $(BIN_NAME)
	cp $(BIN_NAME) $(BIN_DIR)/$(BIN_NAME)

uninstall:
	$(RM) $(BIN_DIR)/$(BIN_NAME)

clean:
	$(RM) $(BIN_NAME) $(BIN_OBJS) $(DEP_FILE)

-include $(DEP_FILE)

$(DEP_FILE): $(BIN_SRCS)
	$(CC) -M -MF $@ $(INCLUDE) $(BIN_SRCS)

