/* 
2019-20 Programming Project Part1B

File name:	part1b-mandelbrot.c 
Name:		Chan Tik Shun
Student ID:	3035536553
Date: 		1/11/2019 
Version: 	1.1
Platform:	X2GO (Xfce 4.12, distributed by Xubuntu)
Compilation:	gcc part1b-mandelbrot.c -o part1b-mandelbrot -l SDL2 -l m
*/

//Using SDL2 and standard IO
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#include <unistd.h>
#include "Mandel.h"
#include "draw.h"

int data_pipe[2]; //create data pipe
int task_pipe[2]; //create task pipe

typedef struct task {
	int start_row;
	int num_of_rows;
} TASK;

typedef struct message {
	int row_index;
	pid_t child_pid;
	float rowdata[IMAGE_WIDTH];
} MSG;

int task_completed = 0; //global variable for child
void sigusr1_handler(int signum) { //signal handler for SIGUSR1

	int x, y;
	struct timespec child_start_compute, child_end_compute;

	TASK * curr_task = malloc(sizeof(*curr_task));

	read(task_pipe[0], curr_task, sizeof(*curr_task)); //read from task pipe to get a task

	printf("Child (%d): Start the computation...\n", (int)getpid());

	clock_gettime(CLOCK_MONOTONIC, &child_start_compute); //record start time

	//computation
	for (y=curr_task->start_row; y<(curr_task->start_row + curr_task->num_of_rows); y++) {
		MSG * curr_msg = malloc(sizeof(*curr_msg));
		curr_msg->row_index = y; //save row index
    		for (x=0; x<IMAGE_WIDTH; x++) 
			curr_msg->rowdata[x] = Mandelbrot(x, y); //compute a value for (x, y)

		if(y+1==curr_task->start_row+curr_task->num_of_rows) //last row,
			curr_msg->child_pid = getpid(); //inform boss for task
		else //more message to come
			curr_msg->child_pid = -1; //don't inform boss for task

		write(data_pipe[1], curr_msg, sizeof(*curr_msg)); //write to data pipe
    	}

	//report compute timing
	clock_gettime(CLOCK_MONOTONIC, &child_end_compute);
	float child_difftime = (child_end_compute.tv_nsec - child_start_compute.tv_nsec)/1000000.0 + (child_end_compute.tv_sec - child_start_compute.tv_sec)*1000.0;
	printf("Child (%d): ... completed. Elapsed time = %.3f ms\n", (int)getpid(), child_difftime);
	task_completed++; //increases the number of task completed by this process
	return;
}

void sigint_handler(int signum) { //signal handler for SIGINT
	close(data_pipe[1]); //close write end of data pipe of child
	close(task_pipe[0]); //close read end of task pipe of child	
	printf("Process %d is interrupted by ^C. Bye Bye\n", (int)getpid());
	printf("Child process %d terminated and completed %d tasks\n", (int)getpid(), task_completed);
	exit(0);
}

int main( int argc, char* args[] )
{
	pipe(data_pipe); //set data pipe
	pipe(task_pipe); //set task pipe

	signal(SIGINT, sigint_handler); //override SIGINT handler
	signal(SIGUSR1, sigusr1_handler); //override SIGUSR1 handler

	int * children = (int*)malloc(sizeof(int) * atoi(args[1])); //for storing child pid

	if (argc != 3) { //not enough/too many arguments
		printf("Invalid argument!\n");
		printf("Usage: ./part1b-mandelbrot <number of child> <number of rows in a task>\n");
		exit(0);
	}

	if (atoi(args[1])>IMAGE_HEIGHT || atoi(args[1]) < 1) { // arguments not within 1~800
		printf("Number of process is not within 1 to %d, please try again.\n", IMAGE_HEIGHT);
		exit(0);
	} else if (atoi(args[2])>IMAGE_HEIGHT || atoi(args[2]) < 1) {
		printf("Number of line is not within 1 to %d, please try again.\n", IMAGE_HEIGHT);
		exit(0);
	}

	if(atoi(args[1])*atoi(args[2])>IMAGE_HEIGHT) {
		printf("Number of process times number of line is larger than %d, please try again.\n", IMAGE_HEIGHT);
		exit(0);
	}

	//data structure to store the start and end times of the whole program
	struct timespec start_time, end_time;
	//get the start time
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	//data structure to store the start and end times of the computation
	struct timespec start_compute, end_compute;
	
	//store the 2D image as a linear array of pixels (in row-major format)
	float * pixels;
	
	//allocate memory to store the pixels
	pixels = (float *) malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);
	if (pixels == NULL) {
		printf("Out of memory!!\n");
		exit(1);
	}
	
	clock_gettime(CLOCK_MONOTONIC, &start_compute);
    	int x, y;
	float difftime, child_difftime;
	
	int rows_to_complete = atoi(args[2]);
	int i, pid;

	//create childs
	for (i=0; i<atoi(args[1]); i++) { 
		pid = fork();
		if(pid == 0) //child
			break; 
		else //parent
			children[i] = pid; //record child process pid
	}
	
	if(pid > 0) { //==============parent==============
		int curr_row = 0;

		close(data_pipe[1]); //close write end of data pipe of parent
		close(task_pipe[0]); //close read end of task pipe of parent	

		printf("Start collecting the image lines\n");

		//distribute a task to each worker
		TASK * curr_task = malloc(sizeof(*curr_task));
		for(int j = 0; j < atoi(args[1]); j++) {
			TASK * curr_task = malloc(sizeof(*curr_task));
			curr_task->start_row = curr_row;
			curr_task->num_of_rows = rows_to_complete;
	
			write(task_pipe[1], curr_task, sizeof(*curr_task));
			kill((pid_t)children[j], SIGUSR1); //send signal to process
			curr_row += rows_to_complete;
		}

		int message_received = 0;		
		MSG * par_msg = malloc(sizeof(*par_msg));

		while(message_received < IMAGE_HEIGHT) { //not all message received
			read(data_pipe[0], par_msg, sizeof(*par_msg)); //read from data pipe
			message_received++;

			for(int k = 0; k < IMAGE_WIDTH; k++) //copy data from par_msg object
				pixels[par_msg->row_index*IMAGE_WIDTH+k] = par_msg->rowdata[k];
			
			//create new task for that child
			if(par_msg->child_pid != -1 && curr_row < 800) { //need more task and still have unassigned task
				TASK * temp_task = malloc(sizeof(*temp_task));
				temp_task->start_row = curr_row;
				if(curr_row + rows_to_complete > IMAGE_HEIGHT) { //large than the image
					temp_task->num_of_rows = IMAGE_HEIGHT - curr_row;
				} else {
					temp_task->num_of_rows = rows_to_complete;
				}

				write(task_pipe[1], temp_task, sizeof(*temp_task));
				kill((pid_t)par_msg->child_pid, SIGUSR1); //send signal to the process that requests a task
				curr_row += temp_task->num_of_rows;
			}
		}

		//at this point, all messages are received
		close(task_pipe[1]); //close write end of task pipe
		close(data_pipe[0]); //close read end of data pipe
		
		for(int j = 0; j < atoi(args[1]); j++) //send SIGINT to all child
			kill(children[j], SIGINT);

		//at this point, all pipes should be closed

		for(int j = 0; j < atoi(args[1]); j++) //for getrusage(RUSAGE_CHILDREN, &temp) to work
			waitpid(children[j], NULL, 0);
	
		printf("All Child processes have completed\n");

		//report child timing in user & system mode
		struct rusage temp;
		getrusage(RUSAGE_CHILDREN, &temp);
		printf("Total time spent by all child processes in user mode = %.3f ms\n", (float) ((float)temp.ru_utime.tv_sec*(float)1000 + (float)temp.ru_utime.tv_usec/(float)1000));
		printf("Total time spent by all child processes in system mode = %.3f ms\n", (float) ((float)temp.ru_stime.tv_sec*(float)1000 + (float)temp.ru_stime.tv_usec/(float)1000));


		//report parent timing in user & system mode
		getrusage(RUSAGE_SELF, &temp);
		printf("Total time spent by parent process in user mode = %.3f ms\n", (float) ((float)temp.ru_utime.tv_sec*(float)1000 + (float)temp.ru_utime.tv_usec/(float)1000));
		printf("Total time spent by parent process in system mode = %.3f ms\n", (float) ((float)temp.ru_stime.tv_sec*(float)1000 + (float)temp.ru_stime.tv_usec/(float)1000));


		//report parent timing
		clock_gettime(CLOCK_MONOTONIC, &end_time);
		difftime = (end_time.tv_nsec - start_time.tv_nsec)/1000000.0 + (end_time.tv_sec - start_time.tv_sec)*1000.0;
		printf("Total elapse time measured by parent process = %.3f ms\n", difftime);
	

		//draw the image by using the SDL2 library
		printf("Draw the image\n");
		DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 3000);
	
		return 0;

	} else if (pid == 0) { //==============child==============

		close(data_pipe[0]); //close read end for data pipe of parent
		close(task_pipe[1]); //close write end for task pipe of parent	

		printf("Child (%d): Start up. Wait for task!\n", (int)getpid());
		
		while(1); //wait for SIGUSR1/SIGINT

	} else {
		printf("Fail to create child!\n");
	}
}

