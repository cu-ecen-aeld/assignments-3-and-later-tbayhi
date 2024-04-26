#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    int retval = 0;

    if (argc < 3) {
        printf("Not all required arguments were provided.\n");
        retval = 1;
    } else {
        // first arg is file path
        char *fname = argv[1];

        // second arg is string to write
        char *writestr = argv[2];

        openlog(NULL, 0, LOG_USER);
        syslog(LOG_DEBUG, "Writing %s to %s.", writestr, fname);
        
        FILE *fp = fopen(fname, "w");

        if (fp == NULL) {
            char *errmsg;
            
            sprintf(errmsg, "Could not write to %s.", fname);

            printf("%s\n", errmsg);
            syslog(LOG_ERR, "%s", errmsg);

            retval = 1;
        }

        fprintf(fp, "%s", writestr);

        if (fclose(fp) != 0) {
            char *errmsg;

            sprintf(errmsg, "Could not write to %s.", fname);

            printf("%s\n", errmsg);
            syslog(LOG_ERR, "%s", errmsg);

            retval = 1;
        }

        closelog();
    }

    return retval;
}
