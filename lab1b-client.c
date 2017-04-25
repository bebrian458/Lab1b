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
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <mcrypt.h>


//====== REMEMBER TO DUP2 IN SERVER =========//


// Terminal modes
struct termios savedTerminal;
struct termios configTerminal;

// Max buffer size for reads
const int SIZE_BUFFER = 1024;

// Child pID
int pID;

// Flags
int isEncrypt=0, 
	isLog=0;

// Log file
char* logfile;
int logfd;

// Socket
int sockfd;

// Key stats
#define MAX_KEYSIZE 16
char key[MAX_KEYSIZE];
int keylen;

// Encryption descriptors
MCRYPT encrypt_fd, decrypt_fd;
char* IV;
char* IV2;

// Macros
const int RECEVING = 0;
const int SENDING = 1;

void signal_handler(int signum){
	
	// SIGINT signals to kill child process/shell
	if(signum == SIGINT){
		kill(pID, SIGINT);
	}

	if(signum == SIGPIPE){
		exit(1);
	}
}

void writeToLog(int sending, int numBytes, char* buffer){

	if(sending){
		char message[14] = "SENT # bytes: ";
		message[5] = '0' + numBytes;
		write(logfd, message, 14);
	}
	else
	{
		char message[18] = "RECEIVED # bytes: ";
		message[9] = '0' + numBytes;
		write(logfd, message, 18);

	}
	write(logfd, buffer, numBytes);
	write(logfd, "\n", 1);
}

// Get key and key length from my.key file
void getkey(){
	int keyfd = open("my.key", 0400);
	if(keyfd < 0){
		fprintf(stderr, "Error opening my.key\n");
		exit(1);
	}
	keylen = read(keyfd, key, MAX_KEYSIZE);
	// if(keylen != MAX_KEYSIZE){
	// 	fprintf(stderr, "Keylen is not maxsize\n");
	// 	exit(1);
	// }
	close(keyfd);
	fprintf(stderr, "%s\n", key);
	fprintf(stderr, "%d\n", keylen);
}

// Initialize modules for encryption and decryption
void mcryptInit(){

	// Initalize for encryption
	encrypt_fd = mcrypt_module_open("twofish", NULL, "cfb", NULL);
	if(encrypt_fd == MCRYPT_FAILED){
		fprintf(stderr, "Error opening encrypt module\n");
		exit(1);
	}
	// Get IV
	IV = malloc(mcrypt_enc_get_iv_size(encrypt_fd));
  	int i;
  	for(i = 0; i < mcrypt_enc_get_iv_size(encrypt_fd); i++)
    	IV[i] = 'A';


	if(mcrypt_generic_init(encrypt_fd, key, keylen, IV) < 0){
		fprintf(stderr, "Error initializing encrypt module\n");
		exit(1);
	}

	// Initialize for decryption
	decrypt_fd = mcrypt_module_open("twofish", NULL, "cfb", NULL);
	if(decrypt_fd == MCRYPT_FAILED){
		fprintf(stderr, "Error opening decrypt module\n");
		exit(1);
	}

	if(mcrypt_generic_init(decrypt_fd, key, keylen, IV) < 0){
		fprintf(stderr, "Error initializing decrypt mdule\n");
		exit(1);
	}
}

void mcryptDeinit(){

	// Deinitialize for encrypt descriptor
	mcrypt_generic_deinit(encrypt_fd);
	mcrypt_module_close(encrypt_fd);

	// Deinitialize for decrypt descriptor
	mcrypt_generic_deinit(encrypt_fd);
	mcrypt_module_close(decrypt_fd);
}

// Before exiting, restore the terminal to original mode
void restoreTerminal(){

	// Close all file descriptors
	close(sockfd);
	if(isEncrypt)
		mcryptDeinit();

	if(tcsetattr(STDIN_FILENO, TCSANOW, &savedTerminal) < 0){
		fprintf(stderr, "Error in restoring to original terminal mode");
		exit(1);
	}
}

void readWrite2(int sockfd){
	struct pollfd fds[2];
	
	// STDIN
	fds[0].fd = 0;
	fds[0].events = 0;
	fds[0].events = POLLIN | POLLHUP | POLLERR;

	// Socket to server
	fds[1].fd = sockfd;
	fds[1].events = 0;
	fds[1].events = POLLIN | POLLHUP | POLLERR;


	while(1){

		int value = poll(fds, 2, 0);
		if(value < 0){
			fprintf(stderr, "Error in poll. %s\n", strerror(errno));
			exit(1);
		}
		// STDIN to server
		if(fds[0].revents & POLLIN){

			// Read from STDIN
			char buffer[SIZE_BUFFER];
			int index = 0;
			ssize_t reading = read(0, buffer, SIZE_BUFFER);
			
//			fprintf(stderr, "Number of bytes read: %d\r\n", reading);

			// Iterate over each index in buffer
			while(reading > 0 && index < reading){

				//fprintf(stderr, "%c\n", &buffer[index]);

				// Check for ^C to kill
				if(*(buffer+index) == 0x03){
//					kill(pID, SIGINT);
					exit(1);
				}

		  		// Check for \r and \n to create a new line
		  		if(*(buffer+index) == '\r' || *(buffer+index) == '\n'){
		  			
		  			// Echo \r\n to STDOUT
		  			char temp[2] = {'\r', '\n'};
		  			write(1, temp, 2);

		  			temp[0] = '\n';

		  			// 1. Encrypt if flagged
		  			if(isEncrypt){
						if(mcrypt_generic(encrypt_fd, temp, 1) != 0){
							fprintf(stderr, "Error in encryption to server\n");
							exit(1);
						}
		  			}

		  			// 2. Log if flagged
					if(isLog)
						writeToLog(SENDING, 1, temp);

		  			// 3. Pass in only a \n to server
		  			write(sockfd, temp, 1);

			  	}
			  	// Otherwise, pass characters normally to STDOUT and server
		  		else{

		  			// Send to STDOUT
		  			write(1, buffer+index, 1);
//					fprintf(stderr, "\r\nBefore encryption: %c\r\n", *(buffer+index));


		  			// 1. Encrypt if flagged
		  			if(isEncrypt){
		  				if(mcrypt_generic(encrypt_fd, buffer+index, 1) != 0){
		  					fprintf(stderr, "Error in encryption to server\n");
							exit(1);
		  				}
		  			}

					// 2. Log if flagged
					if(isLog)
						writeToLog(SENDING, 1, buffer+index);

					// 3. Send to server
		    		write(sockfd, buffer+index, 1);
//		    		fprintf(stderr, "After encryption: %c\r\n\n", *(buffer+index) );
		  		}
		  		index++;
		  	}
		}

		// From server to STDOUT
		if(fds[1].revents & POLLIN){
			
			// Read from server, output to STDOUT
	    	char buffer2[SIZE_BUFFER];
	    	char temp2[2];
	    	int bufptr = 0;
	    	ssize_t sh_reading = read(sockfd, buffer2, SIZE_BUFFER);

	    	while(sh_reading > 0 && bufptr < sh_reading){
    		
    			// 1. Log if flagged
    			if(isLog)
    				// Log will display both a CR and LF on separate lines
    				writeToLog(RECEVING, 1, buffer2+bufptr);

    			// 2. Decrypt if flagged
	  			if(isEncrypt){
	  				if(mdecrypt_generic(decrypt_fd, buffer2+bufptr, 1) != 0){
	  					fprintf(stderr, "Error in decryption from server\n");
						exit(1);
	  				}
	  			}

	    		// Shutdown when receiving ^D from shell
	    		if(*(buffer2 + bufptr) == 0x04){
	    			exit(0);
	    		}
	    		// Server handled NL to CRLF mapping, just pass everything as is to STDOUT
	    		else{
	    			
	    			// 3. Pass characters normally to STDOUT
	    			write(1, buffer2+bufptr, 1);	
	    		}
	    		bufptr++;
	    	}
		}

		// Stop read and write if error
		if(fds[0].revents & (POLLHUP+POLLERR)){
			fprintf(stderr, "POLL Error\n");
			exit(1);
		}

		if(fds[1].revents & (POLLHUP+POLLERR)){
			fprintf(stderr, "POLL Error\n");
			exit(1);	
		}
	}
}

int main(int argc, char *argv[]){
	
	int opt = 0;
	int portnum;

	struct option longopts[] = {
		{"port", 	required_argument, 	NULL, 'p'},
		{"log", 	required_argument, 	NULL, 'l'},
		{"encrypt", no_argument, 		NULL, 'e'},
		{0,0,0,0}
	};

	while((opt = getopt_long(argc, argv, "p:l:e", longopts, NULL)) != -1){
		switch(opt){
			case 'p':
				portnum = atoi(optarg);
				signal(SIGINT, signal_handler);
				signal(SIGPIPE, signal_handler);
				break;
			case 'l':
				isLog = 1;
				logfile = optarg;
				logfd = creat(logfile, S_IRWXU);
				break;
			case 'e':
				isEncrypt = 1;
				getkey();
				mcryptInit();
				break;
			default:
				fprintf(stderr, "Usage: ./lab1a --port=[portnum] --log --encrypt\n");
				exit(1);
				break;
		}
	}

	// Create Socket Connection
	struct sockaddr_in serv_addr;
	struct hostent* server;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		fprintf(stderr, "Error opening socket\n");
		exit(1);
	}

	// Get server name
	server = gethostbyname("localhost");
	if(server == NULL){
		fprintf(stderr, "Error, could not find host\n");
		exit(1);
	}

	// Populate the server struct
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(portnum);
	bcopy((char *) server->h_addr, 
		(char *) &serv_addr.sin_addr.s_addr,
		server->h_length);

	// Connect to the server
	if(connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
		fprintf(stderr, "Error connecting to server\n");
		exit(1);
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
	
	// Read and write between server and client
	readWrite2(sockfd);
	
	// Program finished successfully
 	//restoreTerminal();
 	exit(0);

}















