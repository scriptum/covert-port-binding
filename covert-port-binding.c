/*******************************************************************************

Copyright (c) 2017 Dmitry Efanov, Pavel Roschin.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions: The above
copyright notice and this permission notice shall be included in all copies
or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.

Compile and run:

gcc -O2 covert-port-binding.c -o covert-port-binding
./covert-port-binding

Tested under CentOS 7.2.

*******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

#define STREAM_LEN 1024 * 8 * 128 /* test stream length (128 kilobytes) */
#define MAX_ADDR 16 /* transmit 16 bits per switch */
struct sockaddr_in addr[MAX_ADDR];;
char test_stream[STREAM_LEN];

static inline long double
tv2f(struct timeval *tv)
{
	return (long double)tv->tv_sec + (long double)tv->tv_usec / 1000000.L;
}

static void
error(const char *msg)
{
	perror(msg);
	exit(1);
}

static void
sender_handler(int sig)
{
	int i;
	static int counter = 0;
	static int sock_sender[MAX_ADDR] = {-1};

	for(i = 0; i < MAX_ADDR; i++)
	{
		if(sock_sender[i] > 0)
			close(sock_sender[i]);
		sock_sender[i] = -1;
		if (counter >= STREAM_LEN)
		{
			sched_yield();
			return;
		}
		/* if test stream contains non-zero bit, bind specified port */
		if(test_stream[counter])
		{
			sock_sender[i] = socket(AF_INET, SOCK_STREAM, 0);
			if(sock_sender[i] < 0) 
			{
				error("ERROR opening socket (sender)");
			}
			if(bind(sock_sender[i], (struct sockaddr *) &(addr[i]), sizeof(addr[i])) < 0)
			{
				error("ERROR on binding (sender)");
			}
		}
		counter++;
	}
	sched_yield();
}

static void
do_sender()
{
	signal(SIGUSR1, sender_handler);
	while(1) /* just do nothing */
		usleep(0);
}

static void
do_receiver(pid_t sender_pid)
{
	int i;
	int errors = 0;
	const int bits = STREAM_LEN;
	int sock_receiver;
	int counter = 0;
	char stream[bits];
	struct timeval tv;
	long double start_time, end_time, time;

	/* wait sender process to start
	 * if /proc/sys/kernel/sched_child_runs_first == 0, receiver runs first */
	usleep(1000);
	/* used only for speed measurement */
	gettimeofday(&tv, NULL);
	start_time = tv2f(&tv);

	while(counter < sizeof(stream))
	{
		kill(sender_pid, SIGUSR1);
		sched_yield();

		/* check if we can bind specified port. If we cant't, sender set a bit 1 */
		for(i = 0; i < MAX_ADDR; i++)
		{
			sock_receiver = socket(AF_INET, SOCK_STREAM, 0);
			if(sock_receiver < 0) 
			{
				error("ERROR opening socket (receiver)");
			}
			if(bind(sock_receiver, (struct sockaddr *) &addr[i], sizeof(addr[i])) < 0)
			{
				stream[counter] = 1;
			}
			else
			{
				stream[counter] = 0;
			}
			counter++;
			close(sock_receiver);
		}
	}

	gettimeofday(&tv, NULL);
	end_time = tv2f(&tv);
	time =  end_time - start_time;
	printf("%d bits in %Lf sec = %Lf bps\n", bits, time, bits/time);

	/* check validity of stream */
	for(i = 0; i < sizeof(stream); i++)
	{
		if(stream[i] != test_stream[i])
		{
			errors++;
		}
	}
	if(errors)
	{
		fprintf(stderr, "Wrong stream!\n");
		for(i = 0; i < 1024; i++)
		{
			if(i % 64 == 0)
				putc('\n', stderr);
			if(stream[i] != test_stream[i])
			{
				fprintf(stderr, "\033[1;31m");
			}
			fprintf(stderr, "%d", stream[i]);
			if(stream[i] != test_stream[i])
			{
				fprintf(stderr, "\033[0m");
			}
		}
		putc('\n', stderr);
	}
	printf("Errors: %d, noise: %d%%\n", errors, errors * 100 / bits);
	kill(sender_pid, SIGKILL);
}

/* Use same CPU for sender and receiver */
void set_cpu()
{
	cpu_set_t my_set;
	CPU_ZERO(&my_set);   /* Initialize it all to 0, i.e. no CPUs selected */
	CPU_SET(0, &my_set); /* set the bit that represents core 0 */

	/* use only first CPU in system to avoid multi-core architecture impact */
	sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
}

int main(int argc, char *argv[])
{
	/* pick a random port */
	const int port = 63964;
	pid_t sender_pid;
	int i, status;

	/* initialize socket addresses */
	for(i = 0; i < MAX_ADDR; i++)
	{
		bzero((char *) &addr[i], sizeof(addr[i]));
		addr[i].sin_family = AF_INET;
		addr[i].sin_addr.s_addr = htons(INADDR_LOOPBACK + i);
		addr[i].sin_port = htons(port);
	}

	/* generate test stream */
	for(i = 0; i < STREAM_LEN; i++)
	{
		test_stream[i] = rand() & 1;
	}
	/* using fork to simplify demonstration (all-in-one) */
	switch(sender_pid = fork())
	{
		case -1:
			error("ERROR cannot fork");
		case 0:
			set_cpu();
			do_sender();
		default:
			set_cpu();
			do_receiver(sender_pid);
			wait(&status);
	}

	return 0; 
}
