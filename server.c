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

char path[64];
int segment = 512;
unsigned short int ack_rep = 1;

void c_read(char *file_out, struct sockaddr_in client, char *c_mode, int tid)
{
	char filename[128], mode[12];

	FILE *fp;

	strcpy (filename, file_out);
	strcpy (mode, c_mode);

	int sock, l, client_l, opcode, ssize = 0, bcnt = 0, i, j, n;
	unsigned short int c = 0, rc = 0, acked = 0;
	unsigned char f_buf[MAX+1],p_buf[MAXACK][MAX+12],recvbuf[MAX+12];
	char f_path[196], *idx;
	struct sockaddr_in ack;

	if((sock = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		printf ("Server Error!\n");
		return;
	}

	if(!strncasecmp(mode, "octet", 5) && !strncasecmp (mode, "netascii", 8))
	{
		if(!strncasecmp (mode, "mail", 4))
			l=sprintf((char *)p_buf[0],"%c%c%c%cThis tftp server will not operate as a mail relay%c",0x00,0x05,0x00,0x04,0x00);
		else
			l=sprintf((char *)p_buf[0],"%c%c%c%cUnrecognized mode (%s)%c",0x00,0x05,0x00,0x04,mode,0x00);
		if (sendto (sock, p_buf[0], l, 0, (struct sockaddr *) &client, sizeof (client)) != l)
		{
			printf("Mismatch!\n");
		}
		return;
	}

	if(strchr(filename,0x5C) || strchr(filename,0x2F))
	{
		l=sprintf((char *)p_buf[0],"%c%c%c%cIllegal filename.(%s) You may not attempt to descend or ascend directories.%c",0x00,0x05,0x00,0x00,filename,0x00);
		if(sendto(sock, p_buf[0], l, 0, (struct sockaddr *)&client, sizeof(client)) != l)
		{
			printf("Mismatch!\n");
		}
		return;
	}

	strcpy(f_path, path);
	strncat(f_path, filename, sizeof(f_path)-1);

	fp = fopen (f_path, "r");
	if(fp == NULL)
	{
		printf("Could Not Find File\n");
		l=sprintf((char *)p_buf[0], "%c%c%c%cFile not found (%s)%c",0x00,0x05,0x00,0x01,f_path,0x00);
		if(sendto(sock,p_buf[0],l,0,(struct sockaddr *)&client, sizeof(client)) != l)
		{
			printf("Mismatch!\n");
		}
		return;
	}

	memset(f_buf, 0, sizeof (f_buf));
	while(1)
	{
		acked = 0;
		ssize = fread(f_buf,1,segment,fp);
		c++;

		if(c == 1)
			bcnt = 0;
		else if(c == 2)
			bcnt = 0;
		else
			bcnt = (c - 2) % ack_rep;

		sprintf((char *) p_buf[bcnt], "%c%c%c%c", 0x00, 0x03, 0x00, 0x00);
		memcpy((char *) p_buf[bcnt]+4, f_buf, ssize);
		l = 4 + ssize;
		p_buf[bcnt][2] = (c & 0xFF00) >> 8;
		p_buf[bcnt][3] = (c & 0x00FF);

		if(sendto(sock,p_buf[bcnt],l,0,(struct sockaddr *)&client, sizeof(client)) != l)
		{
			return;
		}

		if((c-1) == 0 || ((c-1) % ack_rep) == 0 || ssize != segment)
		{
			for (j=0; j < RETRIES; j++)
			{
				client_l = sizeof (ack);
				errno = EAGAIN;
				n = -1;

				for(i=0; errno == EAGAIN && i <= TIMEOUT && n < 0; i++)
				{
					n = recvfrom(sock, recvbuf,sizeof(recvbuf),MSG_DONTWAIT,(struct sockaddr *) &ack,(socklen_t *)&client_l);
					usleep (1000);
				}
				if(n < 0 && errno != EAGAIN)
				{
					printf("Connection Error\n");
				}
				else if(n < 0 && errno == EAGAIN)
				{
					printf("Timeout!\n");
				}
				else
				{
					if(client.sin_addr.s_addr != ack.sin_addr.s_addr)
					{
						j--;
						continue;
					}

					if(tid != ntohs(client.sin_port))
					{
						l = sprintf ((char *) recvbuf,"%c%c%c%cBad/Unknown TID%c",0x00,0x05,0x00,0x05,0x00);
						if(sendto (sock, recvbuf, l, 0, (struct sockaddr *) &client, sizeof (client)) != l)
						{
							printf("Mismatch!\n");
						}
						j--;

						continue;
					}

					idx = (char *) recvbuf;
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
							l = sprintf((char *)recvbuf,"%c%c%c%cIllegal operation%c",0x00, 0x05, 0x00, 0x04, 0x00);
							if(sendto(sock, recvbuf, l, 0, (struct sockaddr *)&client, sizeof(client)) != l)
							{
								printf("Mismatch!\n");
							}
						}
					}
					else
					{
						break;
					}
				}

				for(i=0; i <= bcnt; i++)
				{
					if(sendto(sock, p_buf[i], l, 0, (struct sockaddr *)&client, sizeof(client)) != l)
					{
						return;
					}
				}
			}
		}
		else if(1)
		{
			n = recvfrom(sock,recvbuf,sizeof(recvbuf),MSG_DONTWAIT,(struct sockaddr *)&ack, (socklen_t *)&client_l);
		}

		if(j == RETRIES)
		{
			printf("Timeout! Terminating Transfer!\n");
			fclose(fp);
			return;
		}

		if (ssize != segment)
			break;

		memset(f_buf, 0, sizeof(f_buf));
	}
	fclose(fp);
	printf("***File Successfully Sent***\n\n");
	return;
}

void c_write(char *file_in, struct sockaddr_in client, char *c_mode, int tid)
{
	char filename[128], mode[12];

	FILE *fp;

	strcpy(filename, file_in);
	strcpy(mode, c_mode);

	int sock, l, client_l, opcode, fg = 1, i, j, n;
	unsigned short int c = 0, rc = 0;
	unsigned char f_buf[MAX+1],p_buf[MAX+12];
	extern int errno;
	char f_path[196], ackbuf[512], *idx;
	struct sockaddr_in data;

	if((sock = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		printf ("Server Error!\n");
		return;
	}

	if(!strncasecmp(mode, "octet", 5) && !strncasecmp (mode, "netascii", 8))
	{
		if(!strncasecmp(mode, "mail", 4))
			l=sprintf((char *)p_buf,"%c%c%c%cThis tftp server will not operate as a mail relay%c",0x00,0x05,0x00,0x04,0x00);
		else
			l=sprintf ((char *) p_buf,"%c%c%c%cUnrecognized mode (%s)%c",0x00, 0x05, 0x00, 0x04, mode, 0x00);

		if(sendto(sock,p_buf,l,0,(struct sockaddr *)&client,sizeof(client)) != l)
		{
			printf("Mismatch!\n");
		}
		return;
	}

	if(strchr (filename, 0x5C) || strchr (filename, 0x2F))
	{
		l=sprintf((char *) p_buf,"%c%c%c%cIllegal filename.(%s) You may not attempt to descend or ascend directories.%c",0x00,0x05,0x00,0x00,filename,0x00);
		if(sendto(sock, p_buf, l, 0, (struct sockaddr *) &client, sizeof(client)) != l)
		{
			printf("Mismatch!\n");
		}
		return;

	}

	strcpy(f_path, path);
	strncat(f_path, filename,sizeof(f_path)-1);
	fp = fopen(f_path, "w");
	if(fp == NULL)
	{ 
		printf("Failed To Open File!\n");
		l = sprintf((char *)p_buf,"%c%c%c%cFile cannot be opened for writing (%s)%c",0x00,0x05,0x00,0x02,f_path,0x00);
		if(sendto(sock,p_buf,l,0,(struct sockaddr *)&client,sizeof(client)) != l)
		{
			printf("Mismatch!\n");
		}
		return;
	}

	memset(f_buf, 0, sizeof(f_buf));
	n = segment + 4;
	do
	{
		memset(p_buf, 0, sizeof(p_buf));
		memset(ackbuf, 0, sizeof(ackbuf));

		if(c == 0 || (c % ack_rep) == 0 || n != (segment + 4))
		{
			l = sprintf (ackbuf, "%c%c%c%c", 0x00, 0x04, 0x00, 0x00);
			ackbuf[2] = (c & 0xFF00) >> 8;
			ackbuf[3] = (c & 0x00FF);

			if(sendto(sock, ackbuf, l, 0,(struct sockaddr *)&client,sizeof(client)) != l)
			{
				return;
			}
		}

		if(n != (segment+4))
		{
			goto done;
		}

		memset(f_buf, 0, sizeof(f_buf));
		c++;

		for(j=0; j < RETRIES;j++)
		{
			client_l = sizeof(data);
			errno = EAGAIN;
			n = -1;

			for(i=0; errno == EAGAIN && i <= TIMEOUT && n < 0;i++)
			{
				n = recvfrom(sock,p_buf,sizeof(p_buf)-1,MSG_DONTWAIT,(struct sockaddr *)&data,(socklen_t *) & client_l);
				usleep (1000);
			}

			if(n < 0 && errno != EAGAIN)
			{
				printf("Failed To Receive From Client!\n");
			}
			else if (n < 0 && errno == EAGAIN)
			{
				printf("Timeout!\n");
			}
			else
			{
				if(client.sin_addr.s_addr != data.sin_addr.s_addr)
				{
					j--;
					continue;
				}

				if(tid != ntohs (client.sin_port))
				{
					l = sprintf((char *)p_buf,"%c%c%c%cBad/Unknown TID%c",0x00,0x05,0x00,0x05,0x00);
					if(sendto(sock, p_buf, l, 0, (struct sockaddr *)&client, sizeof(client)) != l)
						printf("Mismatch!\n");
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

				memcpy((char *) f_buf, idx, n - 4);

				if(fg)
				{
					if(n > 516)
						segment = n-4;
					fg = 0;
				}

				if(opcode != 3 || rc != c)
				{
					if(opcode > 5)
					{
						l = sprintf((char *) p_buf,"%c%c%c%cIllegal operation%c",0x00, 0x05, 0x00, 0x04, 0x00);
						if(sendto(sock, p_buf, l, 0, (struct sockaddr *) &client, sizeof (client)) != l)
						{
							printf("Mismatch!\n");
						}
					}
				}
				else
				{
					break;
				}
			}

			if(sendto(sock, ackbuf, l, 0, (struct sockaddr *)&client,sizeof(client)) != l)
			{
				return;
			}
		}

		if(j == RETRIES)
		{
			printf("Timeout! Terminating Transfer\n");
			fclose(fp);
			return;
		}
	}

	while(fwrite(f_buf,1,n-4,fp) == n-4);
	fclose(fp);
	sync();
	printf("xxx*Corrupt*xxx\n");
	return;

done:

	fclose(fp);
	sync();
	printf ("***File Successfully Received!***\n\n");
	return;
}

int main (int argc, char **argv)
{
	char s[100];

	if (argc <= 1)
	{
		printf("Provide <Port Number>\n");
		exit(1);
	}

	int port = atoi(argv[1]);
	int sock;

	printf("Current Working Directory: %s \n",getcwd(s,100));
	printf("Enter File Directory To Use [/home/user_name/..../]: ");
	scanf("%s",path);

	if((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		printf("Failed To Create Socket!\n");
		exit(1);
	}
	else
		printf("Socket Created!\n");

	struct sockaddr_in server, client;

	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(sock,(struct sockaddr *)&server, sizeof(server)) == -1)
	{
		printf("Failed To Bind Server-Socket\n");
		perror("server bind failed: ");
		return (2);
	}
	else
		printf("Socket-Port Bind Successful!\n");

	int n, client_l, pid, status, opt, tid;
	char opcode, *idx, filename[196], mode[12];

	while(1)
	{
		client_l = sizeof(client);
		memset (buf, 0, BUFSIZ);
		n = 0;

		while(errno == EAGAIN || n == 0)
		{
			waitpid(-1, &status, WNOHANG);
			n = recvfrom(sock, buf, BUFSIZ,MSG_DONTWAIT,(struct sockaddr *)&client,(socklen_t *)&client_l);
			if(n < 0 && errno != EAGAIN)
			{
				printf("The server could not receive from the client");
				return 0;
			}
			usleep (1000);
		}

		printf("IP: %s, Port: %d\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));

		idx = buf;
		if (idx++[0] != 0x00)
		{
			return 0;
		}
		tid = ntohs(client.sin_port);
		opcode = *idx++;

		if (opcode == 1 || opcode == 2)
		{
			strncpy (filename,idx,sizeof(filename)-1);
			idx += strlen (filename)+1;

			strncpy (mode, idx,sizeof(mode)-1);
			idx += strlen(mode)+1;
		}

		switch(opcode)
		{
			case 1:
				printf("-----Read-Request From Client!-----\n");
				c_read(filename, client, mode, tid);
				break;
			case 2:
				printf("-----Write-Request From Client-----\n");
				c_write(filename, client, mode, tid);
				break;
			default:
				break;
		}
	}
}