#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Check the number of arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <writefile> <writestr>\n", argv[0]);
        syslog(LOG_ERR, "Invalid number of arguments: expected 2, got %d", argc - 1);
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    // Open the syslog
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    // Open the file for writing
    FILE *file = fopen(writefile, "w");
    if (file == NULL) {
        perror("Error opening file");
        syslog(LOG_ERR, "Error opening file: %s", writefile);
        closelog();
        return 1;
    }

    // Write the string to the file
    if (fprintf(file, "%s", writestr) < 0) {
        perror("Error writing to file");
        syslog(LOG_ERR, "Error writing to file: %s", writefile);
        fclose(file);
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    // Close the file and syslog
    fclose(file);
    closelog();

    return 0;
}

