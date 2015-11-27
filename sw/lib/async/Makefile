include ../../Makedefs

LIBNAME=$(shell basename $(shell pwd)).a
ifneq ($(OS),Windows_NT)
OBJECTS := $(patsubst %.c,%.o,$(wildcard src/linux/*.c))
else
OBJECTS := $(patsubst %.c,%.o,$(wildcard src/windows/*.c))
endif
OBJECTS+=$(patsubst %.c,%.o,$(wildcard src/*.c))
CPPFLAGS+=-Iinclude -Isrc

all: $(LIBNAME)

$(LIBNAME): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $?

clean:
	$(RM) $(LIBNAME) $(OBJECTS)
