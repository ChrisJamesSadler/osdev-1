LIBRARY-NAME := libc.ell
G-LIBRARY-NAME := libg.ell
STATIC-NAME := libc.a
G-STATIC-NAME := libg.a
STARTUP-OBJS := sys/btos/crt0.o sys/btos/crti.o sys/btos/crtn.o

DESTINATION := $(HOME)/Projects/os/cross/i686-pc-btos

BUILD-AS := i686-pc-btos-gcc
BUILD-AR := i686-pc-btos-ar
BUILD-AS-OPTIONS := -g -c -fPIC -D_I386MACH_NEED_SOTYPE_FUNCTION
AR-FLAGS := rcs -o $(STATIC-NAME)
BUILD-C := i686-pc-btos-gcc
BUILD-C-FLAGS := -c -fPIC -DINTERNAL_NEWLIB -D_COMPILING_NEWLIB -DABORT_MESSAGE -DMALLOC_PROVIDED -ffreestanding
BUILD-C-OPTIONS := -Wall -Wextra -g -c -I../../../../include -I./sys/btos -I./include -std=gnu99
LINK := i686-pc-btos-gcc
LINK-FLAGS := -O2 -fPIC -shared -nodefaultlibs -ffreestanding -lm -lgcc_s -lbtoscore
LINK-OPTIONS := -o $(LIBRARY-NAME)
G-LINK-OPTIONS := -L. -lc -o $(G-LIBRARY-NAME)

FOLDERS := argz ctype errno locale machine reent search signal stdio stdlib string sys syscalls time
SKIP-FILES := signal/signal.c stdlib/getenv.c syscalls/syslseek.c syscalls/syswait.c syscalls/sysunlink.c syscalls/sysopen.c \
	syscalls/sysgetpid.c syscalls/sysgettod.c syscalls/sysfcntl.c syscalls/sysfork.c syscalls/syskill.c syscalls/syswrite.c \
	syscalls/sysstat.c syscalls/syssbrk.c syscalls/sysisatty.c syscalls/sysexecve.c syscalls/sysclose.c syscalls/syslink.c \
	syscalls/sysread.c syscalls/sysfstat.c syscalls/systimes.c string/memcpy.c string/strlen.c string/memcmp.c string/memmove.c \
	string/memset.c string/memchr.c string/strchr.c sys/btos/crti.S sys/btos/crt0.S sys/btos/crtn.S reent/sbrkr.c
SKIP-SPEC := $(shell printf "! -wholename %s " $(SKIP-FILES))

ASFILES := $(shell find $(FOLDERS) -type f -name "*.s" -or -name "*.S" $(SKIP-SPEC))
CFILES := $(shell find $(FOLDERS) -type f -name "*.c" $(SKIP-SPEC))

INCLUDE-FOLDERS := include sys/btos

ASOBJS1 := $(ASFILES:.s=.o)
ASOBJS2 := $(ASOBJS1:.S=.o)
ASOBJS := $(ASOBJS2)
COBJS := $(CFILES:.c=.o)

EXTRAOBJS := stdio/vfiprintf.o stdio/svfprintf.o stdio/svfiprintf.o stdio/vfiwprintf.o stdio/svfwprintf.o stdio/svfiwprintf.o stdio/vfiscanf.o stdio/svfscanf.o stdio/svfiscanf.o stdio/vfiwscanf.o stdio/svfwscanf.o stdio/svfiwscanf.o

CDEPFILES := $(patsubst %.c,%.d,$(CFILES))

all: copy

startfiles: copy-startfiles

copy-includes: 
	mkdir -p $(DESTINATION)/include
	for folder in $(INCLUDE-FOLDERS); do \
#		rsync --checksum -r $$folder/*.h $(DESTINATION)/include \
		(cd $$folder && find . -type d -exec rsync -r --include '*/' --include '*.h' --exclude '*' --checksum {}/ $(DESTINATION)/include/{} \;); \
	done

-include $(CDEPFILES) $(CPPDEPFILES)

copy: $(LIBRARY-NAME) $(STATIC-NAME) $(STARTUP-OBJS) $(G-LIBRARY-NAME) copy-includes
	mkdir -p $(DESTINATION)/lib
	cp -u $(LIBRARY-NAME) $(DESTINATION)/lib
	cp -u $(STATIC-NAME) $(DESTINATION)/lib
	cp -u $(G-LIBRARY-NAME) $(DESTINATION)/lib/
	cp -u $(STATIC-NAME) $(DESTINATION)/lib/$(G-STATIC-NAME)

copy-startfiles: $(STARTUP-OBJS)
	for file in $(STARTUP-OBJS); do cp $$file $(DESTINATION)/lib; done

clean: dummy.delete

dummy.delete:
	-@find . -name \*.o -delete
	-@$(RM) $(LIBRARY-NAME)
	-@$(RM) $(STATIC-NAME)

$(LIBRARY-NAME): $(ASOBJS) $(COBJS) $(EXTRAOBJS) Makefile
	$(LINK) $(LINK-FLAGS) $(ASOBJS) $(COBJS) $(EXTRAOBJS) $(LINK-OPTIONS)
	
$(G-LIBRARY-NAME): $(LIBRARY-NAME) Makefile
	$(LINK) $(LINK-FLAGS) $(G-LINK-OPTIONS)

$(STATIC-NAME): $(ASOBJS) $(COBJS) $(EXTRAOBJS) Makefile
	$(BUILD-AR) $(AR-FLAGS) $(ASOBJS) $(COBJS) $(EXTRAOBJS)

.s.o: %.s Makefile
	$(BUILD-AS) $< -o $@ $(BUILD-AS-OPTIONS)

.S.o: %.S Makefile
	$(BUILD-AS) $< -o $@ $(BUILD-AS-OPTIONS)

.c.o: %.c Makefile
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/vfiprintf.o: stdio/vfprintf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DINTEGER_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/svfprintf.o: stdio/vfprintf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DSTRING_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/svfiprintf.o: stdio/vfprintf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DSTRING_ONLY -DINTEGER_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/vfiwprintf.o: stdio/vfwprintf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DINTEGER_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/svfwprintf.o: stdio/vfwprintf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DSTRING_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/svfiwprintf.o: stdio/vfwprintf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DSTRING_ONLY -DINTEGER_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/vfiscanf.o: stdio/vfscanf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DINTEGER_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/svfscanf.o: stdio/vfscanf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DSTRING_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/svfiscanf.o: stdio/vfscanf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DSTRING_ONLY -DINTEGER_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/vfiwscanf.o: stdio/vfwscanf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DINTEGER_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/svfwscanf.o: stdio/vfwscanf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DSTRING_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common

stdio/svfiwscanf.o: stdio/vfwscanf.c
	$(BUILD-C) -MMD -MP $(BUILD-C-FLAGS) -DSTRING_ONLY -DINTEGER_ONLY $< -o $@ $(BUILD-C-OPTIONS) -I$(shell dirname $<) -I../libm/common
