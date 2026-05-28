CC      = gcc
BIN    := memvault
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g -Isrc
SRCDIR  = src
SRCS    = $(SRCDIR)/main.c \
          $(SRCDIR)/server.c \
          $(SRCDIR)/client.c \
          $(SRCDIR)/commands.c \
          $(SRCDIR)/kvstore.c \
          $(SRCDIR)/kvhash.c \
          $(SRCDIR)/kvlist.c \
          $(SRCDIR)/object.c \
          $(SRCDIR)/resp.c \
          $(SRCDIR)/ttl.c \
          $(SRCDIR)/eviction.c \
          $(SRCDIR)/aof.c \
          $(SRCDIR)/util.c
OBJS    = $(SRCS:.c=.o)

ifeq ($(OS),Windows_NT)
  TARGET  = $(BIN).exe
  LDFLAGS = -lws2_32
else
  TARGET  = $(BIN)
  LDFLAGS =
endif

RM_CMD  = rm -f
OBJ_PAT = $(SRCDIR)/*.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-$(RM_CMD) $(OBJ_PAT) $(TARGET)

.PHONY: all clean
