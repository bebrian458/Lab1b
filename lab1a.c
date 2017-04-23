#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <termios.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

// Terminal modes
struct termios savedTerminal;
struct termios configTerminal;

// Max buffer size for reads
int SIZE_BUFFER = 1024;

// Pipes
int pipe1[2];
int pipe2[2];

// Child pID
int pID;

// Shell flag, by default, it is 0
int isShell = 0;


// Before exiting, restore the terminal to original mode
void restoreTerminal(){
	if(tcsetattr(STDIN_FILENO, TCSANOW, &savedTerminal) < 0){
		fprintf(stderr, "Error in restoring to original terminal mode");
		exit(1);
	}

	// Status of child process
	int status;

	if(isShell){
		waitpid(pID, &status, 0);
		int sig = status & 0xFF;
		int finalStat = status/256;
		fprintf(stderr,"\rSHELL EXIT SIGNAL=%d STATUS=%d\n", sig, finalStat);
		exit(0);
	}
}

void signal_handler(int signum){
	
	// SIGINT signals to kill child process/shell
	if(signum == SIGINT){
		kill(pID, SIGINT);
	}

	if(signum == SIGPIPE){
		exit(1);
	}
}


// Normal read and write
void readWrite(){

	char buffer[1];
	ssize_t reading = read(0, buffer, 1);
  	while(reading > 0){

  		// Check for ^D to exit
  		if(*buffer == 0x04){
	  		//restoreTerminal();
  			exit(0);
  		}

  		// Check for \r and \n to create a new line
  		if(*buffer == '\r' || *buffer == '\n'){
  			char temp[2] = {'\r', '\n'};
  			write(1, temp, 2);
  		}

    	write(1, buffer, 1);
    	reading = read(0, buffer, 1);
 	}
}


void readWrite2(){
	struct pollfd fds[2];
	
//	printf("hello\n");

	// STDIN
	fds[0].fd = 0;
	fds[0].events = 0;
	fds[0].events = POLLIN | POLLHUP | POLLERR;

	// Read end of pipe 2
	fds[1].fd = pipe2[0];
	fds[1].events = 0;
	fds[1].events = POLLIN | POLLHUP | POLLERR;


	while(1){

		int value = poll(fds, 2, 0);
		if(value < 0){
			fprintf(stderr, "Error in poll. %s\n", strerror(errno));
			exit(1);
		}
		// STDIN to shell
		if(fds[0].revents & POLLIN){

			// Read from STDIN
			char buffer[SIZE_BUFFER];
			int index = 0;
			ssize_t reading = read(0, buffer, SIZE_BUFFER);

			while(reading > 0 && index < reading){

				// Check for ^C to kill
				if(*(buffer+index) == 0x03){
					kill(pID, SIGINT);
					exit(1);
				}

		  		// Check for ^D to exit
		  		if(*(buffer+index) == 0x04){

		  			// Close pipe to shell
		  			close(pipe1[1]);

		  			// Process remaining from shell
			    	char buffer2[SIZE_BUFFER];
			    	char temp2[2];
			    	int bufptr = 0;
			    	ssize_t sh_reading = read(pipe2[0], buffer2, SIZE_BUFFER);
			    	while(sh_reading > 0 && bufptr < sh_reading){
			    		
	    				// Pass in a \r\n to STDOUT if \n
			    		if(*(buffer2 + bufptr) == '\n'){
			    			temp2[0] = '\r';
			    			temp2[1] = '\n';
			    			write(1, temp2, 2);
			    		}
			    		else{
			    			write(1, buffer2 + bufptr, 1);	
			    		}
			    		bufptr++;
			    	}

			    	// Close pipe from shell
			    	close(pipe2[0]);

			    	//fprintf(stderr, "Exit from using ^D\n");
			    	//restoreTerminal();
		  			exit(0);
		  		}

		  		// Check for \r and \n to create a new line
		  		if(*(buffer+index) == '\r' || *(buffer+index) == '\n'){
		  			char temp[2] = {'\r', '\n'};
		  			write(1, temp, 2);

		  			// Pass in only a \n to shell
		  			temp[0] = '\n';
		  			write(pipe1[1], temp, 1);

			  	}
		  		// Otherwise, pass characters normally to STDOUT and shell
		  		else{
		  			write(1, buffer+index, 1);
		    		write(pipe1[1], buffer+index, 1);
		  		}
		  		index++;
		  	}
		}

		// From shell to STDOUT
		if(fds[1].revents & POLLIN){
			
			// Parent reads from pipe 2, writes to stdout
	    	char buffer2[SIZE_BUFFER];
	    	char temp2[2];
	    	int bufptr = 0;
	    	ssize_t sh_reading = read(pipe2[0], buffer2, SIZE_BUFFER);
	    	while(sh_reading > 0 && bufptr < sh_reading){
	    		
	    		// Shutdown when receiving ^D from shell
	    		if(*(buffer2 + bufptr) == 0x04){
	    			exit(0);
	    		}

	    		// Pass in a \r\n to STDOUT if \n
	    		if(*(buffer2 + bufptr) == '\n'){
	    			temp2[0] = '\r';
	    			temp2[1] = '\n';
	    			write(1, temp2, 2);
	    		}
	    		else{
	    			write(1, buffer2 + bufptr, 1);	
	    		}
	    		bufptr++;
	    	}
		}

		// Stop read and write if error
		if(fds[0].revents & (POLLHUP+POLLERR)){
			exit(1);
		}

		if(fds[1].revents & (POLLHUP+POLLERR)){
			exit(1);	
		}
	}
}


/*
// Read and write function
void readWrite2(){

	char buffer[1];
	char temp[2];
	ssize_t reading = read(0, buffer, 1);
  	while(reading > 0){

  		// Check for ^D to exit
  		if(*buffer == 0x04){
  			restoreTerminal();
  			exit(0);
  		}

  		// Check for \r and \n to create a new line
  		if(*buffer == '\r' || *buffer == '\n'){
  			
  			// <cr> or <lf> should echo as <cr><lf> in terminal
  			temp[0] = '\r';
  			temp[1] = '\n'	;
  			write(1, temp, 2);

  			// but go to shell as <lf>
  			temp[0] = '\n';
  			write(pipe1[1], temp, 1);

  			// Parent reads from pipe 2, writes to stdout
	    	char buffer2[SIZE_BUFFER];
	    	char temp2[2];
	    	int bufptr = 0;
	    	ssize_t sh_reading = read(pipe2[0], buffer2, SIZE_BUFFER);
	    	while(sh_reading > 0 && bufptr < sh_reading){
	    		if(*(buffer2 + bufptr) == '\n'){
	    			// printf("hello");
	    			temp2[0] = '\r';
	    			temp2[1] = '\n';
	    			write(1, temp2, 2);
	    		}
	    		else{
	    			//printf("goodbye");
	    			write(1, buffer2 + bufptr, 1);	
	    		}
	    		bufptr++;
	    	}

  		}
  		else{
	  		// Write into terminal
	    	write(1, buffer, 1);

	  		// Write into pipe to child stdin
	  		write(pipe1[1], buffer, 1);
	  	}

    	// // Parent reads from pipe 2, writes to stdout
    	// char buffer2[1];
    	// char temp2[2];
    	// ssize_t sh_reading = read(pipe2[0], buffer2, 1);
    	// while(reading > 0){
    	// 	write(1, buffer2, 1);    		
    	// 	sh_reading = read(pipe2[0], buffer2, 1);
    	// }

    	// reading = read(0, buffer, 1);


 		reading = read(0, buffer, 1);

 	}
}
*/

int main(int argc, char *argv[]){
	
	int opt = 0;

	struct option longopts[] = {
		{"shell", no_argument, NULL, 's'},
		{0,0,0,0}
	};

	while((opt = getopt_long(argc, argv, "s", longopts, NULL)) != -1){
		switch(opt){
			case 's':
				isShell = 1;
				signal(SIGINT, signal_handler);
				signal(SIGPIPE, signal_handler);
				break;
			default:
				fprintf(stderr, "Usage: ./lab1a --shell\n");
				exit(1);
				break;
		}
	}

	// Save and configure terminal modes
	if(tcgetattr(STDIN_FILENO, &savedTerminal) < 0){
		fprintf(stderr, "Error in tcgetattr");
		exit(1);
	}

	if(tcgetattr(STDIN_FILENO, &configTerminal) < 0){
		fprintf(stderr, "Error in tcgetattr");
		exit(1);
	}
	configTerminal.c_iflag = ISTRIP;
	configTerminal.c_oflag = 0;
	configTerminal.c_lflag = 0;
	if(tcsetattr(STDIN_FILENO, TCSANOW, &configTerminal) < 0){
		fprintf(stderr, "Error in tcsetattr");
		//restoreTerminal();
		exit(1);
	}

	atexit(restoreTerminal);

	// Create pipes
	if(pipe(pipe1) == -1){
		fprintf(stderr, "Error in creating pipe 1.");
	}
	if(pipe(pipe2) == -1){
		fprintf(stderr, "Error in creating pipe 2.");
	}

	// Start a shell process if flagged
	if(isShell){

		pID = fork();
		if(pID < 0){
			fprintf(stderr, "Error when trying to fork");
			exit(1);
		}
		// Child process will execute the shell
		if(pID == 0){

			// Make stdin read from pipe 1
			close(pipe1[1]);
			dup2(pipe1[0], 0);
			close(pipe1[0]);

			// Write stdout and stderr to pipe 2
			close(pipe2[0]);
			dup2(pipe2[1], 1);
			dup2(pipe2[1], 2);
			close(pipe2[1]);

			// Execute the shell
			if(execvp("/bin/bash", NULL) == -1){
				fprintf(stderr, "Error in executing the shell");
				exit(1);
			}

		}
		// Parent process bridges the shell and terminal
		else{

			// Close the unused pipe ends
			close(pipe1[0]);
			close(pipe2[1]);

		}

	}


	
	if(isShell){
		readWrite2();	// Read and write for shell communication
	}
	else
		readWrite();	// Read and write normally

	
	// Program finished successfully
 	//restoreTerminal();
 	exit(0);

}















