#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/wait.h>
#include <dirent.h>

#define RRQ 1
#define WRQ 2
#define ACK 0x04
#define DATA 3
#define ERR 0x05
#define ERROR -1

#define MAX 1024
#define TIMEOUT 2000
#define RETRIES 5
#define MAXACK 16
#define MODE "octet" //Define Mode Here (octet/netascii)

static char buf[BUFSIZ];

char errors[7][50] = {"Not Defined!",
                        "No Such File!",
                        "Access Violation!",
                        "Disk full!",
                        "TFTP operation not allowed!",
                        "Unknown Transfer-ID!",
                        "File already exists!"};

unsigned short int ack_rep = 1;
int segment = 512, status = 1, w_size = 1;

int request(int opcode, char *filename, char *mode, char header[])
{
	int l;
	l = sprintf(header, "%c%c%s%c%s%c", 0x00, opcode, filename, 0x00, mode, 0x00);
	if (l == 0)
	{
		printf ("Failed To Create Request Packet!\n");
		exit(-1);
	}
	return l;
}

int error(int err_code, char *errors, char arr[])
{
	int pl;
	int size = sizeof(char *);
	memset (arr, 0, size);
	pl = sprintf(arr, "%c%c%c%c%s%c", 0x00, ERR, 0x00, err_code, errors, 0x00);
	if(pl == 0)
	{
		printf("Failed To Create Error Packet!\n");
		exit(ERROR);
	}
	return pl;
}

void c_put(char *file_out, struct sockaddr_in server, char *f_mode, int sock)
{
	char filename[128], mode[12];

	FILE *fp;

	strcpy(filename, file_out);
	strcpy(mode, f_mode);

	fp = fopen(filename, "r");
	if(fp == NULL)
	{
		printf("Failed To Open File!\n");
		return;
	}

	int l, server_l, opcode, ssize = 0, bcnt = 0, tid, i, j, n;
	char *idx;
	unsigned short int c = 0, rc = 0, acked = 0;
	unsigned char f_buf[MAX+1], p_buf[MAXACK][MAX+12], r_buf[MAX+12];

	struct sockaddr_in ack;

	for(j=0; j < RETRIES-2;j++)
	{
		server_l = sizeof(ack);
		errno = EAGAIN;
		n = -1;

		for (i = 0; errno == EAGAIN && i <= TIMEOUT && n<0; i++)
		{
			n=recvfrom(sock, r_buf, sizeof(r_buf), MSG_DONTWAIT, (struct sockaddr *)&ack, (socklen_t *)&server_l);
			usleep(1000);
		}

		tid = ntohs(ack.sin_port);
		server.sin_port = htons(tid);

		if(n < 0 && errno != EAGAIN)
			printf("Error in Receiving Message!\n");
		else if(n < 0 && errno == EAGAIN)
			printf("Timeout!\n");
		else
		{
			if(server.sin_addr.s_addr != ack.sin_addr.s_addr)
			{
				j--;
				continue;
			}

			if(tid != ntohs(server.sin_port))
			{
				l = error(5, errors[5], buf);
				if(sendto(sock, buf, l, 0, (struct sockaddr *)&server, sizeof(server)) != l)
					perror("Transfer To Server Buffer Failed!\n");
				j--;
				continue;
			}

			idx = (char *)r_buf;

			if(idx++[0] != 0x00)
				printf("Bad Nullbyte!\n");
			opcode = *idx++;
			rc = *idx++ << 8;
			rc &= 0xff00;
			rc += (*idx++ & 0x00ff);

			if(opcode != 4 || rc != c)
			{
				if(opcode > 5)
				{
					l = error(4, errors[4], buf);
					if(sendto(sock, buf, l, 0, (struct sockaddr *)&server, sizeof(server)) != l)
					{
						perror("Transfer To Server Buffer Failed!\n");
					}
				}
			}
			else
			{
				break;
			}
		}
	}

	memset (f_buf, 0, sizeof (f_buf));

	while(1)
	{
		acked = 0;
		ssize = fread(f_buf, 1, segment, fp);
		c++;

		if (c == 1)
			bcnt = 0;
		else if (c == 2)
			bcnt = 0;
		else
			bcnt = (c - 2) % ack_rep;

		sprintf((char *) p_buf[bcnt], "%c%c%c%c", 0x00, 0x03, 0x00, 0x00);
		memcpy((char *) p_buf[bcnt] + 4, f_buf, ssize);
		l = 4 + ssize;
		p_buf[bcnt][2] = (c & 0xFF00) >> 8;
		p_buf[bcnt][3] = (c & 0x00FF);

		if(sendto(sock, p_buf[bcnt], l, 0, (struct sockaddr *)&server, sizeof(server)) != l)
		{
			perror ("Transfer To Server Buffer Failed!\n");
			return;
		}

		if(((c) % ack_rep) == 0 || ssize != segment)
		{
			for(j=0; j < RETRIES; j++)
			{
				server_l = sizeof(ack);
				errno = EAGAIN;
				n = -1;

				for(i=0; errno == EAGAIN && i <= TIMEOUT && n < 0; i++)
				{
					n = recvfrom (sock, r_buf, sizeof (r_buf), MSG_DONTWAIT,(struct sockaddr *) &ack,(socklen_t *) & server_l);
					usleep(1000);
				}
				if(n < 0 && errno != EAGAIN)
					printf("Error in Receiving Message!\n");
				else if (n < 0 && errno == EAGAIN)
					printf("Timeout!\n");
				else
				{
					if(server.sin_addr.s_addr != ack.sin_addr.s_addr)
					{
						j--;
						continue;
					}

					if(tid != ntohs(server.sin_port))
					{
						l = error(5, errors[5], buf);
						if(sendto(sock, buf, l, 0, (struct sockaddr *)&server, sizeof(server)) != l)
							perror ("Transfer To Server Buffer Failed!\n");
						j--;
						continue;
					}

					idx = (char *) r_buf;

					if (idx++[0] != 0x00)
						printf("Bad Nullbyte\n");
					opcode = *idx++;
					rc = *idx++ << 8;
					rc &= 0xff00;
					rc += (*idx++ & 0x00ff);
					if (opcode != 4 || rc != c)
					{
						if (opcode > 5)
						{
							l = error(4, errors[4], buf);
							if(sendto(sock, buf, l, 0, (struct sockaddr *)&server, sizeof(server)) != l)
								perror("Transfer To Server Buffer Failed!\n");
						}
					}
					else
					{
						break;
					}
				}

				for(i = 0; i <= bcnt; i++)
				{
					if(sendto(sock, p_buf[i], l, 0, (struct sockaddr *)&server, sizeof(server)) != l)
					{
						perror("Transfer To Server Buffer Failed!\n");
						return;
					}
					printf("ACK Lost!\n");
				}
			}

		}
		else if(status)
		{
			n = recvfrom(sock, r_buf, sizeof(r_buf), MSG_DONTWAIT, (struct sockaddr *)&ack, (socklen_t *)&server_l);
		}

		if(j == RETRIES)
		{
			printf("ACK Timeout! Terminating Transfer!\n");
			fclose(fp);
			return;
		}

		if(ssize != segment)
			break;
		memset(f_buf, 0, sizeof(f_buf));
	}
	fclose(fp);
	printf("***File Transfer Complete!***\n");
	return;
}

void c_get(char *p_Filename, struct sockaddr_in server, char *p_Mode, int sock)
{
	char filename[128], mode[12];

	FILE *fp;

	strcpy(filename, p_Filename);
	strcpy(mode, p_Mode);

	fp = fopen(filename, "w");	
	if (fp == NULL)
	{
		printf("Unable To Fetch File!\n");
		return;
	}
	else
		printf("File Created! Waiting for Fetch!\n");

	int server_l, l, opcode, tid = 0, flag = 1, i, j, n;
	unsigned short int c = 0, rc = 0;
	unsigned char f_buf[MAX+1], p_buf[MAX+12];
	extern int errno;
	char *idx, ack_buf[512];

	struct sockaddr_in data;

	bzero(f_buf, sizeof(f_buf));
	n = segment + 4;
	do
	{
		bzero(p_buf, sizeof(p_buf));
		bzero(ack_buf, sizeof(ack_buf));
		if(n != (segment + 4))	
		{
			l = sprintf (ack_buf, "%c%c%c%c", 0x00, 0x04, 0x00, 0x00);
			ack_buf[2] = (c & 0xFF00) >> 8;
			ack_buf[3] = (c & 0x00FF);

			if(sendto(sock, ack_buf, l, 0,(struct sockaddr *)&server, sizeof(server)) != l)
			{
				perror("Transfer To Server Buffer Failed!");
				return;
			}
			goto done;
		}

		c++;

		for(j=0; j < RETRIES; j++)
		{
			server_l = sizeof(data);
			errno = EAGAIN;
			n = -1;

			for(i=0; errno == EAGAIN && i <= TIMEOUT && n < 0; i++)
			{
				n =	recvfrom(sock,p_buf,sizeof(p_buf)-1,MSG_DONTWAIT,(struct sockaddr *)&data,(socklen_t *)&server_l);
				usleep (1000);
			}

			if(!tid)
			{
				tid = ntohs(data.sin_port);
				server.sin_port = htons(tid);
			}

			if(n < 0 && errno != EAGAIN)
				printf("Error in Receiving Message\n");
			else if(n < 0 && errno == EAGAIN)
				printf("Timeout!\n");
			else
			{
				if(tid != ntohs (server.sin_port))
				{
					l = sprintf((char *) p_buf, "%c%c%c%cBad/Unknown TID%c",0x00, 0x05, 0x00, 0x05, 0x00);
					if(sendto (sock, p_buf, l, 0, (struct sockaddr *) &server, sizeof (server)) != l)
						perror("Transfer To Server Buffer Failed!\n");
					j--;
					continue;
				}

				idx = (char *) p_buf;
				if (idx++[0] != 0x00)
					printf ("Bad Nullbyte!\n");
				opcode = *idx++;
				rc = *idx++ << 8;
				rc &= 0xff00;
				rc += (*idx++ & 0x00ff);
				memcpy ((char *) f_buf, idx, n - 4);

				if(flag)
				{
					if(n > 516)
						segment = n - 4;
					else if(n < 516)
						segment = 512;
					flag = 0;
				}

				if(opcode != 3)
				{
					if (opcode > 5)
					{
						l=sprintf((char *) p_buf,"%c%c%c%cIllegal operation%c",0x00, 0x05, 0x00, 0x04, 0x00);
						if(sendto (sock, p_buf, l, 0,(struct sockaddr *)&server, sizeof(server)) != l)
							printf("Mismatch\n");
					}
				}
				else
				{
					l = sprintf(ack_buf, "%c%c%c%c", 0x00, 0x04, 0x00, 0x00);
					ack_buf[2] = (c & 0xFF00) >> 8;
					ack_buf[3] = (c & 0x00FF);
					if(((c-1) % ack_rep) == 0)
					{
						if(sendto(sock, ack_buf, l, 0, (struct sockaddr *)&server, sizeof(server)) != l)
						{
							perror("Transfer To Server Buffer Failed!\n");
							return;
						}
					}
					else if(c == 1)
					{
						if(sendto(sock, ack_buf, l, 0, (struct sockaddr *)&server, sizeof(server)) != l)
						{
							printf("Mismatch!\n");
							return;
						}
					}
					break;
				}
			}
		}

		if (j == RETRIES)
		{
			printf ("Timeout! Termination Fetch!\n");
			fclose (fp);
			return;
		}
	}

	while(fwrite (f_buf, 1, n - 4, fp) == n - 4);
	fclose(fp);
	sync();
	printf("xxx*Corrupt*xxx\n");
	return;

done:

	fclose(fp);
	sync();
	if(status)
		printf("***File Fetch Complete!***\n\n");
	return;
}

int main (int argc, char **argv)
{
	int port;
	char* host_ip;
	char opcode, filename[196], mode[12] = MODE, pg;

	if (argc <= 1)
	{
		printf("Provide <IP> and <Port Number>\n");
		exit(1);
	}

	host_ip = argv[1];
	port = atoi(argv[2]);

	printf("Enter File-Name: ");
	scanf("%s",filename);
	printf("PUT [p]/GET [g]: ");
	scanf(" %c",&pg);
	printf("\n");

	if(pg == 'p')
	{
		opcode = WRQ;
		printf("-----Sending File To Server-----\n\n");
	}
	else
	{
		opcode = RRQ;
		printf("-----Fetching File From Server-----\n\n");
	}

	struct hostent *host;

	if (!(host = gethostbyname (host_ip)))
	{
		perror ("Host Address Not Found!\n");
		exit (2);
	}

	FILE *fp;

	if(opcode == WRQ)
	{
		fp = fopen (filename, "r");	
	}
	else if(opcode == RRQ)
	{
		fp = fopen (filename, "w");		
	}

	if (fp == NULL)
	{
		printf ("Failed To Open File!\n");
		exit(1);
	}
	fclose (fp);		

	int sock;
	struct sockaddr_in server;

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		printf ("Failed To Create Socket!\n");
		exit(1);
	}

	bzero(&server, sizeof (server));

	server.sin_family = AF_INET;
	memcpy (&server.sin_addr, host->h_addr, host->h_length);
	server.sin_port = htons (port);	

	int server_l,l;

	server_l = sizeof (server);
	memset (buf, 0, BUFSIZ);
	
	l = request(opcode, filename, mode, buf);
	if (sendto(sock, buf, l, 0, (struct sockaddr *) &server, server_l) != l)
	{
		perror ("Failed To Send Datagram!\n");
		exit(-1);
	}

	switch(opcode)
	{
		case RRQ:
			c_get(filename, server, mode, sock);
			break;
		case WRQ:
			c_put(filename, server, mode, sock);
			break;
		default:
			printf("Transfer To Server Buffer Failed!\n");
	}
	close(sock);
	return 1;
}