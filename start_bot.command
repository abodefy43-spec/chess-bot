#!/bin/bash
# Double-click this file to put the bot online on Lichess.
# Close the Terminal window (or press Ctrl-C) to take the bot offline.

set -e
cd "$(dirname "$0")"

echo "=============================================="
echo "  Lichess bot — Ripper chess engine"
echo "=============================================="
echo

# --- Load secrets ---
if [ ! -f .env ]; then
    echo "ERROR: .env file not found."
    echo "Copy .env.example to .env and put your Lichess bot token in it."
    read -p "Press enter to close..." _
    exit 1
fi
set -a; . ./.env; set +a
if [ -z "${LICHESS_BOT_TOKEN:-}" ] || [ "$LICHESS_BOT_TOKEN" = "lip_XXXXXXXXXXXXXXXXXXXXXX" ]; then
    echo "ERROR: LICHESS_BOT_TOKEN is not set in .env."
    read -p "Press enter to close..." _
    exit 1
fi

# --- Build the engine if needed ---
if [ ! -x ./ripper ] || [ -n "$(find src -newer ripper 2>/dev/null)" ]; then
    echo "Building engine..."
    make -s
    echo
fi

# --- Set up lichess-bot tool (first run only) ---
if [ ! -d lichess-bot ]; then
    echo "First run — cloning lichess-bot..."
    git clone --depth 1 https://github.com/lichess-bot-devs/lichess-bot.git
    echo
fi
if [ ! -d lichess-bot/.venv ]; then
    echo "Installing lichess-bot dependencies (one-time setup)..."
    python3 -m venv lichess-bot/.venv
    lichess-bot/.venv/bin/pip install --quiet -r lichess-bot/requirements.txt
    echo
fi

# --- Generate the lichess-bot config from template with secrets substituted ---
ENGINE_DIR="$(pwd)"
PGN_DIR="$(pwd)/pgn_games"
mkdir -p "$PGN_DIR"

sed -e "s|__LICHESS_BOT_TOKEN__|${LICHESS_BOT_TOKEN}|g" \
    -e "s|__ENGINE_DIR__|${ENGINE_DIR}|g" \
    -e "s|__PGN_DIR__|${PGN_DIR}|g" \
    config.template.yml > lichess-bot/config.yml

echo "Starting lichess-bot. Leave this window open while you play."
echo "Challenge at: https://lichess.org/@/Akera_bot"
echo
echo "Press Ctrl-C here (or close this window) to take the bot offline."
echo "----------------------------------------------"
echo

cd lichess-bot
exec .venv/bin/python lichess-bot.py
