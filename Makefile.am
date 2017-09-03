ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = workshare

workshare_SOURCES = \
	src/test.c \
	src/task.c \
	src/threads.c \
	src/proactor.c \
	src/parallel_for.c \
	src/merge_sort.c \
	src/bitonic_sort.c
			
if WIN32
workshare_CPPFLAGS = -D_WIN32_WINNT=_WIN32_WINNT_WIN7
workshare_LDADD = -lws2_32
else
workshare_CFLAGS = $(PTHREAD_CFLAGS)
workshare_LDFLAGS = $(PTHREAD_LIBS)
workshare_LDADD = -lm 
endif
