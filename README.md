# Introduction
This is a solution for calculating shared expenses between people living in the same house. It relies on a human-readable text file that has information about bills, times new people arrive and leave, payments, etc.
From this file, debt between participants can be calculated freely and consistently.

# Data format
Each line in the text file consists of the following format:
```
<TYPE> <DATE> [...OTHERS]
```

There are the following TYPEs:

0. Participant begins renting a room
1. Participant stops renting a room
2. Participant goes away temporarily
3. Participant returns from temporary leave
4. Add bill type
5. Bill is received
6. Bill is paid
7. Shared goods are bought
8. Payment from one participant to another

All dates should be in UTC ISO-8601 format, like this: "2022-03-21T08:40:23Z". Lines in this file should be ordered by DATE.

## Participant begins renting a room
A unique PARTICIPANT\_ID is assumed based on an incremental integer count from 0 based on the order of TYPE (0) of lines in the file.
```
0 <DATE> <PHONE_NUMBER> <EMAIL> <NAME>
```

## Participant stops renting a room
```
1 <DATE> <PARTICIPANT_ID>
```

## Participant goes away temporarily
```
2 <DATE> <PARTICIPANT_ID>
```

## Participant returns from temporary leave
```
3 <DATE> <PARTICIPANT_ID>
```

## Add bill type
A unique BILL\_TYPE\_ID is assumed based on an incremental integer count from 0 based on the order of TYPE (4) of lines in the file.
```
3 <DATE> <BILL_TYPE_NAME>
```

## Bill is received
A unique BILL\_ID is assumed based on an incremental integer count from 0 based on the order of TYPE (5) of lines in the file.
```
5 <DATE> <BILL_TYPE_ID> <BILLING_PERIOD_START_DATE> <BILLING_PERIOD_END_DATE> <ENTITY> <REFERENCE> <AMOUNT>
```

## Bill is paid
```
6 <DATE> <BILL_ID> <PARTICIPANT_ID>
```

## Shared goods are bought
```
7 <DATE> <PARTICIPANT_ID> <AMOUNT> <DESCRIPTION>
```

## Payment from one participant to another
```
8 <DATE> <FROM_PARTICIPANT_ID> <TO_PARTICIPANT_ID> <AMOUNT>
```
