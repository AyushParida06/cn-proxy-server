// access control - Jaysmita


#include<stdio.h>
#include<string.h>

int main() {
	 FILE *fp = fopen("blacklist.txt", "r");   // open the list for reading
    if (fp == NULL) {                         // file missing: stop
        printf("Could not open file\n");
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        printf("[%s]\n", line);               // brackets show where each line really ends
    }

    fclose(fp);                               // always close the file
    return 0;
}
	