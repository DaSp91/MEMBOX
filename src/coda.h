/** 
 * @file coda.h
 * @brief Contiene le dichiarazioni delle funzioni implementate in coda.c 
 *
 * @author Danilo Spano' 465347 & Daniele Trezza 489554
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 * originale degli autori.
 */

#ifndef CODA_H_
#define CODA_H_

/* Elemento della coda. */
typedef struct Node {
    void        * data;
    struct Node * next;
} Node_t;

/* Struttura dati coda. */
typedef struct Coda {
    Node_t        *head;
    Node_t        *tail;
    unsigned long  len;
} Coda_t;


/**
 * @function initCoda
 * @brief Alloca ed inizializza una coda di tipo Coda_t 
 *  
 * @return NULL se si sono verificati problemi nell'allocazione (errno settato), il puntatore alla coda q in caso di successo 
 */
Coda_t *initCoda();

/**
 * @function deleteCoda
 * @brief Cancella una coda allocata con initQueue.
 *  
 * @param q puntatore alla coda da cancellare
 */
void deleteCoda(Coda_t *q);

/**
 * @function inserisci
 * @brief Inserisce un elemento(data) nella coda(q)
 *   
 * @param data è l'elemento da inserire, q è la coda nella quale va inserito l'elemento
 * @return 0 in caso di successo, -1 in caso di errore (errno settato opportunamente)
 */
int inserisci(Coda_t *q, void *data);

/**
 * @function estrai
 * @brief  Estrae un elemento dalla coda q
 *
 * @param q è la coda dalla quale estrarre l'elemento
 * @return l'elemento estratto (di tipo void)
*/
void *estrai(Coda_t *q);

/**
 * @function length
 *
 * @param q è la coda da cui leggere la lunghezza
 * @return la lunghezza della coda q
 */
unsigned long length(Coda_t *q);

#endif
