#!/bin/bash
set -e

echo "[BUILD] Compilazione in corso..."
gcc -Wall -Wextra -o server server.c
gcc -Wall -Wextra -o client client.c
echo "[BUILD] Compilazione completata con successo"

SOCKET_PATH="/tmp/socket"

# Cleanup automatico: se lo script viene interrotto (Ctrl+C sul terminale
# che lo esegue) o esce per qualsiasi motivo, termina il server in modo
# ordinato invece di lasciarlo orfano.
cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo ""
        echo "[INFO] Chiusura del server (PID $SERVER_PID)..."
        kill -TERM "$SERVER_PID"
        wait "$SERVER_PID" 2>/dev/null
    fi
}
trap cleanup EXIT INT TERM

# Avvio del server in background
./server &
SERVER_PID=$!
echo "[INFO] Server avviato in background con PID $SERVER_PID"

# Attende che il socket sia effettivamente pronto, invece di un semplice
# sleep a tempo fisso: più affidabile se la macchina è sotto carico.
TRIES=0
until [ -S "$SOCKET_PATH" ] || [ "$TRIES" -ge 50 ]; do
    sleep 0.1
    TRIES=$((TRIES + 1))
done

if [ ! -S "$SOCKET_PATH" ]; then
    echo "[ERRORE] Il server non ha creato il socket in tempo, esco."
    exit 1
fi

echo "[INFO] Socket pronto su $SOCKET_PATH"
echo "[INFO] Apri altri terminali e lancia './client' per connettere più client"
echo "[INFO] Premi Ctrl+C qui per terminare il server in modo ordinato"
echo ""

# Il server resta in background: lo script attende semplicemente che
# termini (per SIGTERM esterno, o per Ctrl+C qui gestito dal trap).
wait "$SERVER_PID"