/**
 * @file  coda.c
 * @brief Contiene le implementzioni delle funzioni dichiarate e commentate
 *        in coda.h 
 * @author Daniele Trezza 489554 & Danilo Spano' 465347
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 * originale degli autori.         
 */

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <coda.h>


/* Mutex sulla coda che memorizza i fd dei client da servire*/
static pthread_mutex_t lock_coda = PTHREAD_MUTEX_INITIALIZER;
/* Variabile condition sulla coda*/
static pthread_cond_t  cond_coda = PTHREAD_COND_INITIALIZER;


Coda_t *initCoda() {
  Coda_t *c = malloc(sizeof(Coda_t));
  if(c == NULL) return NULL;
  
  c->head = malloc(sizeof(Node_t));
  if (c->head == NULL) return NULL;
  c->head->data = NULL;
  c->head->next = NULL;
  c->tail = c->head;
  c->len = 0;
  return c;
}

void deleteCoda(Coda_t *c) {
  while(c->head != c->tail) {
    Node_t *p = (Node_t*)c->head;
    c->head = c->head->next;
    free(p);
  }
  if (c->head) free(c->head);
  free(c);
}

int inserisci(Coda_t *c, void *data) {
  assert(data != NULL);
  Node_t *node  = malloc(sizeof(Node_t));
  if(node == NULL)
    return -1;
  
  node->data = data;
  node->next = NULL;
  pthread_mutex_lock(&lock_coda);
  c->tail->next = node;
  c->tail = node;
  (c->len)++;
  pthread_cond_signal(&cond_coda);
  pthread_mutex_unlock(&lock_coda);
  return 0;
}

void *estrai(Coda_t *c) {
  pthread_mutex_lock(&lock_coda);
  while(c->head == c->tail) {
    pthread_cond_wait(&cond_coda, &lock_coda);
  }
  // locked
  assert(c->head->next);
  Node_t *node = (Node_t *)c->head;
  void *data = (c->head->next)->data;
  c->head = c->head->next;
  (c->len)--;
  assert(c->len>=0);
  pthread_mutex_unlock(&lock_coda);
  free(node);
  return data;
}

unsigned long length(Coda_t *c) {
  pthread_mutex_lock(&lock_coda);
  unsigned int len = c->len;
  pthread_mutex_unlock(&lock_coda);
  return len;
}

