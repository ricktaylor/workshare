ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = workshare

#workshare_CFLAGS = $(PTHREAD_CFLAGS)
#workshare_LDFLAGS = $(PTHREAD_LIBS)

workshare_SOURCES = \
	src/test.c \
	src/task.c \
	src/threads.c \
	src/proactor.c \
	src/parallel_for.c \
	src/parallel_sort.c
			
#thehouse_LDADD += -lglfw -lGL

if WIN32
workshare_CPPFLAGS = -D_WIN32_WINNT=_WIN32_WINNT_WIN7
workshare_LDADD = -lws2_32
endif
