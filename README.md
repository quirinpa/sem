# Introduction
This is a solution for calculating shared expenses between people living in the same house. It relies on a human-readable text file that has information about bills, times new people arrive and leave, payments, etc.
From this file, debt between participants can be calculated freely and consistently.

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

# Running
```sh
cat data.txt | ./sem
```
# Algorithm
I've commented the code (sem.c) very extensively in order to make it understandable to
non-programmers. Please take a look at it.
