CC = gcc
CFLAGS = -Wall -Wextra -g -pthread
LDFLAGS = -lX11 -pthread
SRCS = proj.c task1.c task2.c task3.c task4.c task5.c task6.c task7.c task8.c task9.c task10.c task11.c
OBJS = $(SRCS:.c=.o)

terminator: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) terminator
