/* This program reads in an xbase-dbf file and sends 'inserts' to an
   MySQL-server with the records in the xbase-file

   M. Boekhold (boekhold@cindy.et.tudelft.nl)  okt. 1995
   Patched for MySQL by Michael Widenius (monty@analytikerna.se) 96.11.03

   GLC - Gerald L. Clark (gerald_clark@sns.ca) june 1998
   Added Date and Boolean field conversions.
   Fixxed Quick mode insert for blank Numeric fields
   Modified to use -x flag to add _rec and _timestamp fields to start of record.
      ( only those lines immediately affect by if(express) (and getopt) )
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <mysql.h>
#include "dbf.h"

int verbose = 0, upper = 0, lower = 0, create = 0, fieldlow = 0, var_chars = 1;
int express = 0;
int null_fields = 0, trim = 0, quick = 0;
char primary[11];
char *host = NULL;
char *dbase = "test";
char *table = "test";
char *pass = NULL;
char *user = NULL;

char *subarg = NULL;
char *flist = NULL;
char *indexes = NULL;
char *convert = NULL;

void do_onlyfields(char *flist, dbhead *dbh);
void do_substitute(char *subarg, dbhead *dbh);
inline void strtoupper(char *string);
inline void strtolower(char *string);
void do_create(MYSQL *, char*, dbhead*, char *charset);
void do_inserts(MYSQL *, char*, dbhead*);
int check_table(MYSQL *, char*);
void usage(void);

inline void strtoupper(char *string) {
    while (*string != '\0') {
        *string = toupper(*string);
        string++;
    }
}

inline void strtolower(char *string) {
    while (*string != '\0') {
        *string = tolower(*string);
        string++;
    }
}

int check_table(MYSQL *sock, char *table) {
    MYSQL_RES *result;
    MYSQL_ROW row;

    if ((result = mysql_list_tables(sock, NULL)) == NULL)
        return 0;
    while ((row = mysql_fetch_row(result)) != NULL) {
        if (strcmp((char *) row[0], table) == 0) {
            mysql_free_result(result);
            return 1;
        }
    }
    mysql_free_result(result);
    return 0;
}

void usage(void) {
    printf("dbf2mysql %s\n", VERSION);
    printf("usage: dbf2mysql [-h hostname] [-d dbase] [-t table] [-p primary key]\n");
    printf("                 [-o field[,field]] [-s oldname=newname[,oldname=newname]]\n");
    printf("                 [-i field[,field]] [-c] [-f] [-F] [-n] [-r] [-u|-l] \n");
    printf("                 [-v[v]] [-x] [-q]  [-P password] [-U user] [-C charset]\n");
    printf("                 dbf-file\n");
}

/* patch by Mindaugas Riauba <minde@pub.osf.lt> */

/* Nulls non specified field names */

void do_onlyfields(char *flist, dbhead *dbh) {
    char *s, *flist2;
    int i, match;

    if (flist == NULL) return;

    if (verbose > 2)
        printf("Removing not specified fields\n");

    if ((flist2 = malloc(strlen(flist) * sizeof (char) + 1)) == NULL) {
        fprintf(stderr, "Memory allocation error in function do_onlyfields\n");
        dbf_close(&dbh);
        exit(1);
    }

    if (verbose > 2)
        printf("Used fields: ");
    for (i = 0; i < dbh -> db_nfields; i++) {
        strcpy(flist2, flist);
        match = 0;
        for (s = strtok(flist2, ","); s != NULL; s = strtok(NULL, ",")) {
            if (strcasecmp(s, dbh -> db_fields[i].db_name) == 0) {
                match = 1;
                if (verbose > 1)
                    printf("%s", s);
            }
        }
        if (match == 0) dbh -> db_fields[i].db_name[0] = '\0';
    }
    if (verbose > 2)
        printf("\n");
    free(flist2);
} /* do_onlyfields */

/* patch submitted by Jeffrey Y. Sue <jysue@aloha.net> */
/* Provides functionallity for substituting dBase-fieldnames for others */
/* Mainly for avoiding conflicts between fieldnames and MySQL-reserved */

/* keywords */

void do_substitute(char *subarg, dbhead *dbh) {
    /* NOTE: subarg is modified in this function */
    int i, bad;
    char *p, *oldname, *newname;
    if (subarg == NULL) return;

    if (verbose > 1) {
        printf("Substituting new field names\n");
    }
    /* use strstr instead of strtok because of possible empty tokens */
    oldname = subarg;
    while (oldname && strlen(oldname) && (p = strstr(oldname, "="))) {
        *p = '\0'; /* mark end of oldname */
        newname = ++p; /* point past \0 of oldname */
        if (strlen(newname)) { /* if not an empty string */
            p = strstr(newname, ",");
            if (p) {
                *p = '\0'; /* mark end of newname */
                p++; /* point past where the comma was */
            }
        }
        if (strlen(newname) >= DBF_NAMELEN) {
            printf("Truncating new field name %s to %d chars\n",
                    newname, DBF_NAMELEN - 1);
            newname[DBF_NAMELEN - 1] = '\0';
        }
        bad = 1;
        for (i = 0; i < dbh->db_nfields; i++) {
            if (strcmp(dbh->db_fields[i].db_name, oldname) == 0) {
                bad = 0;
                strcpy(dbh->db_fields[i].db_name, newname);
                if (verbose > 1) {
                    printf("Substitute old:%s new:%s\n",
                            oldname, newname);
                }
                break;
            }
        }
        if (bad) {
            printf("Warning: old field name %s not found\n",
                    oldname);
        }
        oldname = p;
    }
} /* do_substitute */

void do_create(MYSQL *SQLsock, char *table, dbhead *dbh, char *charset) {
    /* Patched by GLC to support date and boolean fields */
    /* Patched by WKV to support Visual FoxPro fields */
    char *query, *s;
    char t[20];
    int i, first_done;

    if (verbose > 2) {
        printf("Building CREATE-clause\n");
    }

    if (!(query = (char *) malloc((express * 100) +
            (dbh->db_nfields * 60) + 29 + strlen(table)))) {
        fprintf(stderr, "Memory allocation error in function do_create\n");
        mysql_close(SQLsock);
        dbf_close(&dbh);
        exit(1);
    }

    sprintf(query, "CREATE TABLE `%s` (", table);
    if (express) {
        strcat(query,
                "_rec int(10) unsigned DEFAULT '0' NOT NULL auto_increment PRIMARY KEY,\n");
        strcat(query, "_timestamp timestamp(14),\n");
    }
    first_done = 0;
    for (i = 0; i < dbh->db_nfields; i++) {
        if (!strlen(dbh->db_fields[i].db_name)) {
            continue;
            /* skip field if length of name == 0 */
        }
        if (first_done) {
            strcat(query, ",\n\t");
        }
        first_done = 1;
        if (fieldlow)
            strtolower(dbh->db_fields[i].db_name);

        strcat(query, "`\0");
        strcat(query, dbh->db_fields[i].db_name);
        strcat(query, "`\0");
        switch (dbh->db_fields[i].db_type) {
            case 'C':
                if (var_chars)
                    strcat(query, " varchar");
                else
                    strcat(query, " char");
                sprintf(t, "(%d)", dbh->db_fields[i].db_flen);
                strcat(query, t);
                break;

            case 'N':
                if (dbh->db_fields[i].db_dec != 0) {
                    strcat(query, " real"); /* decimal the better choice? */
                } else {
                    strcat(query, " int");
                }
                break;

            case 'L':
                /* strcat(query, " char (1)"); */
                strcat(query, " enum('F','T')");
                break;

            case 'D':
                strcat(query, " date");
                break;

            case 'M':
                strcat(query, " text");
                break;

            case 'F':
                strcat(query, " double");
                break;

            case 'B': /* ?Depends on DB vs FoxPro? Former is memo, latter is Binary */
                sprintf(t, " decimal(15,%d)", dbh->db_fields[i].db_dec);
                strcat(query, t);
                break;

            case 'G':
                strcat(query, " blob");
                break;

            case 'P':
                strcat(query, " blob");
                break;

            case 'Y':
                strcat(query, " decimal(21,4)");
                break;

            case 'T':
                strcat(query, " datetime");
                break;

            case 'I':
                strcat(query, " int");
                break;

        }
        if (strcmp(dbh->db_fields[i].db_name, primary) == 0) {
            strcat(query, " not null primary key");
        } else if (!null_fields)
            strcat(query, " not null");
    }

    if (indexes) /* add INDEX statements */ {
        for (s = strtok(indexes, ","); s != NULL; s = strtok(NULL, ",")) {
            strcat(query, ",INDEX(");
            strcat(query, s);
            strcat(query, ")");
        }
    }

    strcat(query, ")\n");

    if (charset) {
        strcat(query, " DEFAULT CHARSET=");
        strcat(query, charset);
        strcat(query, "\n");
    }

    if (verbose > 2) {
        printf("Sending create-clause\n");
        printf("%s\n", query);
        printf("fields in dbh %d, allocated mem for query %d, query size %d\n",
                dbh->db_nfields, (dbh->db_nfields * 60) + 29 + strlen(table), strlen(query));
    }

    if (mysql_query(SQLsock, query) != 0) {
        fprintf(stderr, "Error creating table!\n");
        fprintf(stderr, "Detailed report: %s\n", mysql_error(SQLsock));
        dbf_close(&dbh);
        free(query);
        mysql_close(SQLsock);
        exit(1);
    }

    free(query);
}

/* Patched by GLC to fix quick mode Numeric fields */
void do_inserts(MYSQL *SQLsock, char *table, dbhead *dbh) {
    int result, i, j, nc = 0, h;
    field *fields;
    char *query, *vpos, *pos;
    char str[257], *cvt = NULL, *s;
    u_long val_len = 0;
    char *datafile = NULL;
    FILE *fconv, *tempfile = NULL;
    int quote_field;
    u_long val_used;
    int base_pos;

    /* Max Number of characters that can be stored before checking buffer size */
#define VAL_EXTRA 16

    if (convert != NULL) /* If specified conversion table */ {
        if ((fconv = fopen(convert, "rt")) == NULL)
            fprintf(stderr, "Cannot open convert file '%s'.\n", convert);
        else {
            nc = atoi(fgets(str, 256, fconv));
            if (verbose > 2)
                printf("Using conversion table '%s' with %d entries\n", convert, nc);
            if ((cvt = (char *) malloc(nc * 2 + 1)) == NULL) {
                fprintf(stderr, "Memory allocation error in do_inserts (cvt)\n");
                mysql_close(SQLsock);
                dbf_close(&dbh);
                exit(1);
            }
            for (i = 0, fgets(str, 256, fconv); (i < nc * 2) && (str != NULL); i++) {
                cvt[i++] = atoi(strtok(str, " \t"));
                cvt[i] = atoi(strtok(NULL, " \t"));
                fgets(str, 256, fconv);
            }
            cvt[i] = '\0';
        }
    }

    if (verbose > 2) {
        printf("Inserting records\n");
    }

    for (i = 0; i < dbh->db_nfields; i++) {
        switch (dbh->db_fields[i].db_type) {
            case 'M':
            case 'G':
                val_len += 2048;
                break;
            case 'T':
                val_len += 23;
                break;
            default:
                val_len += dbh->db_fields[i].db_flen * 2 + 3;
        }
    }

    if (!(query = (char *) malloc((express * 10) + 26 + strlen(table) + val_len + VAL_EXTRA))) {
        fprintf(stderr,
                "Memory allocation error in function do_inserts (query)\n");
        mysql_close(SQLsock);
        dbf_close(&dbh);
        exit(1);
    }
    if (!quick) {
        sprintf(query, "INSERT INTO `%s` VALUES (", table);
    } else
        if (express)
        strcat(query, "NULL,NULL,");
    else /* if specified -q create file for 'LOAD DATA' */ {
        datafile = tempnam("/tmp", "d2my");
        tempfile = fdopen(open(datafile, O_WRONLY | O_CREAT | O_EXCL,
                0600), "wt");
        if (tempfile == NULL || datafile == NULL) {
            fprintf(stderr, "Cannot open file '%s' for writing\n", datafile);
            return;
        }
        query[0] = '\0';
    }
    base_pos = strlen(query);

    if ((fields = dbf_build_record(dbh)) == (field *) DBF_ERROR) {
        fprintf(stderr, "Couldn't create memory structure for record\n");
        return;
    }

    for (i = 0; i < dbh->db_records; i++) {
        result = dbf_get_record(dbh, fields, i);
        if (result == DBF_VALID) {
            vpos = query + base_pos;
            val_used = 0;
            for (h = 0; h < dbh->db_nfields; h++) {
                /* if length of fieldname==0, skip it */
                if (!strlen(fields[h].db_name))
                    continue;
                if (fields[h].db_type == 'N' ||
                        fields[h].db_type == 'F' ||
                        fields[h].db_type == 'B' ||
                        fields[h].db_type == 'Y' ||
                        fields[h].db_type == 'I')
                    quote_field = 0;
                else
                    quote_field = 1;

                if (vpos != query + base_pos) {
                    *vpos++ = ',';
                    val_used++;
                }

                if (quick || quote_field) {
                    *vpos++ = '\'';
                    val_used++;
                }

                if (trim && quote_field) /* trim leading spaces */ {
                    for (pos = fields[h].db_contents; isspace(*pos) && (*pos != '\0');
                            pos++);
                    memmove(fields[h].db_contents, pos, strlen(pos) + 1);
                }
                if ((nc > 0) && (fields[h].db_type == 'C')) {
                    for (j = 0; fields[h].db_contents[j] != '\0'; j++) {
                        if ((s = strchr(cvt, fields[h].db_contents[j])) != NULL)
                            if ((s - cvt) % 2 == 0)
                                fields[h].db_contents[j] = s[1];
                    }
                }
                if (upper)
                    strtoupper(fields[h].db_contents);
                if (lower)
                    strtolower(fields[h].db_contents);

                for (pos = fields[h].db_contents; *pos; pos++) {
                    if (*pos == '\\' || *pos == '\'') {
                        *vpos++ = '\\';
                        val_used++;
                    }
                    *vpos++ = *pos;
                    val_used++;
                    if (val_used >= val_len) {
                        //			int	newsiz = (((val_used + strlen(pos) + VAL_EXTRA + base_pos + 4095)/4096)+1)*4096-16;
                        int newsiz = base_pos + val_used + strlen(pos) + VAL_EXTRA;
                        void *tmpptr = realloc(query, newsiz);
                        if (tmpptr == NULL) {
                            perror("out of memory in do_inserts");
                            mysql_close(SQLsock);
                            dbf_close(&dbh);
                            exit(1);
                        }
                        query = tmpptr;
                        vpos = query + base_pos + val_used;
                        val_len = newsiz - base_pos - VAL_EXTRA;
                    }
                }
                if (quote_field && !quick) {
                    *vpos++ = '\'';
                    val_used++;
                } else {
                    if (!fields[h].db_contents[0]) { /* Numeric field is null */
                        if (null_fields) {
                            strcpy(vpos, "NULL");
                            vpos += 4;
                            val_used += 4;
                        } else {
                            if (!quote_field) {
                                *vpos++ = '0';
                                val_used++;
                            }
                        }
                    }
                }
                if (quick) {
                    *vpos++ = '\'';
                    val_used++;
                }
            }
            if (!quick) {
                vpos[0] = ')'; /* End of values */
                vpos[1] = 0; /* End of query */
            } else
                vpos[0] = '\0';
            if ((verbose == 3) && ((i % 100) == 0)) {
                printf("Inserting record %d\n", i);
            }
            if (verbose > 3) {
                printf("Record %4d: %s\n", i, query);
            }

            if (!quick) {
                if (mysql_query(SQLsock, query) == -1) {
                    fprintf(stderr,
                            "Error sending INSERT in record %04d\n", i);
                    fprintf(stderr,
                            "Detailed report: %s\n",
                            mysql_error(SQLsock));
                    if (verbose > 1) {
                        fprintf(stderr, "%s\n", query);
                    }
                }
            } else {
                if (express)
                    fprintf(tempfile, "NULL,NULL,%s\n", query);
                else
                    fprintf(tempfile, "%s\n", query);
            }
        }
    }

    dbf_free_record(dbh, fields);

    free(query);
    if (nc > 0)
        free(cvt);

    if (quick) {
        fclose(tempfile);
        query = str;
        sprintf(query, "LOAD DATA LOCAL INFILE '%s' REPLACE INTO table %s fields terminated by ',' enclosed by ''''",
                datafile, table);
        if (verbose > 1) {
            fprintf(stderr, "%s\n", query);
        }
        if (mysql_query(SQLsock, query) == -1) {
            fprintf(stderr,
                    "Error sending LOAD DATA INFILE from file '%s'\n", datafile);
            fprintf(stderr,
                    "Detailed report: %s\n",
                    mysql_error(SQLsock));
        }
        if (unlink(datafile) == -1) {
            fprintf(stderr, "Error while removing temporary file '%s'.\n", datafile);
        }
        free(datafile);
    }
}

int main(int argc, char **argv) {
    int i;
    MYSQL *SQLsock, mysql;
    extern int optind;
    extern char *optarg;
    char *query;
    dbhead *dbh;
    char *charset;

    primary[0] = '\0';

    while ((i = getopt(argc, argv, "xqfFrne:lucvi:h:p:d:t:s:o:U:P:C:")) != EOF) {
        switch (i) {
            case 'P':
                pass = (char *) strdup(optarg);
                break;
            case 'U':
                user = (char *) strdup(optarg);
                break;
            case 'x':
                express = 1;
                break;
            case 'f':
                fieldlow = 1;
                break;
            case 'F':
                var_chars = 0;
                break;
            case 'r':
                trim = 1;
                break;
            case 'n':
                null_fields = 1;
                break;
            case 'v':
                verbose++;
                break;
            case 'c':
                create++;
                break;
            case 'l':
                lower = 1;
                break;
            case 'u':
                if (lower) {
                    usage();
                    printf("Can't use -u and -l at the same time!\n");
                    exit(1);
                }
                upper = 1;
                break;
            case 'e':
                convert = (char *) strdup(optarg);
                break;
            case 'h':
                host = (char *) strdup(optarg);
                break;
            case 'q':
                quick = 1;
                break;
            case 'p':
                strncpy(primary, optarg, 11);
                break;
            case 'd':
                dbase = (char *) strdup(optarg);
                break;
            case 't':
                table = (char *) strdup(optarg);
                break;
            case 'i':
                indexes = (char *) strdup(optarg);
                break;
            case 's':
                subarg = (char *) strdup(optarg);
                break;
            case 'o':
                flist = (char *) strdup(optarg);
                break;
            case 'C':
                charset = (char *) strdup(optarg);
                break;
            case ':':
                usage();
                printf("missing argument!\n");
                exit(1);
            case '?':
                usage();
                printf("unknown argument: %s\n", argv[0]);
                exit(1);
            default:
                break;
        }
    }

    argc -= optind;
    argv = &argv[optind];

    if (argc != 1) {
        usage();
        exit(1);
    }

    if (verbose > 2) {
        printf("Opening dbf-file %s\n", argv[0]);
    }

    if ((dbh = dbf_open(argv[0], O_RDONLY)) == (dbhead *) - 1) {
        fprintf(stderr, "Couldn't open xbase-file %s\n", argv[0]);
        exit(1);
    }

    if (verbose) {
        printf("dbf-file: %s - %s, MySQL-dbase: %s, MySQL-table: %s\n",
                argv[0],
                dbh->db_description,
                dbase,
                table);
        printf("Number of records: %ld\n", dbh->db_records);
    }
    if (verbose > 1) {
        printf("Name\t\t Length\tDisplay\t Type\n");
        printf("-------------------------------------\n");
        for (i = 0; i < dbh->db_nfields; i++) {
            printf("%-12s\t%7d\t%5d\t%3c\n",
                    dbh->db_fields[i].db_name,
                    dbh->db_fields[i].db_flen,
                    dbh->db_fields[i].db_dec,
                    dbh->db_fields[i].db_type);
        }
    }

    if (verbose > 2) {
        printf("Making connection to MySQL-server\n");
    }

    // Init mysql
    if (!(mysql_init(&mysql))) {
        fprintf(stderr, "Can't initialize. Insufficient memory.");
        dbf_close(&dbh);
        exit(1);
    }

    if (!(SQLsock = mysql_real_connect(&mysql, host, user, pass, NULL, 0, NULL, 0))) {
        fprintf(stderr, "Couldn't get a connection with the ");
        fprintf(stderr, "designated host!\n");
        fprintf(stderr, "Detailed report: %s\n", mysql_error(&mysql));
        dbf_close(&dbh);
        exit(1);
    }

    if (verbose > 2) {
        printf("Selecting database '%s'\n", dbase);
    }

    if ((mysql_select_db(SQLsock, dbase)) == -1) {
        fprintf(stderr, "Couldn't select database %s.\n", dbase);
        fprintf(stderr, "Detailed report: %s\n", mysql_error(SQLsock));
        mysql_close(SQLsock);
        dbf_close(&dbh);
        exit(1);
    }

    if (charset && !mysql_set_character_set(&mysql, charset)) {
        printf("New client character set: %s\n",
                mysql_character_set_name(&mysql));
    }

    /* Substitute field names */
    do_onlyfields(flist, dbh);
    do_substitute(subarg, dbh);

    if (!create) {
        if (!check_table(SQLsock, table)) {
            printf("Table does not exist!\n");
            exit(1);
        }
    } else {
        if (verbose > 2) {
            printf("Dropping original table (if one exists)\n");
        }

        if (!(query = (char *) malloc(12 + strlen(table)))) {
            printf("Memory-allocation error in main (drop)!\n");
            mysql_close(SQLsock);
            dbf_close(&dbh);
            exit(1);
        }

        sprintf(query, "DROP TABLE `%s`", table);
        mysql_query(SQLsock, query);
        free(query);

        /* Build a CREATE-clause
         */
        do_create(SQLsock, table, dbh, charset);
    }

    /* Build an INSERT-clause
     */
    if (create < 2)
        do_inserts(SQLsock, table, dbh);

    if (verbose > 2) {
        printf("Closing up....\n");
    }

    mysql_close(SQLsock);
    dbf_close(&dbh);
    exit(0);
}
