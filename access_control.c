// access control - Jaysmita


#include<stdio.h>
#include<string.h>

int is_blocked(const char *host) {
    FILE *fp = fopen("blacklist.txt", "r");   // open the list for reading
    if (fp == NULL) {                         // file missing: stop
        printf("Could not open file\n");
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
    	line[strcspn(line, "\r\n")] = '\0';
	if (strcmp(line, host) == 0) {
		fclose(fp);
		return 1;
	}
        
    }

    fclose(fp);                               
    return 0;
}

#ifdef TEST
int main() {
    printf("facebook.com -> %d\n", is_blocked("facebook.com"));
    printf("example.com  -> %d\n", is_blocked("example.com"));
    return 0;
}
#endif
	