/*
	Work with character device driver
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>	
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>

#define SERVER_PORT 23456 

int send_data_to_monitor(char* buf);

struct cse536_buffer
{
	int record_id;
	int *final_clock;
	int *original_clock;
	uint32_t src_ip;
	uint32_t destination_ip;
	char data[236];
}buffer;

struct cse536_tx_rcv_buffer
{
	int record_id;
	int final_clock;
	int original_clock;
	uint32_t src_ip;
	uint32_t destination_ip;
	char data[236];
};

struct cse536_tx_rcv_buffer rcv_buffer;
char dest_addr[16];

uint32_t  in_aton(const char *str)
{
         unsigned long l;
         unsigned int val;

          int i;
  
          l = 0;
          for (i = 0; i < 4; i++) {
                  l <<= 8;
                  if (*str != '\0') {
                          val = 0;
                          while (*str != '\0' && *str != '.' && *str != '\n') {
                                  val *= 10;
                                  val += *str - '0';
                                  str++;
                         }
                          l |= val;
                          if (*str != '\0')
                                  str++;
                  }
          }
         return htonl(l);
}




main(int argc, char *argv[])
{
	FILE *fd = NULL;
	int file_desc = 0;
	char data[256], *dest_address, option;
	struct cse536_tx_rcv_buffer snd_buffer;
	size_t count;
	int exit = 0, ch;
	int  final_clk = 0;
	int original_clk = 0;
	char rcvd[276];
	char source[16] = {0};
	char rcv_address[16] = {0};

	while(exit == 0)
	{

		printf(" (A) to set detination address\t");
		printf("(S) Send Event\n");
		printf("(R) Receive Data\t");
		printf("\t(Q) exit\n");

		fflush(stdin);

		scanf("%c", &option);
		while( (ch = fgetc(stdin)) != EOF && ch != '\n' ){}	
		option = toupper(option);

		

		switch(option)
		{
			case 'A':
				memset(dest_addr,0,sizeof(dest_addr));
				printf("Destination Address\n");
				scanf("%15s", dest_addr);
				while( (ch = fgetc(stdin)) != EOF && ch != '\n' ){}	
				
				break;
			case 'S':
				printf("Enter the data to be sent:\n");
				scanf("%256s", data);
				while( (ch = fgetc(stdin)) != EOF && ch != '\n' ){}	
				memset(&buffer, 0, sizeof(buffer));
				buffer.record_id = 1;
				buffer.src_ip = in_aton("192.168.85.132");
				buffer.destination_ip = in_aton(dest_addr);
				
				buffer.final_clock = &final_clk;

				buffer.original_clock = &original_clk;
				
				memcpy(buffer.data, data, strlen(data));
				file_desc = open("/dev/cse5361",O_RDWR);
				if (file_desc)
				{
					write(file_desc,(const char *)&buffer,sizeof(buffer));
					
					//fclose(fd);

					memset(&snd_buffer,0,sizeof(snd_buffer));
					snd_buffer.record_id = buffer.record_id;
					snd_buffer.original_clock = *(buffer.original_clock);
					snd_buffer.final_clock = *(buffer.final_clock);
					snd_buffer.src_ip = buffer.src_ip;
					snd_buffer.destination_ip = buffer.destination_ip;
					memcpy(snd_buffer.data,buffer.data,sizeof(buffer.data));

					printf("sent an  event to %s",dest_addr);
					printf("\noriginal_clock: %d", original_clk);
					printf("\nFinal_clock: %d", final_clk);
					printf("\n string sent:%s",snd_buffer.data);
					
					send_data_to_monitor((char*)&snd_buffer);
					close(file_desc);
				}
				else
				{
					printf("error while opening file");
				}
			break;
			case 'R':
				fd = fopen("/dev/cse5361", "rb");
				if (fd)
				{
					
					memset(&rcv_buffer,0,sizeof(rcv_buffer));
					count = fread(&rcv_buffer, 1, sizeof(rcv_buffer), fd);

					

					if (!count)
						printf("No data Received\n");
					else
					{

						printf("\nrecord_id %d- ",rcv_buffer.record_id);
						printf("\nfinal_clock %d- ",rcv_buffer.final_clock);
						printf("\noriginal_clock %d- ",rcv_buffer.original_clock);
						printf("\nreceived string %s- ",rcv_buffer.data);

						if(rcv_buffer.record_id == 0)
						{
							printf("received ack from %s",dest_addr);

							if(send_data_to_monitor((char*)&rcv_buffer) < 0)
							{
								printf("error while sending data to monitor\n");
							} 
						}
						else
						{
								printf("received event from %s",rcv_address);

						}

					}
					fclose(fd);
				}
				else
				{
					printf("error while opening file");
				}

			break;
			case 'Q':
				exit = 1;
			break;
			default :
				printf("Invalid Option\n");
				break;
		}
	}
}

int send_data_to_monitor(char* buf)
{

   struct sockaddr_in client, server;
   struct hostent *hp;
   int len, ret, n;
   int s, new_s;

   bzero((char *)&server, sizeof(server));
   server.sin_family = AF_INET;
   server.sin_addr.s_addr = INADDR_ANY;
   server.sin_port = htons(0);

   s = socket(AF_INET, SOCK_DGRAM, 0);
   if (s < 0)
   {
		perror("simplex-talk: UDP_socket error");
		return -1;
   }

   if ((bind(s, (struct sockaddr *)&server, sizeof(server))) < 0)
   {
		perror("simplex-talk: UDP_bind error");
		return -1;
   }

   hp = gethostbyname( "192.168.0.4" );
   if( !hp )
   {
      	fprintf(stderr, "Unknown host %s\n", "localhost");
      	return -1;
   }

   bzero( (char *)&server, sizeof(server));
   server.sin_family = AF_INET;
   bcopy( hp->h_addr, (char *)&server.sin_addr, hp->h_length );
   server.sin_port = htons(SERVER_PORT); 

   
   ret = sendto(s, buf,sizeof(struct cse536_tx_rcv_buffer), 0,(struct sockaddr *)&server, sizeof(server));
   if( ret != sizeof(struct cse536_tx_rcv_buffer))
   {
	   fprintf( stderr, "Datagram Send error %d\n", ret );
	   return -1;
   }

   printf("\ndata sent to monitor succesfully - %d bytes sent",ret);

  
   return 0;
}