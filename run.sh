#!/bin/bash

gcc -Wall server.c -o server || { echo "Errore compilazione server"; exit 1; }
gcc -Wall client.c -o client || { echo "Errore compilazione client"; exit 1; }

echo "[BUILD] Compilazione completata con successo"

# 2. Avvio del processo server in background
./server &
SERVER_PID=$!
echo "[INFO] Server avviato in background con PID $SERVER_PID."

# Breve attesa per permettere al server di creare il socket prima della connessione
sleep 0.5

# 3. Avvio del client in primo piano
./client

# 4. Al termine del client, invia SIGTERM al server per la chiusura ordinata dell'applicazione
echo ""
echo "[INFO] Client chiuso. Invio SIGTERM al server (PID $SERVER_PID)..."
kill -TERM "$SERVER_PID" 2>/dev/null