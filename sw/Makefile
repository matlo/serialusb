include Makedefs

BINS=serialusb

OBJECTS := $(patsubst %.c,%.o,$(shell find . -name "*.c"))

all: $(BINS)

serialusb: $(OBJECTS)

clean:
	$(RM) $(OBJECTS) $(BINS)
