# Choose one or more back-ends Allowed values are
#	BE_CDB
#	BE_MYSQL
#	BE_SQLITE
#	BE_REDIS
#	BE_PG

BACKENDS=-DBE_PSK -DBE_CDB -DBE_MYSQL -DBE_SQLITE -DBE_REDIS -DBE_PG
BACKENDS=-DBE_MYSQL -DBE_PG

BE_CFLAGS=`mysql_config --cflags`
BE_LDFLAGS=`mysql_config --libs`
BE_DEPS=

# Flags for PostgreSQL
BE_CFLAGS += -I`pg_config --includedir` 
BE_LDFLAGS += -L`pg_config --libdir`

CDBDIR=contrib/tinycdb-0.78
CDB=$(CDBDIR)/cdb
CDBINC=$(CDBDIR)/
CDBLIB=$(CDBDIR)/libcdb.a
BE_CFLAGS += -I$(CDBINC)/
BE_LDFLAGS += -L$(CDBDIR) -lcdb
BE_DEPS += $(CDBLIB)

BE_LDFLAGS += -lsqlite3

BE_CFLAGS += -I/usr/include/hiredis
BE_LDFLAGS += -L/usr/lib -lhiredis

OPENSSLDIR=/usr/local/stow/openssl-1.0.0c/
OSSLINC=-I$(OPENSSLDIR)/include
OSSLIBS=-L$(OPENSSLDIR)/lib -lcrypto 



CFLAGS = -I../mosquitto-1.2.3/src/
CFLAGS += -I../mosquitto-1.2.3/lib/
CFLAGS += -fPIC -Wall -Werror $(BACKENDS) $(BE_CFLAGS) -I$(MOSQ)/src -DDEBUG=1 $(OSSLINC)
LDFLAGS=$(BE_LDFLAGS) -lmosquitto $(OSSLIBS)
LDFLAGS += -L../mosquitto-1.2.3/lib
# LDFLAGS += -Wl,-rpath,$(../../../../pubgit/MQTT/mosquitto/lib) -lc
# LDFLAGS += -export-dynamic

OBJS=auth-plug.o base64.o pbkdf2-check.o log.o hash.o be-psk.o be-cdb.o be-mysql.o be-pg.o be-sqlite.o be-redis.o

all: auth-plug.so np 

auth-plug.so : $(OBJS) $(BE_DEPS)
	$(CC) -fPIC -shared $(OBJS) -o $@  $(OSSLIBS) $(BE_DEPS) $(LDFLAGS) 

be-redis.o: be-redis.c be-redis.h log.h hash.h Makefile
be-sqlite.o: be-sqlite.c be-sqlite.h Makefile
auth-plug.o: auth-plug.c be-cdb.h be-mysql.h be-sqlite.h Makefile
be-psk.o: be-psk.c be-psk.h Makefile
be-cdb.o: be-cdb.c be-cdb.h Makefile
be-mysql.o: be-mysql.c be-mysql.h Makefile
be-sqlite.o: be-sqlite.c be-sqlite.h Makefile
be-pg.o: be-pg.c be-pg.h Makefile
pbkdf2-check.o: pbkdf2-check.c base64.h Makefile
base64.o: base64.c base64.h Makefile
log.o: log.c log.h Makefile
hash.o: hash.c hash.h uthash.h Makefile

np: np.c base64.o
	$(CC) $(CFLAGS) $^ -o $@ $(OSSLIBS)

$(CDBLIB):
	(cd $(CDBDIR); make libcdb.a cdb )

pwdb.cdb: pwdb.in
	$(CDB) -c -m  pwdb.cdb pwdb.in
clean :
	rm -f *.o *.so 
	(cd contrib/tinycdb-0.78; make realclean )
