/**  
 * @file lista.h
 * @brief Contiene le dichiarazioni delle funzioni implementate in lista.c
 *
 * @author Danilo Spano' 465347 & Daniele Trezza 489554
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 * originale degli autori.  
 */

#ifndef LISTA_H_
#define LISTA_H_

/*elemento della lista*/
typedef struct nodo{
  unsigned long chiave;
  int fd_possessore;
  struct nodo * next;
} nodo;

typedef nodo * lista_t;


/**
 * @function freeList
 * @brief funzione che libera l'intera lista
 *
 * @param l la lista da liberare 
 * 
 */
void freeList (lista_t l);

/**
 * @function cerca
 * @brief funzione che data una chiave n cerca una coppia <chiave-possessore> nella lista l
 *
 * @param l lista in cui cercare 
 * @param n chiave da cercare nella lista
 *
 * @return un intero corrispondente al possessore di quella chiave n caso di successo, 0 altrimenti (sono sicuro che l'elemento sia unico, grazie all'operazione di inserimento
 *
 */
int cerca (lista_t l, unsigned long n);

/**
 * @function insertList
 * @brief funzione che inserisce un nuovo elemento(chiave-possessore) nella lista (se non è già presente)
 *
 * @param l lista in cui inserire l'elemento 
 * @param k chiave da inserire nella lista
 * @param fd possessore da inserire nella lista
 *
 * @return 1 in caso di successo, 0 in caso di fallimento
 *
 */
int insertList (lista_t *l, unsigned long k, int fd);

/**
 * @function cerca_client
 * @brief funzione che cerca tutti i fd_possessore == fd nella lista l e inserisce le rispettive chiavi in out
 *
 * @param l lista in cui cercare gli fd 
 * @param fd fd da cercare
 * @param out lista in cui copiare le chiavi trovate
 *
 * @return 1 in caso di successo, 0 in caso di fallimento
 *
 */
int cerca_client (lista_t l, int fd, lista_t * out);

/**
 * @function removeList
 * @brief funzione che rimuove un elemento dalla lista l
 *
 * @param l lista in cui rimuovere l'elemento
 * @param k chiave da rimuovere
 *
 */
void removeList (lista_t *l, unsigned long k);

#endif
