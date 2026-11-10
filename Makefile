ifeq ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC non impostato")
endif

PORTLIBS := $(DEVKITPRO)/portlibs/ppc
LIBOGC   := $(DEVKITPRO)/libogc

CC      := $(DEVKITPPC)/bin/powerpc-eabi-gcc
MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float

CFLAGS  := -O2 -Wall $(MACHDEP) -I$(LIBOGC)/include -I$(PORTLIBS)/include -I$(PORTLIBS)/include/freetype2
LDFLAGS := $(MACHDEP) -L$(LIBOGC)/lib/wii -L$(PORTLIBS)/lib
LIBS    := -lgrrlib -lpng -ljpeg -lfreetype -lbz2 -lz -lfat -lwiiuse -lbte -logc -lm

all: boot.dol

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

boot.elf: main.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

boot.dol: boot.elf
	$(DEVKITPPC)/bin/elf2dol boot.elf boot.dol || $(DEVKITPRO)/tools/bin/elf2dol boot.elf boot.dol

clean:
	rm -f *.o *.elf *.dol
