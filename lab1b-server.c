#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <termios.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

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

// Before exiting, restore the terminal to original mode
void restoreTerminal(){
	if(tcsetattr(STDIN_FILENO, TCSANOW, &savedTerminal) < 0){
		fprintf(stderr, "Error in restoring to original terminal mode");
		exit(1);
	}

	// Status of child process
	int status;
	waitpid(pID, &status, 0);
	int sig = status & 0xFF;
	int finalStat = status/256;
	fprintf(stderr,"\rSHELL EXIT SIGNAL=%d STATUS=%d\n", sig, finalStat);
	exit(0);
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

// Will only be using this read
void readWrite2(){
	struct pollfd fds[2];
	
//	printf("hello\n");

	// STDIN - socket to client
	fds[0].fd = 0;
	fds[0].events = 0;
	fds[0].events = POLLIN | POLLHUP | POLLERR;

	// Read end of pipe 2 (read from shell's output)
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

int main(int argc, char *argv[]){
	
	int opt = 0;
	int portnum;

	struct option longopts[] = {
		{"port", 	required_argument, 	NULL, 'p'},
		{"encrypt", no_argument, 		NULL, 'e'},
		{0,0,0,0}
	};

	while((opt = getopt_long(argc, argv, "p:e", longopts, NULL)) != -1){
		switch(opt){
			case 'p':
				portnum = atoi(optarg);
				break;
			case 'e':
				// TODO: set up to encrypt
				break;
			default:
				fprintf(stderr, "Usage: ./lab1a --port=[portnum] --encrypt\n");
				exit(1);
				break;
		}
	}

	// Create socket connection
	int sockfd, newsockfd;
	unsigned int clientlen;
	struct sockaddr_in serv_addr, client_addr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		fprintf(stderr, "Error opening socket\n");
		exit(1);
	}

	// Populate the server struct and bind it to sockfd
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portnum);

	if(bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
		fprintf(stderr, "Error binding socket\n");
		exit(1);
	}

	// Set up for connecting to client socket
	listen(sockfd, 5);
	clientlen = sizeof(client_addr);
	newsockfd = accept(sockfd, (struct sockaddr *) &client_addr, &clientlen);
	if(newsockfd < 0){
		fprintf(stderr, "Error accepting socket\n");
		exit(1);
	}

	// Create pipes
	if(pipe(pipe1) == -1){
		fprintf(stderr, "Error in creating pipe 1.");
		exit(1);
	}
	if(pipe(pipe2) == -1){
		fprintf(stderr, "Error in creating pipe 2.");
		exit(1);
	}

	// Start a shell process
	pID = fork();
	if(pID < 0){
		fprintf(stderr, "Error when trying to fork");
		exit(1);
	}
	// Child process will execute the shell
	if(pID == 0){

		// Make child's stdin read from pipe 1
		close(pipe1[1]);
		dup2(pipe1[0], 0);
		close(pipe1[0]);

		// Write child's stdout and stderr to pipe 2
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

		// Redirect stdin/stderr/stdout to the socket
		dup2(newsockfd, 0);
		dup2(newsockfd, 1);
		dup2(newsockfd, 2);
		close(newsockfd);
	}
	
	// Read and write for shell communication
	readWrite2();
	
	// Program finished successfully
 	//restoreTerminal();
 	exit(0);

}















