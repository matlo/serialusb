include Makedefs

prefix=$(DESTDIR)/usr
bindir=$(prefix)/bin

BINS=serialusb
SCRIPTS=serialusb-launcher.sh

OBJECTS := $(patsubst %.c,%.o,$(shell find . -name "*.c"))

all: $(BINS)

serialusb: $(OBJECTS)

clean:
	$(RM) $(OBJECTS) $(BINS)

install: all
	mkdir -p $(prefix)
	mkdir -p $(bindir)
	for i in $(BINS); do cp $$i $(bindir)/; done
	for i in $(SCRIPTS); do cp $$i $(bindir)/; done

uninstall:
	-for i in $(SCRIPTS); do $(RM) $(bindir)/$$i; done
	-for i in $(BINS); do $(RM) $(bindir)/$$i; done
	-$(RM) $(bindir)
	-$(RM) $(prefix)

really-clean: clean uninstall
