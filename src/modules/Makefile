MODULES := boot test rtc ps2 ata fat terminal pci vga iso9660

all: dummy.file
	for dir in $(MODULES); do \
		make -C $$dir; \
	done

clean: dummy.file
	for dir in $(MODULES); do \
		make -C $$dir clean; \
	done

dummy.file: