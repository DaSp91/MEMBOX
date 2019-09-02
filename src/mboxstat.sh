#!/bin/bash

#  @file mboxstat.sh
#  @author Danilo Spano' 465347 & Daniele Trezza 489554
#   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
#   originale degli autori. 

fun_uso() {
echo "Utilizzo:
 $0 [-pugrcsomd] STATFILE
Legenda opzioni:

  --help       mostra il messaggio di help
  -p           stampa il numero di PUT_OP e PUT_OP fallite
  -u           stampa il numero di UPDATE_OP e UPDATE_OP fallite
  -g           stampa il numero di GET_OP e GET_OP fallite
  -r           stampa il numero di REMOVE_OP e REMOVE_OP fallite
  -c           stampa il numero di connessioni
  -s           stampa la size in KB
  -o           stampa il numero di oggetti
  -m           stampa il massimo numero di oggetti memorizzati e la massima size raggiunti
  -d           stampa il numero di DUMP

STATFILE è il file delle statistiche

Senza nessuna opzione lo script stampa tutte le statistiche dell’ultimo timestamp.
"
}

#creo un array che utilizzero' per controllare gli argomenti passati 
ARRAYARG=(0, 0, 0, 0, 0, 0, 0, 0, 0)

#controllo il numero di argomenti passati allo script
if [ $# -eq 0 ] || [ $# -gt 11 ]; then
  echo "Errore! Num argomenti leciti 1-11, Num di argomenti passati:$#"
  echo "Prova '$0 --help' per maggiori informazioni."
  exit -1
fi

#creo un array da tutti gli argomenti in ingresso
args=("$@")
#FILE contiene l'ultimo argomento passato allo script
FILE=${args[$#-1]}

#ulteriore controllo sul numero di argomenti:
# -> se è 1, controllo che sia un file o l'opzione --help;
# -> se > 1, controllo che l'ultimo argomento sia un file.
if [ $# -eq 1 ] && ! [ -f $1 ] && [ "$1" != "--help" ]; then
  echo "Errore! Argomento '$1' NON valido."
  echo "Prova '$0 --help' per maggiori informazioni."
  exit -1
elif ! [ $# -eq 1 ] && ! [ -f $FILE ]; then
  echo "Errore! Argomento '$FILE' NON valido."
  echo "Prova '$0 --help' per maggiori informazioni."
  exit -1    
fi

#controllo eventuali errori negli argomenti passati(escluso l'ultimo che ho gia' controllato)
for((i=0;i<$#-1;i++)); do
    if [ "${args[$i]}" != "--help" ] && [ "${args[$i]}" != "-p" ] && [ "${args[$i]}" != "-u" ] && [ "${args[$i]}" != "-g" ] && [ "${args[$i]}" != "-r" ] && [ "${args[$i]}" != "-c" ] && [ "${args[$i]}" != "-s" ] && [ "${args[$i]}" != "-o" ] && [ "${args[$i]}" != "-m" ] && [ "${args[$i]}" != "-d" ]; then 
      echo "Errore! Argomento: '${args[$i]}' NON valido."
      echo "Prova '$0 --help' per maggiori informazioni."
      exit -1
    fi
done

#Qui sono sicuro che gli argomenti sono corretti
while getopts ":pugrcsomd-:" opt; do
  case $opt in
    -) case $OPTARG in
          help)
            fun_uso
            exit 0
            ;;
       esac
       ;;
    p)
      ARRAYARG[0]=1
      ;;
    u)
      ARRAYARG[1]=1
      ;;
    g)
      ARRAYARG[2]=1
      ;;
    r)
      ARRAYARG[3]=1
      ;;
    c)
      ARRAYARG[4]=1
      ;;
    s)
      ARRAYARG[5]=1
      ;;
    o)
      ARRAYARG[6]=1
      ;;
    m)
      ARRAYARG[7]=1
      ;;
    d)
      ARRAYARG[8]=1
      ;;
  esac
done

OUTPUT="#timestamp"
if [ "${ARRAYARG[0]}" = 1 ]; then
  OUTPUT="$OUTPUT PUT PUTFAILED"
fi
if [ "${ARRAYARG[1]}" = 1 ]; then
  OUTPUT="$OUTPUT UPDATE UPDATEFAILED"
fi
if [ "${ARRAYARG[2]}" = 1 ]; then
  OUTPUT="$OUTPUT GET GETFAILED"
fi
if [ "${ARRAYARG[3]}" = 1 ]; then
  OUTPUT="$OUTPUT REMOVE REMOVEFAILED"
fi
if [ "${ARRAYARG[4]}" = 1 ]; then
  OUTPUT="$OUTPUT CONNECTIONS"
fi
if [ "${ARRAYARG[5]}" = 1 ]; then
  OUTPUT="$OUTPUT SIZE"
fi
if [ "${ARRAYARG[6]}" = 1 ]; then
  OUTPUT="$OUTPUT OBJECTS"
fi
if [ "${ARRAYARG[7]}" = 1 ]; then
  OUTPUT="$OUTPUT MAXOBJ MAXSIZE"
fi
if [ "${ARRAYARG[8]}" = 1 ]; then
  OUTPUT="$OUTPUT DUMP"
fi
# Se non ho opzioni stampo tutte le statistiche
if [ "$1" = "$FILE" ]; then
  OUTPUT="$OUTPUT PUT PUTFAILED UPDATE UPDATEFAILED GET GETFAILED REMOVE REMOVEFAILED CONNECTIONS SIZE OBJECTS MAXOBJ MAXSIZE DUMP"
fi

# Se non ho opzioni stampo le statistiche dell'ultimo timestamp
if [ "$1" = "$FILE" ]; then
  tail -n 1 $FILE > FileTemp
  read timestamp junk nput nputfailed nupdate nupdatefailed nget ngetfailed nremove nremovefailed nlock nlockfailed nobjlock nobjlockfailed ndump ndumpfailed nconnections ncurrentsize nmaxsize ncurrentobj nmaxobj < FileTemp
  OUTPUT="$OUTPUT\n$timestamp $nput $nputfailed $nupdate $nupdatefailed $nget $ngetfailed $nremove $nremovefailed $nconnections $ncurrentsize $ncurrentobj $nmaxobj $nmaxsize $ndump"
  rm FileTemp  
else
  while read timestamp junk nput nputfailed nupdate nupdatefailed nget ngetfailed nremove nremovefailed nlock nlockfailed nobjlock nobjlockfailed ndump ndumpfailed nconnections ncurrentsize nmaxsize ncurrentobj nmaxobj
  do
    OUTPUT="$OUTPUT\n$timestamp"
    if [ "${ARRAYARG[0]}" = 1 ]; then
      OUTPUT="$OUTPUT $nput $nputfailed"
    fi
    if [ "${ARRAYARG[1]}" = 1 ]; then
      OUTPUT="$OUTPUT $nupdate $nupdatefailed"
    fi
    if [ "${ARRAYARG[2]}" = 1 ]; then
      OUTPUT="$OUTPUT $nget $ngetfailed"
    fi
    if [ "${ARRAYARG[3]}" = 1 ]; then
      OUTPUT="$OUTPUT $nremove $nremovefailed"
    fi
    if [ "${ARRAYARG[4]}" = 1 ]; then
      OUTPUT="$OUTPUT $nconnections"
    fi
    if [ "${ARRAYARG[5]}" = 1 ]; then
      OUTPUT="$OUTPUT $ncurrentsize"
    fi
    if [ "${ARRAYARG[6]}" = 1 ]; then
      OUTPUT="$OUTPUT $ncurrentobj"
    fi
    if [ "${ARRAYARG[7]}" = 1 ]; then
      OUTPUT="$OUTPUT $nmaxobj $nmaxsize"
    fi
    if [ "${ARRAYARG[8]}" = 1 ]; then
      OUTPUT="$OUTPUT $ndump"
    fi
  done < $FILE
fi

echo -e $OUTPUT | column -t
