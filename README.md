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
STOP - Participant stops renting a room
PAUSE - Participant goes away temporarily
RESUME - Participant returns from temporary leave
PAY - Bill is paid
BUY - Shared goods are bought
TRANSFER - Payment from one participant to another
```

Lines should always be appended at the end of the file. It is assumed that they are ordered by the first DATE expressed in the line.


All dates should be in UTC ISO-8601 format, like this: "2022-03-21T08:40:23Z".
Optionally, we can have a date only, like "2022-02-01", this is assumed as "2022-01-31T24:00:00Z".


Comments start with "#". It is assumed that the required items in the line are present before the "#", except in cases where there is a "#" at the start of a line.

## Participant begins renting a room
```
START <DATE> <PARTICIPANT_ID> [<PHONE_NUMBER> <EMAIL> ... <NAME>]
```

## Participant stops renting a room
```
STOP <DATE> <PARTICIPANT_ID>
```

## Participant goes away temporarily
```
PAUSE <DATE> <PARTICIPANT_ID>
```

## Participant returns from temporary leave
```
RESUME <DATE> <PARTICIPANT_ID>
```

## Bill is paid
```
PAY <DATE> <PARTICIPANT_ID> <AMOUNT> <START_DATE> <END_DATE> [<BILL_TYPE_ID> <ENTITY> <REFERENCE> ...]
```

## Shared goods are bought
```
BUY <DATE> <PARTICIPANT_ID> <AMOUNT> [DESCRIPTION]
```

## Payment from one participant to another
```
TRANSFER <DATE> <FROM_PARTICIPANT_ID> <TO_PARTICIPANT_ID> <AMOUNT>
```

# Dependency
This program is dependant on BerkeleyDB 4.6 or similar.

# Running
```sh
cat data.txt | ./sem
```
