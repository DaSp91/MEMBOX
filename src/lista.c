/** 
 * @file lista.c
 * @brief Contiene le implementzioni delle funzioni dichiarate e commentate
 *        in lista.h 
 *
 * @author Daniele Trezza 489554 & Danilo Spano' 465347
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 * originale degli autori.  
 */

#include <stdio.h>
#include <stdlib.h>
#include "lista.h"


void freeList(lista_t l){
  if (l == NULL) return;
  lista_t aux;
  aux = l->next;
  free(l);
  l=NULL;
  freeList(aux);
}

int cerca (lista_t l, unsigned long n) {
  
  if (l==NULL)
    return 0;
  else if (l->chiave==n)
    return l->fd_possessore;
  else return cerca(l->next, n); 
}

int cerca_client (lista_t l, int fd, lista_t * out) {
  
  if (l==NULL)
    return 0;
  else {
    while(l != NULL){
      if (l->fd_possessore == fd)
	insertList(out, l->chiave, l->fd_possessore);
      
      l = l->next;
    }
    return 1;
  }		
}
int insertList (lista_t *l, unsigned long k, int fd){
  
  lista_t aux=malloc(sizeof(nodo));
  if(aux==NULL) return 0;
  aux->chiave=k;
  aux->fd_possessore=fd;
  aux->next=*l;
  *l=aux;
  return 1;
}

void removeList (lista_t *l, unsigned long k){
  lista_t prec, succ, aux;
  if (*l!=NULL) {
    if((*l)->chiave==k) {
      aux=*l;
      *l=(*l)->next;
      free(aux);
    }
    
    else {
      
      prec=*l;
      succ=prec->next;
      
      while(succ!=NULL) {
	if(succ->chiave==k) {
	  prec->next=succ->next;
	  free(succ);
	  break;
	}
	prec=prec->next;
	succ=succ->next;
      }
    }
    
    if(succ==NULL && prec->chiave==k)
      free(prec);
  }
}


/*funzione ausiliaria*/
void stampaList(lista_t l){
  
  printf("{");
  while (l!=NULL){
    printf("(%ld,%d)", l->chiave,l->fd_possessore);
    l=l->next;
    if (l!=NULL) printf ("->"); 
  }
  printf("}\n");
  
}
