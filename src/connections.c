/**
 * @file connections.c
 * @brief Contiene le implementzioni delle funzioni dichiarate e commentate
 *        in connections.h 
 * @author Danilo Spano' 465347 & Daniele Trezza 489554
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 * originale degli autori.
 */

#define _POSIX_C_SOURCE 200809L
#include "connections.h"
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <string.h>


int openConnectionServer(char* path){
  //controllo il parametro
  if(path==NULL){
    errno=EINVAL;
    perror("parametro non valido");
    return -1;
  }
  
  int fd_skt;
  struct sockaddr_un sa;
  memset(&sa, 0, sizeof(struct sockaddr_un));
  strncpy(sa.sun_path, path, UNIX_PATH_MAX);
  sa.sun_family=AF_UNIX;
  
  fd_skt=socket(AF_UNIX,SOCK_STREAM,0);
  if(fd_skt==-1){
    perror("socket");
    return -1;
  }
  if(bind(fd_skt,(struct sockaddr*)&sa, sizeof(sa))!=0){
    perror("bind");
    close(fd_skt);
    remove(path);
    return -1;
  }
  if(listen(fd_skt, SOMAXCONN)==-1){
    perror("listen");
    close(fd_skt);
    remove(path);
    return -1;
  }
  return fd_skt;
}


int openConnection(char* path, unsigned int ntimes, unsigned int secs){
  //controllo i parametri
  if(path==NULL || ntimes<=0 || secs<=0){
    errno=EINVAL;
    perror("parametri non validi");
    return -1;
  }
  
  int fd_skt;
  struct sockaddr_un sa;
  memset(&sa, 0, sizeof(struct sockaddr_un));
  strncpy(sa.sun_path, path, UNIX_PATH_MAX);
  sa.sun_family=AF_UNIX;
  
  fd_skt=socket(AF_UNIX,SOCK_STREAM,0);
  
  if(fd_skt==-1){
    perror("socket");
    return -1;
  }
  
  int connesso=0;
  unsigned int i=0;
  while(connesso!=1 && i<ntimes){
    int k=connect(fd_skt, (struct sockaddr*)&sa, sizeof(sa));
    
    if(k==0)connesso=1;
    else {
      if(errno == ENOENT){
	i++;
	errno = 0;
	sleep(secs);
      }
      else{
	perror("connect");
	close(fd_skt);
	return -1;
      }
    }
  }
  if(connesso==1) return fd_skt;
  else return -1;
}


int readHeader(long fd, message_hdr_t *hdr){
  if(hdr==NULL){
    errno = EINVAL;
    return -1;
  }
  
  int ris = read(fd, (char *)hdr, sizeof(message_hdr_t));
  if (ris == -1) return -1;
  
  return 0;
}


int readData(long fd, message_data_t *data){
  
  if(data==NULL){
    errno = EINVAL;
    return -1;
  }
  
  int len = sizeof(message_data_t);
  //devo leggere prima la len
  
  int letti = 0;
  letti = read(fd, (char*) &(data->len), sizeof(data->len));
  
  if(letti==-1) return -1;
  
  if (data->len == 0) return 0;
  
  letti = 0;
  len = data->len;
  //alloco lo spazio necessario per il corpo del messaggio
  data->buf = malloc(len*sizeof(char));
  //controllo se la malloc è andata a buon fine
  if(data->buf == NULL)
    return -1;
  
  int letti_temp = 0;
  while( (letti = read(fd, (data->buf) + letti_temp, len - letti_temp)) < len){
    
    //controllo se sono a fine lettura
    if(letti == 0) break;  
    //controllo se l'errore è dovuto a un segnale
    if(letti < 0 && errno != EINTR)
      return -1;
    errno = 0;
    letti_temp += letti;
  }
  
  return 0;
}


int sendRequest(long fd, message_t *msg){
  if(msg==NULL){
    errno = EINVAL;
    return -1;
  }
  
  
  if( write(fd, &(msg->hdr), sizeof(msg->hdr)) < 0) return -1;
  
  //se è il caso, il messaggio
  
  if (msg->data.len == 0) return 0;
  
  if( write(fd, &(msg->data.len), sizeof(msg->data.len)) < 0) return -1;
  
  int scritti = 0;
  
  int len = (msg->data).len;
  
  scritti = write(fd, ((msg->data).buf), len);
  
  //controllo che abbia scritto tutti i byte del messaggio
  if(scritti != len) return -1;
  
  return 0;
}
