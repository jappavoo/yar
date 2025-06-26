SRCS       := main.c tty.c cmd.c hexdump.c
OBJS       := $(SRCS:%.c=%.o)
O          :=0
CFLAGS     += -g -O${O} -std=gnu99 -MD -MP -Wall -Werror
TARGETS    := yar
UTHASHREPO := git@github.com:troydhanson/uthash.git
UTHASHDIR  := ext/uthash
UTHASHINCS  = ${UTHASHDIR}/include
TLPIURL    := https://man7.org/tlpi/code/download/tlpi-241221-dist.tar.gz
TLPITGZ    := tlpi-241221-dist.tar.gz
TLPIDIR    := ext/tlpi-dist
TLPIINCS    = ${TLPIDIR}
TLPILIBDIR  = ${TLPIDIR}
TLPILIB     = tlpi

CFLAGS     += -I${UTHASHINCS} -I${TLPIINCS}
ifeq ($(wildcard /usr/include/sys/pidfd.h),)
	CFLAGS += -DNOSYSPIDFD
endif


LDFLAGS    += -L${TLPILIBDIR} -l${TLPILIB}
EXTFILES    = ${UTHASHINCS}/uthash.h \
	${TLPIDIR}/lib${TLPILIB}.a

.PHONY: clean 

%.so: %.c
	${CC} ${CFLAGS} -fPIC -shared $< -o $@

all: ${EXTFILES} ${TARGETS} ${FUNCSOS}

yar: ${OBJS}
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS} 

${UTHASHINCS}/uthash.h:
	-mkdir -p ext && git clone ${UTHASHREPO} ${UTHASHDIR}

ext/${TLPITGZ}:
	-mkdir -p ext && wget ${TLPIURL} -O ext/${TLPITGZ}

ext/.tlpi.dir: ext/${TLPITGZ}
	cd ext && tar xf ${TLPITGZ} && touch .tlpi.dir

${TLPIDIR}/lib${TLPILIB}.a: ext/.tlpi.dir
	make -C ${TLPIDIR}
clean:
	-rm -rf $(wildcard *.o *.d ${TARGETS} *.so ext)

-include $(wildcard *.d)



