#!/bin/bash

read -p "Commento commit (titolo): " msg
read -p "Descrizione commit (testo): " body

git add .

# Commit con titolo + corpo
git commit -m "$msg" -m "$body"

# Recupero branch corrente
branch=$(git rev-parse --abbrev-ref HEAD)

# Push
git push origin "$branch"

