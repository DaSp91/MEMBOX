/*
 * membox Progetto del corso di LSO 2016
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Pelagatti, Torquati
 *
 */

/**
 * @file membox.c
 * @brief File principale del server del progetto membox++
 *
 * @author Danilo Spano' 465347 & Daniele Trezza 489554
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 * originale degli autori.
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "connections.h"
#include "icl_hash.h"
#include "message.h"
#include "coda.h" 
#include "stats.h"
#include "lista.h"
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

/* size massima per una riga del file */
#define SIZE_LINEA 150

/* macro per controllare l'esito delle system call */
#define ec_meno1(val, stringa)							\
  if ((val) == -1) {							        \
    perror(stringa);								\
    return -1;									\
  }       

/* macro per l'invio di risposte positive*/
#define successo(risp, connfd, key)						\
  setHeader(&risp, OP_OK, &(key));						\
  write(connfd, &(risp.hdr), sizeof(message_hdr_t));


/*
* GESTIONE DELLE STATISTICHE
*/

/* struttura che memorizza le statistiche del server, struct statistics 
 * e' definita in stats.h.*/
struct statistics  mboxStats = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };

//Mutex sulle statistiche
pthread_mutex_t statlock = PTHREAD_MUTEX_INITIALIZER;

/**
 * @function aggiorna_operazioni
 * @brief aggiorna opportunamente le statistiche
 *
 * @param op operazione, esito distingue se l'operazione è fallita o ha avuto successo 
*/
void aggiorna_operazioni(int op, int esito) {
  pthread_mutex_lock(&statlock);
  switch (op) {
  case PUT_OP: {if(esito)mboxStats.nput++; else mboxStats.nput_failed++;
      break;
  }
  case UPDATE_OP: {if(esito)mboxStats.nupdate++; else mboxStats.nupdate_failed++;
      break;
  }
  case GET_OP: {if(esito)mboxStats.nget++; else mboxStats.nget_failed++;
      break;
  }
  case REMOVE_OP: {if(esito)mboxStats.nremove++; else mboxStats.nremove_failed++;
      break;
  }
  case LOCK_OP: {if(esito)mboxStats.nlock++; else mboxStats.nlock_failed++;
      break;
  }
  case LOCK_OBJ_OP: {if(esito)mboxStats.nobjlock++; else mboxStats.nobjlock_failed++;
      break;
  }
  case DUMP_OP: {if(esito)mboxStats.ndump++; else mboxStats.ndump_failed++;
      break;
  }
  }
  pthread_mutex_unlock(&statlock); 
}



void aggiorna_oggetti(int a){
  //se a>0 incrementa il numero di oggetti nel repository
  if (a) {
    mboxStats.current_objects += a;
    //controlla se è il nuovo massimo n. di oggetti raggiunto.
    if(mboxStats.current_objects > mboxStats.max_objects) mboxStats.max_objects = mboxStats.current_objects;
  }
  //se a <=0, decrementa il numero di oggetti nel repository
  else mboxStats.current_objects--;
}


void incrementa_current_size(int val){
  pthread_mutex_lock(&statlock);
  //incremento la dimensione attuale del repository.
  mboxStats.current_size+=val;
  //confronto la size corrente con la massima size raggiunta, eventualmente aggiorno max_size
  if(mboxStats.current_size > mboxStats.max_size) mboxStats.max_size = mboxStats.current_size;
  pthread_mutex_unlock(&statlock); 
}

void decrementa_current_size(int val){
  //decremento la size attuale del repository.
  pthread_mutex_lock(&statlock);
  mboxStats.current_size-=val;
  pthread_mutex_unlock(&statlock);
}


/**
 * @function stampa_statistiche
 * @brief apre un file in append e vi scrive le statistiche
 * 
 * @param path: path del file da aprire
 * @return 0 in caso di successo, -1 in caso di fallimento.
*/
int stampa_statistiche(char *path) {
  FILE *f;
  if (path == NULL) return -1;
  //apro in append il file per scrivere le statistiche
  pthread_mutex_lock(&statlock);
  f = fopen(path, "a");
  if(f==NULL) {perror("fopen"); return -1;}
  int res = printStats(f);
  fclose(f);
  pthread_mutex_unlock(&statlock); 
  return res;
}


/* struttura che memorizza i dati necessari per il funzionamento del server letti dal file di configurazione*/
struct config {
  char *UnixPath;
  unsigned int MaxConnections;
  unsigned int ThreadsInPool;
  unsigned int StorageSize;
  unsigned int StorageByteSize;
  unsigned int MaxObjSize;
  char *StatFileName;
  char *DumpFile;
};

/* puntatore alla struttura che contiene i parametri di configurazione */
struct config *fc_t;
/* Coda che memorizza i fd dei client da servire */
Coda_t *clienti;

/* Dichiarazione tabella hash */
icl_hash_t *tabella;
/* Mutex sulla tabella hash */
pthread_mutex_t mtxhash = PTHREAD_MUTEX_INITIALIZER;

/* Pool di thread che svolgeranno le operazioni */
pthread_t *threadpool;

/*lista che contiene le lock sugli oggetti del tipo <chiave_oggetto , proprietario_lock>*/
lista_t lista_obj_lock=NULL;
/*variabile mutex per la lista di lock*/
pthread_mutex_t mtxLista = PTHREAD_MUTEX_INITIALIZER;
/*variabile condition per la gestione della coda FIFO*/
pthread_cond_t condLista = PTHREAD_COND_INITIALIZER;
/* Variabile che indica il numero di oggetti attualmente contenuti */
unsigned long ActualStorageSyze = 0;
/* Variabile che indica size occupata dal server*/
unsigned long ActualStorageByteSize = 0;
/* Mutex per le variabili "Actual*" */
pthread_mutex_t mtxAct = PTHREAD_MUTEX_INITIALIZER;

/* Variabile di mutex per repository locked (usata per la LOCK_OP) */
pthread_mutex_t repoLocked = PTHREAD_MUTEX_INITIALIZER;
/* Flag che indica se la repository è stata lockata da una LOCK_OP */
unsigned int islocked = 0;

/* Flag che indica se i thread devono chiudere le connessioni */
unsigned int closing = 0;

/* Variabile mutex per "closing" */
pthread_mutex_t mtxClosing = PTHREAD_MUTEX_INITIALIZER;

/**
 *@function use
 *@brief spiega come attivare correttamente il server
 */
static void use(const char* filename){
  fprintf(stderr, "use:\n %s -f configfile [-d dumpfile]\n\n configfile: percorso del file di configurazione\n dumpfile: eventuale file di dump dal quale caricare il repository\n", filename);
}

/* 
 * Funzione Hash usata nella creazione della tabella hash (presa dai file forniti dai docenti)
 * 
 */
static inline unsigned int fnv_hash_function( void *key, int len ) {
  unsigned char *p = (unsigned char*)key;
  unsigned int h = 2166136261u;
  int i;
  for ( i = 0; i < len; i++ )
    h = ( h * 16777619 ) ^ p[i];
  return h;
}
static inline unsigned int ulong_hash_function( void *key ) {
  int len = sizeof(unsigned long);
  unsigned int hashval = fnv_hash_function( key, len );
  return hashval;
}

/**
 * @function key_compare
 * @brief funzione ausiliare per tabella hash, compara due chiavi
 * @return 1 se le chiavi sono uguali, 0 altrimenti
 */
static int key_compare(void *a, void *b) {
  return *((unsigned int *)a) == *((unsigned int *)b);
}

/**
 * @function free_hash_key
 * @brief funzione ausiliare per tabella hash, libera la chiave dalla memoria
 */
void free_hash_key(void *key) { free(key); }

/**
 * @function free_hash_data
 * @brief funzione ausiliare per tabella hash, libera un elemento dalla memoria
 */
void free_hash_data(void *data) {
  free(((message_data_t *)data)->buf);
  free((message_data_t *)data);
}

/**
 * @function cleanup
 * @brief cancella il file socket, elimina: la coda, la tabella hash, la lista per le lock su oggetti, la struttura di configurazione.
 *
 */
static void cleanup() {
  unlink(fc_t->UnixPath);
  deleteCoda(clienti);
  icl_hash_destroy(tabella, free_hash_key, free_hash_data);
  freeList(lista_obj_lock);
  free(fc_t);
}



/*
 * @function 
 * @brief legge il file riga per riga e setta i vari campi della struttura di configurazione
 *  
 * @param fd puntatore del file(già aperto), fc_t struttura da settare
 * @return -1 in caso di errore, 0 altrimenti
 */
static int leggi_File_config(FILE* fd, struct config *fc_t){
  
  if (fd == NULL || fc_t == NULL){
    errno = EINVAL;
    return -1;
  }
  
  //buffer che conterrà una linea del file di config
  char buf[SIZE_LINEA];
  
  char *token;
  //fino a quando ci sono righe da leggere
  while( fgets(buf,SIZE_LINEA,fd) != NULL){
    
    //controllo che non sia un commento o una riga vuota
    if( buf[0] != '#' && buf[0] != '\n' && buf[0] != ' '){ 
      
      token = strtok(buf, " \t");
      
      //controllo che ci siano i vari paremtri nel file
      if(token != NULL && strcmp(token,"UnixPath") == 0) {
	token = strtok(NULL," =\n");
	strcpy(fc_t->UnixPath,token);
      }
      else if (token != NULL && strcmp(token,"MaxConnections") == 0 ){
	token = strtok(NULL," =\n");
	fc_t->MaxConnections = (int)strtol(token,NULL,10);
      }
      else if (token != NULL && strcmp(token,"ThreadsInPool") == 0 ){
	token = strtok(NULL," =\n");
	fc_t->ThreadsInPool = (int)strtol(token,NULL,10);
      }
      else if (token != NULL && strcmp(token,"StorageSize") == 0 ){
	token = strtok(NULL," =\n");
	fc_t->StorageSize = (int)strtol(token,NULL,10);
      }
      else if (token != NULL && strcmp(token,"StorageByteSize") == 0 ){
	token = strtok(NULL," =\n");
	fc_t->StorageByteSize = (int)strtol(token,NULL,10);
      }
      else if (token != NULL && strcmp(token,"MaxObjSize") == 0 ){
	token = strtok(NULL," =\n");
	fc_t->MaxObjSize = (int)strtol(token,NULL,10);
      }
      else if (token != NULL && strcmp(token,"StatFileName") == 0 ){
	token = strtok(NULL," =\n");
	strcpy(fc_t->StatFileName,token);
      }
      else if (token != NULL && strcmp(token,"DumpFile") == 0 ){
	token = strtok(NULL," =\n");
	strcpy(fc_t->DumpFile,token);
      }
      else{//file mal strutturato
	errno = EBADFD;
	return -1;
      }
    }
  }
  
  return 0;
}


/*Gestore dei segnali*/
static void sighandler(int sig) {
  switch(sig) {
  case SIGINT:  //comportamento identico a sigterm
  case SIGQUIT: //comportamento identico a sigterm
  case SIGTERM: {
    cleanup();
    printf("Server membox terminato con successo!\n");
    exit(EXIT_SUCCESS);
  }
  case SIGUSR1: {
    printf("Scrivo statistiche\n");
    //Per conoscere il numero attuale di connessioni concorrenti, controllo la lunghezza della coda.
    pthread_mutex_lock(&statlock);
    //la funzione length prende la lock sulla coda
    mboxStats.concurrent_connections = length(clienti);
    pthread_mutex_unlock(&statlock); 
    if(stampa_statistiche(fc_t->StatFileName)==-1){printf("errore durante la scrittura sul file delle statistiche\n");}
    break;
  }
  case SIGUSR2: {
    printf("Server membox in terminazione. Non saranno accettate nuove connessioni.\n");
    // smetto di accettare nuove connessioni settando il closing a 1
    pthread_mutex_lock(&mtxClosing);
    closing = 1;
    pthread_mutex_unlock(&mtxClosing);
    // inserisco in coda una connessione fittizia per ogni thread in modo da farli terminare quano la estraggono dalla coda
    int terminazione = -1;
    for (int j = 0; j < fc_t->ThreadsInPool; j++) {
      if(inserisci(clienti, &terminazione) == -1)
	exit(EXIT_FAILURE);
    }
    // aspetto la terminazione dei thread lavoratori (con la join)
    for (int i = 0; i < fc_t->ThreadsInPool; i++) {
      pthread_join(threadpool[i], NULL);
    }
    cleanup();
    
    pthread_exit(EXIT_SUCCESS);
  }
  case SIGPIPE: {;
      // ignoro SIGPIPE per evitare di essere terminato da una scrittura su un socket chiuso
  }
  }
}


/* Thread worker - esegue le operazioni */
void *worker_thread() {
  
  while (1) {
    // estrai aspetta che ci sia almeno un elemento in coda
    int *connfd = estrai(clienti);
    
    // Quando ho ricevuto il segnale SIGUSR2, il valore di connfd è posto a -1.
    if (*connfd == -1) {
      printf("Thread in terminazione\n");
      pthread_exit((void *)EXIT_SUCCESS);
    }
    
    message_t msg, reply;
    
    // leggo eventuali messaggi
    int letti = 0;
    while ((letti = read(*connfd, (char*)&(msg.hdr), sizeof(message_hdr_t))) != 0){ 
      if (letti == -1) {
        if (errno == ECONNRESET)
          break; // il client ha terminato la connessione prima della lettura
        setHeader(&msg, OP_FAIL, &(msg.hdr.key));
        write(*connfd, &msg.hdr, sizeof(message_hdr_t));
        errno = 0;
        break;
      }
      
      // controllo se il repository è locked
      pthread_mutex_lock(&repoLocked);
      //Se il repository è lockato e la lock non è della connessione che sto analizzando
      //restituisco un messaggio di errore
      if (islocked && *connfd != islocked) {
        aggiorna_operazioni(msg.hdr.op, 0);
        setHeader(&reply, OP_LOCKED, &(msg.hdr.key));
        write(*connfd, &reply.hdr, sizeof(message_hdr_t));
        pthread_mutex_unlock(&repoLocked);
        break;
      }
      pthread_mutex_unlock(&repoLocked);
      
      //Se l'operazione è LOCK_OBJ_OP e l'oggetto è locked da qualcun'altro, mi metto in attesa
      if(msg.hdr.op == LOCK_OBJ_OP){
	pthread_mutex_lock(&mtxLista);
	while(cerca(lista_obj_lock,msg.hdr.key)!=0 && cerca(lista_obj_lock,msg.hdr.key)!=*connfd){
	  pthread_cond_wait(&condLista, &mtxLista);
	}
	pthread_mutex_unlock(&mtxLista);
      }
      
      // eseguo le varie operazioni
      aggiorna_operazioni(msg.hdr.op, 1);
      switch (msg.hdr.op) {
      case PUT_OP: {
        // leggo i dati
        if (readData(*connfd, &(msg.data)) == -1){
          setHeader(&reply, OP_FAIL, &(msg.hdr.key));
          write(*connfd, &reply.hdr, sizeof(message_hdr_t));
          aggiorna_operazioni(msg.hdr.op, 0);
          break;
        }
	
	// prendo la lock sulle variabili actual
        pthread_mutex_lock(&mtxAct);
        // verifico tutte le eventuali condizioni di errore
        if (ActualStorageSyze == fc_t->StorageSize && fc_t->StorageSize != 0){
          aggiorna_operazioni(msg.hdr.op, 0);
	  setHeader(&reply, OP_PUT_TOOMANY, &(msg.hdr.key));
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));				
	  pthread_mutex_unlock(&(mtxAct));
	  break;
        }
        if ( ((ActualStorageByteSize+msg.data.len) > fc_t->StorageByteSize) && fc_t->StorageByteSize != 0){
          aggiorna_operazioni(msg.hdr.op, 0);
	  setHeader(&reply, OP_PUT_REPOSIZE, &(msg.hdr.key));						
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t)); 					
	  pthread_mutex_unlock(&(mtxAct));
          break;
        }
        if ((msg.data.len > fc_t->MaxObjSize && fc_t->MaxObjSize != 0)) {
          aggiorna_operazioni(msg.hdr.op, 0);
          setHeader(&reply, OP_PUT_SIZE, &(msg.hdr.key));						
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));					
	  pthread_mutex_unlock(&(mtxAct));
          break;
        }
        pthread_mutex_unlock(&mtxAct);
        // acquisisco la mutex sulla tabella
        pthread_mutex_lock(&mtxhash);
        message_data_t *data = icl_hash_find(tabella, &(msg.hdr.key));
        if (data != NULL) {
          // chiave già presente!!
          aggiorna_operazioni(msg.hdr.op, 0);
	  setHeader(&reply, OP_PUT_ALREADY, &(msg.hdr.key));						
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t)); //quii					
	  pthread_mutex_unlock(&(mtxhash));
          break;
        }
	
        // Alloco l'oggetto da salvare
        unsigned long *tmp_key = malloc(sizeof(unsigned long)); //long/int
        if (tmp_key == NULL) {
          free(msg.data.buf);
          setHeader(&reply, OP_FAIL, &(msg.hdr.key));
          write(*connfd, &reply.hdr, sizeof(message_hdr_t)); //quii
          break;
        }
        *tmp_key = msg.hdr.key;
	
        message_data_t *tmp_data = malloc(sizeof(msg.data));
        if (tmp_data == NULL) {
          free(msg.data.buf);
          free(tmp_key);
          setHeader(&reply, OP_FAIL, &(msg.hdr.key));
          write(*connfd, &reply.hdr, sizeof(message_hdr_t)); //qui
          break;
        }
        tmp_data->len = msg.data.len;
        tmp_data->buf = msg.data.buf;
	
        if ((icl_hash_insert(tabella, tmp_key, tmp_data)) == NULL) {
          // errore nella icl_hash_insert
          aggiorna_operazioni(msg.hdr.op, 0);
          free(tmp_key);
          free(tmp_data->buf);
          free(tmp_data);
	  setHeader(&reply, OP_FAIL, &(msg.hdr.key));						
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));	//qui				
	  pthread_mutex_unlock(&(mtxhash));
          break;
        }
        // inserimento è andato a buon fine
        pthread_mutex_unlock(&mtxhash);
        // incremento i valori Actual
        pthread_mutex_lock(&mtxAct);
        ActualStorageSyze++;
        ActualStorageByteSize += msg.data.len;
        aggiorna_oggetti(1); // aggiunto oggetto al repo
        incrementa_current_size(msg.data.len);
        pthread_mutex_unlock(&mtxAct);
        successo(reply, *connfd, msg.hdr.key);
        break;
      }
      case UPDATE_OP: {
        // leggo i dati
        if (readData(*connfd, &(msg.data)) == -1) {
          setHeader(&reply, OP_FAIL, &(msg.hdr.key));
          write(*connfd, &reply.hdr, sizeof(message_hdr_t));
          break;
        }
	
        // verifico se è presente la chiave
        pthread_mutex_lock(&mtxhash);
        message_data_t *data = icl_hash_find(tabella, &(msg.hdr.key));
        if (data == NULL) {
          // chiave non presente
          free(msg.data.buf);
          aggiorna_operazioni(UPDATE_OP, 0);
	  setHeader(&reply, OP_UPDATE_NONE, &(msg.hdr.key));						
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));					
	  pthread_mutex_unlock(&(mtxhash));
          break;
        }
        if (data->len != msg.data.len) {
          // errore, len diverse
          free(msg.data.buf);
          aggiorna_operazioni(UPDATE_OP, 0);
	  setHeader(&reply, OP_UPDATE_SIZE, &(msg.hdr.key));						
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));					
	  pthread_mutex_unlock(&(mtxhash));
          break;
        }
	
        // verificate le condizioni di errore aggiorno
        free(data->buf);
       	data->buf = msg.data.buf;
     
        // l'inserimento è andato a buon fine, rilascio la lock sulla tabella
        pthread_mutex_unlock(&mtxhash);
        successo(reply, *connfd, msg.hdr.key);
        break;
      }
      case GET_OP: {
        // acquisisco la mutex sulla tabella
        pthread_mutex_lock(&mtxhash);
        message_data_t *data = icl_hash_find(tabella, &(msg.hdr.key));
        if (data == NULL) {
          // chiave non presente!!
          aggiorna_operazioni(GET_OP, 0);
	  setHeader(&reply, OP_GET_NONE, &(msg.hdr.key));						
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));					
	  pthread_mutex_unlock(&(mtxhash));
          break;
        }
        pthread_mutex_unlock(&mtxhash);
        // mando l'oggetto al client
        setHeader(&reply, OP_OK, &(msg.hdr.key));
        setData(&reply, data->buf, data->len);
        sendRequest(*connfd, &reply);
        break;
      }
      case REMOVE_OP: {
        // verifico se è presente la chiave
        pthread_mutex_lock(&mtxhash);
        message_data_t *data = icl_hash_find(tabella, &(msg.hdr.key));
        if (data == NULL) {
          // chiave non presente!!
          aggiorna_operazioni(REMOVE_OP, 0);
	  setHeader(&reply, OP_REMOVE_NONE, &(msg.hdr.key));						
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));					
	  pthread_mutex_unlock(&(mtxhash));
          break;
        }
        int len = data->len;
        if (icl_hash_delete(tabella, &(msg.hdr.key), free_hash_key, free_hash_data) == -1) {
          // errore in cancellazione
          aggiorna_operazioni(REMOVE_OP, 0);
	  setHeader(&reply, OP_FAIL, &(msg.hdr.key));						
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));					
	  pthread_mutex_unlock(&(mtxhash));
          break;
        }
        // cancellazione è andata a buon fine
        pthread_mutex_unlock(&mtxhash);
        // decremento i valori Actual
        pthread_mutex_lock(&mtxAct);
        ActualStorageSyze--;
        ActualStorageByteSize -= len;
        aggiorna_oggetti(0); // rimosso oggetto dal repo
        decrementa_current_size(len);
        pthread_mutex_unlock(&mtxAct);
        successo(reply, *connfd, msg.hdr.key);
        break;
      }
      case LOCK_OP: {
        // setto la lock
        pthread_mutex_lock(&repoLocked);
        islocked = *connfd;
        pthread_mutex_unlock(&repoLocked);
        successo(reply, *connfd, msg.hdr.key);
        break;
      }
      case UNLOCK_OP: {
        // controllo se il sistema è instato di lock
        pthread_mutex_lock(&repoLocked);
        if (islocked) {
          islocked = 0;
          successo(reply, *connfd, msg.hdr.key);
        } else {
          setHeader(&reply, OP_LOCK_NONE, &(msg.hdr.key));
          write(*connfd, &reply.hdr, sizeof(message_hdr_t));
        }
        pthread_mutex_unlock(&repoLocked);
        break;
      }
	
      case LOCK_OBJ_OP: { 
      	//controllo che l'oggetto da lockare sia nel repository
	pthread_mutex_lock(&mtxhash);
	message_data_t *data = icl_hash_find(tabella, &(msg.hdr.key));
	if (data == NULL) {
	  // chiave non presente!!
	  setHeader(&reply, OP_LOCK_OBJ_NONE, &(msg.hdr.key));
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));
	  aggiorna_operazioni(msg.hdr.op, 0);
	  pthread_mutex_unlock(&mtxhash);
	  break;
	}
	pthread_mutex_unlock(&mtxhash);
	
	//in questo punto, se l'oggetto è in lista, il proprietario è questo client
	pthread_mutex_lock(&mtxLista);
	if(cerca(lista_obj_lock, msg.hdr.key)==*connfd){
	  //lock su un oggetto già lockato dallo stesso utente
	  setHeader(&reply, OP_LOCK_OBJ_DLK, &(msg.hdr.key));
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));
	  aggiorna_operazioni(msg.hdr.op, 0);
	  pthread_mutex_unlock(&mtxLista);
	  break;
	}
	
	int i= insertList (&lista_obj_lock, msg.hdr.key, *connfd);
	if(i==0){
	  setHeader(&reply, OP_FAIL, &(msg.hdr.key));
	  write(*connfd, &reply.hdr, sizeof(message_hdr_t));
	  aggiorna_operazioni(msg.hdr.op, 0);
	  pthread_mutex_unlock(&mtxLista);
	  break;
	}
	pthread_mutex_unlock(&mtxLista);
        successo(reply, *connfd, msg.hdr.key);
	break;
      }
      case UNLOCK_OBJ_OP:{
      	pthread_mutex_lock(&mtxLista);
      	removeList (&lista_obj_lock, msg.hdr.key);
      	pthread_cond_signal(&condLista);
	pthread_mutex_unlock(&mtxLista);
	successo(reply, *connfd, msg.hdr.key);
      	break;
      }
      case DUMP_OP:{
	//controllo se il sistema è in stato di lock
	//e che ci sia il path del DumpFile nel file di configrazione
	
	pthread_mutex_lock(&repoLocked);
        if (islocked && fc_t->DumpFile != NULL) {
	  
	  FILE *fd;
	  if( (fd = fopen(fc_t->DumpFile,"rb")) != NULL){ 
	    //il file DumpFile esiste già, quindi lo rinomino.
	    //chiudo il file DumpFile prima di rinominarlo
	    fclose(fd);
	    
	    char strbak[30];
	    //rinomino DumpFile in DumpFile.bak (se DumpFile.bak esiste gia' viene sovrascritto)
	    strcpy(strbak,fc_t->DumpFile);
	    strcat(strbak, ".bak");
	    if( (rename(fc_t->DumpFile, strbak)) == -1 ){
	      //errore nella rename
	      setHeader(&reply, OP_NO_DUMP, &(msg.hdr.key));
	      write(*connfd, &reply.hdr, sizeof(message_hdr_t));
	      aggiorna_operazioni(msg.hdr.op, 0);
	      pthread_mutex_unlock(&repoLocked);
	      break;
	    }
	    //rinomina con successo!
	  }
	  else{//fopen.. == NULL
	    if(errno != ENOENT ){
	      //errore nella open
	      setHeader(&reply, OP_NO_DUMP, &(msg.hdr.key));
	      write(*connfd, &reply.hdr, sizeof(message_hdr_t));
	      aggiorna_operazioni(msg.hdr.op, 0);
	      pthread_mutex_unlock(&repoLocked);
              break;
	    }
	  }
	  //In questo punto:
	  //-> o ho rinominato il file Dump esistente in nomefile.bak;
	  //-> oppure non esisteva nessun file di Dump;
	  //in entrambi i casi creo il nuovo file Dumpfile
	  if( (fd = fopen(fc_t->DumpFile,"wb")) == NULL){
	    //errore nella open
	    setHeader(&reply, OP_NO_DUMP, &(msg.hdr.key));
	    write(*connfd, &reply.hdr, sizeof(message_hdr_t));
	    aggiorna_operazioni(msg.hdr.op, 0);
	    pthread_mutex_unlock(&repoLocked);
            break;
	  }
	  //scrivo su file il contenuto del repository
	  // prendo la lock sulle variabili actual
	  pthread_mutex_lock(&mtxAct);
	  //scrivo il numero di oggetti
	  if(fwrite(&(ActualStorageSyze),sizeof(unsigned long),1,fd) == 0){
	    setHeader(&reply, OP_FAIL, &(msg.hdr.key));
	    write(*connfd, &reply.hdr, sizeof(message_hdr_t));
	    aggiorna_operazioni(msg.hdr.op, 0);
	    pthread_mutex_unlock(&repoLocked);
	    pthread_mutex_unlock(&mtxAct);
            break;
	  }
	  //scrivo la size dello storage
	  if(fwrite(&(ActualStorageByteSize),sizeof(unsigned long),1,fd) == 0){
	    setHeader(&reply, OP_FAIL, &(msg.hdr.key));
	    write(*connfd, &reply.hdr, sizeof(message_hdr_t));
	    aggiorna_operazioni(msg.hdr.op, 0);
	    pthread_mutex_unlock(&repoLocked);
	    pthread_mutex_unlock(&mtxAct);
            break;
	  }
	  pthread_mutex_unlock(&mtxAct);
	  //scrivo il contenuto del repository:
	  //ho modificato la funzione icl_hash_dump in modo che stampi il formato corretto
	  pthread_mutex_lock(&mtxhash);
	  if( icl_hash_dump(fd,tabella) == -1){
	    setHeader(&reply, OP_NO_DUMP, &(msg.hdr.key));
	    write(*connfd, &reply.hdr, sizeof(message_hdr_t));
	    aggiorna_operazioni(msg.hdr.op, 0);
	    pthread_mutex_unlock(&repoLocked);
	    pthread_mutex_unlock(&mtxhash);
 	    break;
	  }
	  fclose(fd);
	  //copia su file avvenuta con successo!
	  pthread_mutex_unlock(&mtxhash);
	}
	//se il sistema non è in stato di lock oppure non esiste il path del dumpfile,
	//restituisco un codice di errore
	else {
          setHeader(&reply, OP_NO_DUMP, &(msg.hdr.key));
          write(*connfd, &reply, sizeof(message_hdr_t));
	  aggiorna_operazioni(msg.hdr.op, 0);
	  pthread_mutex_unlock(&repoLocked);
	  break;
        }
	pthread_mutex_unlock(&repoLocked);
	successo(reply, *connfd, msg.hdr.key);
	break;
      }
	
      default: {
        // operazione non riconosciuta
        membox_key_t errore;
        errore = 0;
        setHeader(&reply, OP_FAIL, &errore);
        write(*connfd, &reply.hdr, sizeof(message_hdr_t));
      }
        // ho finito lo switch delle operazioni
      }
      // fine ciclo client
    }
    // ho finito di leggere
    // chiudo il fd del client
    
    pthread_mutex_lock(&repoLocked);
    if (islocked && *connfd == islocked)
      islocked = 0;
    pthread_mutex_unlock(&repoLocked);
    
    pthread_mutex_lock(&mtxLista);
    lista_t lockedObjects = NULL;
    //se il client non ha fatto le Unlock sugli oggetti lockati
    //provveddo a slockarli prima di uscire 
    cerca_client(lista_obj_lock, *connfd, &lockedObjects);
    while(lockedObjects != NULL){
      removeList (&lista_obj_lock, lockedObjects->chiave);
      pthread_cond_signal(&condLista);      
      lockedObjects = lockedObjects->next;
    }
    pthread_mutex_unlock(&mtxLista);
    
    close(*connfd);
    free(connfd);
  }
  pthread_exit((void *)EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
  if(argc!=3 && argc!=5 ){
    use(argv[0]);
    exit(EXIT_FAILURE);
  }
  fflush(stdout);
  
  /* Maschero i segnali */
  sigset_t mask;
  ec_meno1(sigemptyset(&mask), "sigemptyset");
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGQUIT);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGUSR1);
  sigaddset(&mask, SIGUSR2);
  sigaddset(&mask, SIGPIPE);
  //blocco i segnali finchè non ho finito l'installazione degli handler
  if(sigprocmask(SIG_BLOCK, &mask, NULL) == -1){
    perror("sigprocmask");
    exit(EXIT_FAILURE);
  }
  
  //installo il signal handler per tutti i segnali che mi interessano
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler=sighandler;
  if(sigaction(SIGINT, &sa, NULL) == -1) {perror("sigaction"); exit(EXIT_FAILURE);}
  if(sigaction(SIGQUIT, &sa, NULL) == -1) {perror("sigaction"); exit(EXIT_FAILURE);}
  if(sigaction(SIGTERM, &sa, NULL) == -1) {perror("sigaction"); exit(EXIT_FAILURE);}
  if(sigaction(SIGUSR1, &sa, NULL) == -1) {perror("sigaction"); exit(EXIT_FAILURE);}
  if(sigaction(SIGUSR2, &sa, NULL) == -1) {perror("sigaction"); exit(EXIT_FAILURE);}
  if(sigaction(SIGPIPE, &sa, NULL) == -1) {perror("sigaction"); exit(EXIT_FAILURE);}
  
  /*tolgo la maschera*/
  ec_meno1( sigemptyset(&mask), "sigemptyset");
  /*riabilito la ricezione dei segnali con i nuovi gestori*/
  ec_meno1( sigprocmask(SIG_SETMASK, &mask, NULL), "sigprocmask");
  
  /* Inizializzo la coda dei lavori */
  clienti = initCoda();
  if (!clienti) {
    fprintf(stderr, "initCoda fallita\n");
    exit(errno);
  }
  
  /* Inizializzo la tabella hash (1000 buckets)*/
  tabella = icl_hash_create(1000, ulong_hash_function, key_compare);
  if (tabella == NULL) {
    perror("creazione tabella");
    exit(EXIT_FAILURE);
  }
  /* Alloco la struttura dove memorizzare la configurazione*/
  fc_t = malloc(sizeof(struct config));
  if(fc_t==NULL){perror("malloc"); exit(EXIT_FAILURE);}
  
  char UP[SIZE_LINEA], SF[SIZE_LINEA], DF[SIZE_LINEA];
  fc_t->UnixPath = UP;
  
  fc_t->StatFileName = SF;
  
  fc_t->DumpFile = DF;
  
  
  /* Leggo file di configurazione e salvo i dati*/
  int opt;
  while ((opt = getopt(argc, argv, "f:d:")) != -1) {
    switch (opt) {
    case 'f':{
      FILE *f;
      if ((f = fopen(argv[2], "r")) == NULL){
	fprintf(stderr, "Aprendo il file di configurazione\n");
	exit(EXIT_FAILURE);
      }
      
      if(leggi_File_config(f, fc_t) == -1){
	perror("leggendo il file di configurazione\n");
        exit(EXIT_FAILURE);
      }

      if (fclose(f) != 0){
	fprintf(stderr, "chiusura file\n");
	exit(EXIT_FAILURE);
      }
      break;
    }/*chiudo il case 'f'*/
      
    case 'd':{
      FILE *f;
      
      if ((f = fopen(argv[optind - 1], "rb")) == NULL){
	fprintf(stderr, "Aprendo il file di dump\n");
	exit(EXIT_FAILURE);
      }
      unsigned long numobj, size_storage;
      /*leggo il #oggetti e la size dello storage*/
      if(fread(&(numobj),sizeof(unsigned long),1,f) == 0){
	fprintf(stderr, "fread-numobj\n");
	exit(EXIT_FAILURE);
      }
     
      if(fread(&(size_storage),sizeof(unsigned long),1,f) == 0){
	fprintf(stderr, "fread-size_storage\n");
	exit(EXIT_FAILURE);
      }
     
      // verifico le eventuali condizioni di errore su numobj e size_storage
      if ( (numobj > fc_t->StorageSize) && fc_t->StorageSize != 0) {
	fprintf(stderr, "numobj-limit\n");
	exit(EXIT_FAILURE);
      }
      if ((size_storage > fc_t->StorageByteSize) && fc_t->StorageByteSize != 0) {
	fprintf(stderr, "size_storage-limit\n");
	exit(EXIT_FAILURE);
      }

      unsigned long *key_tmp;
      message_data_t *data_tmp;
      //leggo tutti gli oggetti da inserire nel repo dal file 
      for(int i = 0; i<numobj; i++){
	
        key_tmp = malloc(sizeof(unsigned long)); 
        if (key_tmp == NULL) {
	  fprintf(stderr, "malloc-key_tmp\n");
	  exit(EXIT_FAILURE);
        }

	//leggo il valore della chiave e lo inserisco in key_tmp
	if(fread(&(*key_tmp),sizeof(unsigned long),1,f) == 0){
	  free(key_tmp);
	  fprintf(stderr, "fread-key_tmp\n");
	  exit(EXIT_FAILURE);
      	}
	
	data_tmp = malloc(sizeof(message_data_t));
        if (data_tmp == NULL) {
	  free(key_tmp);
	  fprintf(stderr, "malloc-data_tmp\n");
	  exit(EXIT_FAILURE);
	}

	//leggo la lunghezza dell'oggetto lo inserisco in data_tmp->len
	if(fread(&(data_tmp->len), sizeof(unsigned int),1,f) == 0){
	  free(key_tmp);
	  free(data_tmp);
	  fprintf(stderr, "fread-data_tmp->len\n");
	  exit(EXIT_FAILURE);
      	}

	//controllo eventuali errori sulla lunghezza dell'oggetto
	if ( (data_tmp->len > fc_t->MaxObjSize) && fc_t->MaxObjSize != 0){
	  fprintf(stderr, "MaxObjSize-limit\n");			
	  pthread_mutex_unlock(&(mtxhash));
	  exit(EXIT_FAILURE);
        }
	
	data_tmp->buf = malloc(data_tmp->len * sizeof(char));
	if(fread(data_tmp->buf,sizeof(char),data_tmp->len,f) == 0){
	  free(key_tmp);
	  free(data_tmp);
	  fprintf(stderr, "fread-data_tmp->buf\n");
	  exit(EXIT_FAILURE);
      	}

	//Acquisisco la lock sulla tabella e controllo se la chiave da inserire
	//è gia' presente nel repo (se ad esempio nel file di dump esistono due chiavi uguali)
	pthread_mutex_lock(&(mtxhash));
      	message_data_t *data = icl_hash_find(tabella, &(key_tmp));
        if (data != NULL) {
	  free(key_tmp);
	  free(data_tmp->buf);
	  free(data_tmp);
	  fprintf(stderr, "chiave-presente\n");
	  pthread_mutex_unlock(&(mtxhash));
	  exit(EXIT_FAILURE);
        }
	
	//inserisco l'oggetto letto nel repo
        if ((icl_hash_insert(tabella, key_tmp, data_tmp)) == NULL) {
          // errore nella icl_hash_insert
          free(key_tmp);
	  free(data_tmp->buf);
	  free(data_tmp);
	  fprintf(stderr, "insert\n");
	  pthread_mutex_unlock(&(mtxhash));
          exit(EXIT_FAILURE);
        }
	// inserimento è andato a buon fine
        pthread_mutex_unlock(&mtxhash);
      }
      
      if (fclose(f) != 0){
	fprintf(stderr, "chiusura file dump\n");
	exit(EXIT_FAILURE);
      }

      char strbak[30];
      //rinomino DumpFile in DumpFile.bak (se DumpFile.bak esiste gia' viene sovrascritto)
      strcpy(strbak,argv[optind - 1]);
      strcat(strbak, ".bak");
      if( (rename(argv[optind - 1], strbak)) == -1 ){
	//errore nella rename
	fprintf(stderr,"rename\n");
	exit(EXIT_FAILURE);
      }
      //rinomina con successo!

      //creo il file di dump vuoto 
      if( (f = fopen(argv[optind - 1],"wb")) == NULL){
	//errore nella open
	fprintf(stderr,"fopen-wb\n");
	exit(EXIT_FAILURE);
      }
      if (fclose(f) != 0){
	fprintf(stderr, "fclose-wb\n");
	exit(EXIT_FAILURE);
      }

      //incremento i valori Actual e aggiorno le statistiche
      ActualStorageSyze += numobj;
      ActualStorageByteSize += size_storage;
      aggiorna_oggetti(numobj); 
      incrementa_current_size(size_storage);
      break;
    }
    default: 
      use(argv[0]);
      exit(EXIT_FAILURE);
    }/*chiudo lo switch(opt)*/
  }/*chiudo il while(opt=..)*/
  
  /* Creo il pool di thread */
  threadpool = malloc(fc_t->ThreadsInPool * sizeof(pthread_t));
  for (int i = 0; i < fc_t->ThreadsInPool; i++)
    if(pthread_create(&(threadpool[i]), NULL, worker_thread, NULL ) != 0) {
      perror("pthread_create");
      exit(EXIT_FAILURE);
    }
  
  int listenfd;
  ec_meno1((listenfd = openConnectionServer(fc_t->UnixPath)), "openConnecionServer");
  
  printf("Server attivo.\n");
  
  while (1) {
    // il server entra nel ciclo infinito
    // verifica se deve accettare altre connessioni
    pthread_mutex_lock(&mtxClosing);
    if (closing) {
      pthread_mutex_unlock(&mtxClosing);
      // esco senza eliminare gli altri thread
      pthread_exit(EXIT_SUCCESS);
    }
    pthread_mutex_unlock(&mtxClosing);
    int connfd;
    if ((connfd = accept(listenfd, (struct sockaddr *)NULL, NULL)) == -1) {
      perror("accept");
    }
    else{
      int l = length(clienti);
      if (l >= fc_t->MaxConnections) {
	// troppe connessioni
	message_t reply;
	membox_key_t r = -1;
	setHeader(&reply, OP_FAIL, &r);
	write(connfd, &reply.hdr, sizeof(message_hdr_t));
	close(connfd);
      }
      else{
	int *connfd_tmp = malloc(sizeof(int));
	*connfd_tmp = connfd;
	if(inserisci(clienti, connfd_tmp) == -1)
	  exit(EXIT_FAILURE);
      }
    }
  }
  return 0;
}
