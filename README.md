# Introduction
This is a solution for calculating shared expenses between people living in the same house. It relies on a human-readable text file that has information about bills, times new people arrive and leave, payments, etc.
From this file, debt between participants can be calculated freely and consistently.

# Building
On Linux, you might have to provide "LIBDB\_PATH", if compiling on Alpine Linux, also provide "ALPINE=y".

To build on Alpine Linux:
```sh
make ALPINE=y
```
To build on regular linux, you might have to provide "LIBDB\_PATH" in case the default doesn't work for you. Here's an example:
```sh
make LIBDB_PATH=$LIBDB_PATH
```

# Running
You can:
```sh
./sem -?
```
To find out about the available options. Otherwise, feed a file to sem:
```sh
./sem < data.txt
```
You will get a result like:
```
leon owes quirinpa 58.45€
tomas owes quirinpa 2.10€
tomas owes leon 29.54€
elias owes leon 23.55€
vitor owes quirinpa 278.95€
emad owes leon 108.86
```
If you run it with the following options:
```sh
./sem -hgd < data.txt
```
you get a bigger result. Here's the last section of it:
```
* PAY 2022-08-18 quirinpa 3149 2022-06-23 2022-07-22 # comms
|   2022-06-29 518400 652 leon
|   2022-07-15 1382400 869 leon quirinpa
|   2022-07-22 604800 761 quirinpa
* PAY 2022-09-02 quirinpa 833 2022-07-20 2022-08-20 # gas
|   2022-08-20 2678400 834 quirinpa
* PAY 2022-09-02 quirinpa 4136 2022-07-27 2022-08-26 # light
|   2022-08-26 2592000 4137 quirinpa
* BUY 2022-09-06 quirinpa 853 # 8.53 sonasol, lixivia, sonasol wc, sacos do lixo
|   427 quirinpa leon
\
*| RESUME 2022-09-11 leon
|* PAY 2022-09-18 quirinpa 3149 2022-07-23 2022-08-22 # comms
||   2022-08-22 2592000 3150 quirinpa
\\
*|| START 2022-09-18 tomas7
\\\
*||| START 2022-09-19 rafael
leon owes quirinpa 58.45€
tomas owes quirinpa 2.10€
tomas owes leon 29.54€
elias owes leon 23.55€
vitor owes quirinpa 278.95€
emad owes leon 108.86€
```

But before you run the program, you need to understand the following section of this document.

# Data format
Each line in the text file consists of the following format:
```
<TYPE> <DATE> [...OTHERS]
```

There are the following TYPEs:

```
START - Participant begins renting a room
PAUSE - Participant goes away temporarily
RESUME - Participant returns from temporary leave
STOP - Participant stops renting a room
TRANSFER - Payment from one participant to another
BUY - Shared goods are bought
PAY - Bill is paid
```

Lines should always be appended at the end of the file. It is assumed that they are ordered by the first DATE expressed in the line.


All dates should be in UTC ISO-8601 format, like this: "2022-03-21T08:40:23Z".
Optionally, we can have a date only, like "2022-02-01", this is assumed as "2022-01-31T24:00:00Z".


Comments start with "#". It is assumed that the required items in the line are present before the "#", except in cases where there is a "#" at the start of a line.

## Participant begins renting a room
```
START <DATE> <PERSON_ID> [<PHONE_NUMBER> <EMAIL> ... <NAME>]
```

## Participant goes away temporarily
```
PAUSE <DATE> <PERSON_ID>
```

## Participant returns from temporary leave
```
RESUME <DATE> <PERSON_ID>
```

## Participant stops renting a room
```
STOP <DATE> <PERSON_ID>
```

## Payment from one participant to another
```
TRANSFER <DATE> <FROM_PERSON_ID> <TO_PERSON_ID> <AMOUNT>
```

## Shared goods are bought
```
BUY <DATE> <PERSON_ID> <AMOUNT> [DESCRIPTION]
```

## Bill is paid
```
PAY <DATE> <PERSON_ID> <AMOUNT> <START_DATE> <END_DATE> [<BILL_TYPE_ID> <ENTITY> <REFERENCE> ...]
```

# Dependency
This program is dependant on BerkeleyDB 4.6 or similar.

# Algorithm
I've commented the code (sem.c) very extensively in order to make it understandable to
non-programmers. Please take a look at it if you like.
