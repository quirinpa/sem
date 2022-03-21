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
4. Bill is received
5. Bill is paid
6. Shared goods are bought

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

## Bill is received
A unique BILL\_ID is assumed based on an incremental integer count from 0 based on the order of TYPE (4) of lines in the file.
```
4 <DATE> <BILL_TYPE> <BILLING_PERIOD_START_DATE> <BILLING_PERIOD_END_DATE> <ENTITY> <REFERENCE> <AMOUNT>
```

BILL\_TYPE can be:
0. Electricity
1. Communications
2. Water
3. Gas

## Bill is paid
```
5 <DATE> <BILL_ID> <PARTICIPANT_ID>
```

## Shared goods are bought
```
6 <DATE> <PARTICIPANT_ID> <AMOUNT> <DESCRIPTION>
```
