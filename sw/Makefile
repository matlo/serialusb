include Makedefs

BINS=serialusb

OBJECTS := $(patsubst %.c,%.o,$(shell find . -name "*.c"))
OBJECTS:=$(filter-out $(patsubst %.c,%.o,$(shell find . -regex ".*/windows/.*\.c")),$(OBJECTS))

all: $(BINS)

serialusb: $(OBJECTS)

clean:
	$(RM) $(OBJECTS) $(BINS)
