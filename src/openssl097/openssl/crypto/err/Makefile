#
# OpenSSL/crypto/err/Makefile
#

DIR=	err
TOP=	../..
CC=	cc
INCLUDES= -I.. -I$(TOP) -I../../include
CFLAG=-g
INSTALL_PREFIX=
OPENSSLDIR=     /usr/local/ssl
INSTALLTOP=/usr/local/ssl
MAKEDEPPROG=	makedepend
MAKEDEPEND=	$(TOP)/util/domd $(TOP) -MD $(MAKEDEPPROG)
MAKEFILE=	Makefile
AR=		ar r

CFLAGS= $(INCLUDES) $(CFLAG)

GENERAL=Makefile
TEST=
APPS=

LIB=$(TOP)/libcrypto.a
LIBSRC=err.c err_all.c err_prn.c
LIBOBJ=err.o err_all.o err_prn.o

SRC= $(LIBSRC)

EXHEADER= err.h
HEADER=	$(EXHEADER)

ALL=    $(GENERAL) $(SRC) $(HEADER)

top:
	(cd ../..; $(MAKE) DIRS=crypto SDIRS=$(DIR) sub_all)

all:	lib

lib:	$(LIBOBJ)
	$(AR) $(LIB) $(LIBOBJ)
	$(RANLIB) $(LIB) || echo Never mind.
	@touch lib

files:
	$(PERL) $(TOP)/util/files.pl Makefile >> $(TOP)/MINFO

links:
	@$(PERL) $(TOP)/util/mklink.pl ../../include/openssl $(EXHEADER)
	@$(PERL) $(TOP)/util/mklink.pl ../../test $(TEST)
	@$(PERL) $(TOP)/util/mklink.pl ../../apps $(APPS)

install:
	@headerlist="$(EXHEADER)"; for i in $$headerlist ; \
	do  \
	(cp $$i $(INSTALL_PREFIX)$(INSTALLTOP)/include/openssl/$$i; \
	chmod 644 $(INSTALL_PREFIX)$(INSTALLTOP)/include/openssl/$$i ); \
	done;

tags:
	ctags $(SRC)

tests:

lint:
	lint -DLINT $(INCLUDES) $(SRC)>fluff

depend:
	$(MAKEDEPEND) -- $(CFLAG) $(INCLUDES) $(DEPFLAG) -- $(PROGS) $(LIBSRC)

dclean:
	$(PERL) -pe 'if (/^# DO NOT DELETE THIS LINE/) {print; exit(0);}' $(MAKEFILE) >Makefile.new
	mv -f Makefile.new $(MAKEFILE)

clean:
	rm -f *.o *.obj lib tags core .pure .nfs* *.old *.bak fluff

# DO NOT DELETE THIS LINE -- make depend depends on it.

err.o: ../../e_os.h ../../include/openssl/bio.h ../../include/openssl/buffer.h
err.o: ../../include/openssl/crypto.h ../../include/openssl/e_os2.h
err.o: ../../include/openssl/err.h ../../include/openssl/lhash.h
err.o: ../../include/openssl/opensslconf.h ../../include/openssl/opensslv.h
err.o: ../../include/openssl/safestack.h ../../include/openssl/stack.h
err.o: ../../include/openssl/symhacks.h ../cryptlib.h err.c
err_all.o: ../../include/openssl/aes.h ../../include/openssl/asn1.h
err_all.o: ../../include/openssl/bio.h ../../include/openssl/blowfish.h
err_all.o: ../../include/openssl/bn.h ../../include/openssl/buffer.h
err_all.o: ../../include/openssl/cast.h ../../include/openssl/conf.h
err_all.o: ../../include/openssl/crypto.h ../../include/openssl/des.h
err_all.o: ../../include/openssl/des_old.h ../../include/openssl/dh.h
err_all.o: ../../include/openssl/dsa.h ../../include/openssl/dso.h
err_all.o: ../../include/openssl/e_os2.h ../../include/openssl/ec.h
err_all.o: ../../include/openssl/engine.h ../../include/openssl/err.h
err_all.o: ../../include/openssl/evp.h ../../include/openssl/fips.h
err_all.o: ../../include/openssl/idea.h ../../include/openssl/lhash.h
err_all.o: ../../include/openssl/md2.h ../../include/openssl/md4.h
err_all.o: ../../include/openssl/md5.h ../../include/openssl/mdc2.h
err_all.o: ../../include/openssl/obj_mac.h ../../include/openssl/objects.h
err_all.o: ../../include/openssl/ocsp.h ../../include/openssl/opensslconf.h
err_all.o: ../../include/openssl/opensslv.h ../../include/openssl/ossl_typ.h
err_all.o: ../../include/openssl/pem2.h ../../include/openssl/pkcs12.h
err_all.o: ../../include/openssl/pkcs7.h ../../include/openssl/rand.h
err_all.o: ../../include/openssl/rc2.h ../../include/openssl/rc4.h
err_all.o: ../../include/openssl/rc5.h ../../include/openssl/ripemd.h
err_all.o: ../../include/openssl/rsa.h ../../include/openssl/safestack.h
err_all.o: ../../include/openssl/sha.h ../../include/openssl/stack.h
err_all.o: ../../include/openssl/symhacks.h ../../include/openssl/ui.h
err_all.o: ../../include/openssl/ui_compat.h ../../include/openssl/x509.h
err_all.o: ../../include/openssl/x509_vfy.h ../../include/openssl/x509v3.h
err_all.o: err_all.c
err_prn.o: ../../e_os.h ../../include/openssl/bio.h
err_prn.o: ../../include/openssl/buffer.h ../../include/openssl/crypto.h
err_prn.o: ../../include/openssl/e_os2.h ../../include/openssl/err.h
err_prn.o: ../../include/openssl/lhash.h ../../include/openssl/opensslconf.h
err_prn.o: ../../include/openssl/opensslv.h ../../include/openssl/safestack.h
err_prn.o: ../../include/openssl/stack.h ../../include/openssl/symhacks.h
err_prn.o: ../cryptlib.h err_prn.c
