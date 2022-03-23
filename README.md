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
If they are in this format: "2022-03-21", "T00:00:00Z" is assumed.
The current date/time can be obtained in this format via:
```sh
date -u +"%Y-%m-%dT%H:%M:%SZ"
```

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
