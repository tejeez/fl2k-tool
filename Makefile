fl-wspr: fl-wspr.c
	$(CC) fl-wspr.c -o $@ -Wall -Wextra -O3 -losmo-fl2k -lm
