ARCH ?= $(shell uname -m)

SRC_DIR = src
INC_DIR = include
SRCS    = $(SRC_DIR)/caffeine.c \
          $(SRC_DIR)/worker.c \
          $(SRC_DIR)/daemon.c \
          $(SRC_DIR)/log.c \
          $(SRC_DIR)/caffeine_cfg.c \
          $(SRC_DIR)/caffeine_sig.c \
          $(SRC_DIR)/caffeine_utils.c \
          $(SRC_DIR)/list_instances.c \
          $(SRC_DIR)/headers.c \
          $(SRC_DIR)/deploy.c \
          $(SRC_DIR)/cJSON.c \
          $(SRC_DIR)/cJSON_Utils.c \
          $(SRC_DIR)/server_monitor.c \
          $(SRC_DIR)/shared_mem.c

ifeq ($(ARCH),x86_64)
    CC = gcc
    TARGET = bin/caffeine-x86_64
else ifeq ($(ARCH),aarch64)
    CC = aarch64-linux-gnu-gcc
    TARGET = bin/caffeine-aarch64
else ifeq ($(ARCH),arm64)
    CC = aarch64-linux-gnu-gcc
    TARGET = bin/caffeine-aarch64
else ifeq ($(ARCH),arm)
    CC = arm-linux-gnueabihf-gcc
    TARGET = bin/caffeine-arm
else
    CC = gcc
    TARGET = bin/caffeine-$(ARCH)
endif

CFLAGS  = -Wall -Wextra -O2 -I$(INC_DIR)
LDFLAGS = -pthread

OBJ_DIR = build/$(ARCH)
OBJS    = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

PREFIX ?= /usr/local

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p bin
	@echo "Linking executable for $(ARCH): $@..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	@echo "Compiling $(ARCH) object: $<..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up build and binary files..."
	rm -rf build/ bin/

install: $(TARGET)
	@echo "Installing $(TARGET) to $(PREFIX)/bin..."
	install -d $(PREFIX)/bin
	install -m 0755 $(TARGET) $(PREFIX)/bin/caffeine