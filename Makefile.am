ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = workshare

workshare_SOURCES = \
	src/task.c \
	src/threads.c \
	src/proactor.c
			
if WIN32
workshare_CPPFLAGS = -D_WIN32_WINNT=_WIN32_WINNT_WIN7
workshare_LDADD = -lws2_32
else
workshare_CFLAGS = $(PTHREAD_CFLAGS)
workshare_LDFLAGS = $(PTHREAD_LIBS)
workshare_LDADD = -lm 
endif
