#!/bin/bash

read -p "Commento commit: " msg

git add .
if ! git commit -m "$msg"; then
    echo "Nessun file da commitare."
    exit 1
fi

branch=$(git rev-parse --abbrev-ref HEAD)
git push origin "$branch"

