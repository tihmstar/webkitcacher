AM_CFLAGS = $(libgeneral_CFLAGS) $(sqlite3_CFLAGS)
AM_LDFLAGS = $(libgeneral_LIBS) $(sqlite3_LIBS)

bin_PROGRAMS = webkitcacher
webkitcacher_CXXFLAGS = $(AM_CXXFLAGS) $(AM_CFLAGS)
webkitcacher_LDADD = $(AM_LDFLAGS)
webkitcacher_SOURCES =  main.cpp \
												webkitCacher.cpp
