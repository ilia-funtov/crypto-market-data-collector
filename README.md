# crypto-market-data-collector
Market data collector for crypto exchanges

A tool with a command-line interface that helps to collect market data from popular cryptocurrency trade exhanges.
It collects level-2 prices (order book) and trade events (buys & sells).
Next exchanges are supported: Coinbase, Bitfinex, Bitmex, Kraken.
It is written in C++ 17.

How to build in Linux (Ubuntu)

Install prerequsites:

sudo apt install build-essential cmake

Install dependencies:

sudo apt install libboost-all-dev libssl-dev libcurl4-openssl-dev

Get the source and build:

git clone https://github.com/ilia-funtov/crypto-market-data-collector.git

cd crypto-market-data-collector

mkdir build

cd build

cmake ..

make

Just skip some warnings from openssl since some functions could be declared as deprecated.

Usage:

Get help about command line options

./market-data-collector --help

Run

./market-data-collector --dump-path ./market_data --symbol-config ../config/symbol_mapping.json

It always requires a path to a symbol mapping configuration file where exchange specific names are defined; and a path to dump collected data.

In the folder specified as a dump path two subfolder are created: prices and trades. Prices contains files with information from order books.
Trades contains files with information about trades. Files are in csv format.

Price file format is:

exchange name, timestamp in microseconds, best bid price, best bid volume, best ask price, best ask volume, next (bid price, bid volume, ask price, ask volume) repeated N times

Trade file format is:

exchange name, price, volume (positive for taker buy and negative for taker sell), timestamp in microseconds
