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

Lines should always be appended at the end of the file. And preferably (not strictly necessary), they are ordered by DATE.


All dates should be in UTC ISO-8601 format, like this: "2022-03-21T08:40:23Z".
Optionally, we can have a date only, like "2022-02-01", this is assumed as "2022-01-31T24:00:00Z".


Comments start with "#". Everything after "#" is not processed.

## Participant begins renting a room
```
START <DATE> <PARTICIPANT_ID> <PHONE_NUMBER> <EMAIL> <NAME>
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
PAY <DATE> <PARTICIPANT_ID> <BILL_TYPE_ID> <ENTITY> <REFERENCE> <AMOUNT> <BILLING_PERIOD_START_DATE> <BILLING_PERIOD_END_DATE>
```

## Shared goods are bought
```
BUY <DATE> <PARTICIPANT_ID> <AMOUNT> <DESCRIPTION>
```

## Payment from one participant to another
```
TRANSFER <DATE> <FROM_PARTICIPANT_ID> <TO_PARTICIPANT_ID> <AMOUNT>
```

# Pseudocode
The desired output of the program is the amount of debt between each pair of participants.

The algorithm for processing the file consists in reading the lines in the file in order, and adding information to a directed and weighted graph in which each node is a person and each edge is a representation of monetary value owned between them.

A TRANSFER event adds the amount specified to the weight of the edge between the sender and the receiver of the transfer.

Intervals of time in which a person is present, for example from a person's START event to a PAUSE event, are inserted into a interval tree data structure.

A PAY event queries the interval tree data structure for intervals overlapping the interval that the bill refers to. Then it separates it into sections in which the number of people present is different. It calculates the price per second (S) like so: S = ANOUNT / (BILLING\_PERIOD\_END\_DATE - BILLING\_PERIOD\_START\_DATE). Then, for each sub-interval [X, Y], being N the number of people present at that interval, it calculates the price per person by doing (Y - X) * S / N.

# Resources

- Range overlap [krl.c](https://github.com/LineageOS/android_external_openssh/blob/lineage-18.1/krl.c)
- Splitting range array [link](https://tousu.in/qa/?qa=3554098/)
- Another splitting ranges [reference](https://ssw.jku.at/Research/Papers/Wimmer05/Wimmer05.pdf)
- Multiple interval algorithms (very useful) [link](https://digitalcommons.usu.edu/cgi/viewcontent.cgi?article=8143&context=etd)
